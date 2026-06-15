#!/usr/bin/env python3
"""Diff local chain against scraped prod data; emit repair txs for whatever is
missing (objectives, actions, pending claims, checks) or mismatched (action
usages_left / is_completed patches). Output: repair.jsonl — same format as the
pNN files, safe to re-run until clean.

Run AFTER the community/netlink/role phases are correct (p2-p5 re-pushed).
"""
import json
import hashlib
import time
import os
import urllib.request
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
NODE = "http://127.0.0.1:8888"
CM = "cambiatus.cm"
GEN_NOW = int(time.time())


def rpc(path, body):
    req = urllib.request.Request(NODE + path, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req).read())


def table(scope, tbl, **kw):
    rows, lower = [], ""
    while True:
        r = rpc("/v1/chain/get_table_rows", {
            "json": True, "code": CM, "scope": str(scope), "table": tbl,
            "limit": 1000, "lower_bound": lower, **kw})
        rows += r["rows"]
        if not r.get("more"):
            return rows
        lower = r["next_key"]


def load(name):
    with open(os.path.join(HERE, name)) as f:
        return json.load(f)


def name_to_uint64(s):
    charmap = ".12345abcdefghijklmnopqrstuvwxyz"
    v = 0
    for i in range(13):
        c = charmap.index(s[i]) if i < len(s) else 0
        if i < 12:
            v |= (c & 0x1F) << (64 - 5 * (i + 1))
        else:
            v |= c & 0x0F
    return v


def sym_scope(sym):
    prec, code = sym.split(",")
    v = 0
    for c in reversed(code):
        v = (v << 8) | ord(c)
    return (v << 8) | int(prec)


def act(account, name, actor, data):
    return {"account": account, "name": name,
            "authorization": [{"actor": actor, "permission": "active"}],
            "data": data}


def setindices(sale, obj, action, claim):
    return act(CM, "setindices", CM, {
        "sale_id": sale, "objective_id": obj,
        "action_id": action, "claim_id": claim})


communities = load("communities.json")
indexes = load("indexes.json")[0]
actions_rows = sorted(load("actions.json"), key=lambda r: int(r["id"]))
claims_rows = load("claims.json")
checks_rows = load("checks.json")
validators_rows = load("validators.json")

members, objectives = {}, {}
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    members[code] = {r["name"] for r in load(f"member_{code}.json")}
    objectives[code] = sorted(load(f"objective_{code}.json"),
                              key=lambda r: int(r["id"]))

obj_community = {}
for code, objs in objectives.items():
    for o in objs:
        obj_community[int(o["id"])] = code
cmm_by_code = {c["symbol"].split(",")[1]: c for c in communities}
action_by_id = {int(r["id"]): r for r in actions_rows}
validators_by_action = defaultdict(list)
for v in validators_rows:
    validators_by_action[int(v["action_id"])].append(v["validator"])

# local state
local_objs = {}
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    local_objs[code] = {int(o["id"]) for o in table(sym_scope(cmm["symbol"]), "objective")}
local_actions = {int(r["id"]): r for r in table(CM, "action")}
local_claims = {int(r["id"]) for r in table(CM, "claim")}
local_checks = defaultdict(set)
for ch in table(CM, "check"):
    local_checks[int(ch["claim_id"])].add(ch["validator"])

txs = []
last_obj = int(indexes["last_used_objective_id"])
last_act = int(indexes["last_used_action_id"])

# missing objectives
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    if not cmm["has_objectives"]:
        continue
    for o in objectives[code]:
        oid = int(o["id"])
        if oid in local_objs[code]:
            continue
        editor = o["creator"] if o["creator"] in members[code] else cmm["creator"]
        txs.append((f"repair objective {oid} ({code})", [
            setindices(0, oid - 1, last_act, 0),
            act(CM, "upsertobjctv", editor, {
                "community_id": cmm["symbol"], "objective_id": 0,
                "description": o["description"], "editor": editor})]))


def action_data(row, sym, creator, usages_left, is_completed):
    aid = int(row["id"])
    vals = [v for v in validators_by_action.get(aid, [])
            if v in members[sym.split(",")[1]]]
    verifications = int(row["verifications"])
    # current contract refuses verifications <3 or even; old prod rows predate that
    if verifications > 0 and (verifications < 3 or verifications % 2 == 0):
        verifications = max(3, verifications + (verifications % 2 == 0))
        if verifications % 2 == 0:
            verifications += 1
    if row["verification_type"] == "claimable" and vals and (
            len(vals) < 2 or len(vals) < verifications):
        vals = []
    return {
        "community_id": sym,
        "action_id": 0 if aid not in local_actions else aid,
        "objective_id": int(row["objective_id"]),
        "description": row["description"],
        "reward": row["reward"],
        "verifier_reward": row["verifier_reward"],
        "deadline": int(row["deadline"]),
        "usages": int(row["usages"]),
        "usages_left": usages_left,
        "verifications": verifications,
        "verification_type": row["verification_type"],
        "validators_str": "-".join(vals),
        "is_completed": is_completed,
        "creator": creator,
        "has_proof_photo": row["has_proof_photo"],
        "has_proof_code": row["has_proof_code"],
        "photo_proof_instructions": row["photo_proof_instructions"],
        "image": "",
    }


# missing actions (created un-completed with full usages; patched later)
created_now = set()
for row in actions_rows:
    aid = int(row["id"])
    if aid in local_actions:
        continue
    code = obj_community.get(int(row["objective_id"]))
    if code is None or not cmm_by_code[code]["has_objectives"]:
        continue
    cmm = cmm_by_code[code]
    creator = row["creator"] if row["creator"] in members[code] else cmm["creator"]
    txs.append((f"repair action {aid} ({code})", [
        setindices(0, last_obj, aid - 1, 0),
        act(CM, "upsertaction", creator,
            action_data(row, cmm["symbol"], creator, int(row["usages"]), 0))]))
    created_now.add(aid)

# missing pending claims
pending = sorted((c for c in claims_rows if c["status"] == "pending"),
                 key=lambda c: int(c["id"]))
claims_now = set()
for c in pending:
    cid, aid = int(c["id"]), int(c["action_id"])
    if cid in local_claims:
        continue
    row = action_by_id.get(aid)
    if row is None:
        continue
    code = obj_community.get(int(row["objective_id"]))
    if code is None or not cmm_by_code[code]["has_objectives"]:
        continue
    if aid not in local_actions and aid not in created_now:
        continue
    # claim must land before the action is patched to completed/exhausted —
    # if the local action is already patched, unpatch first and re-patch later
    cmm = cmm_by_code[code]
    if c["claimer"] not in members[code] or row["verification_type"] != "claimable":
        continue
    la = local_actions.get(aid)
    if la and (int(la["is_completed"]) or
               (int(la["usages"]) > 0 and int(la["usages_left"]) < 1)):
        creator = row["creator"] if row["creator"] in members[code] else cmm["creator"]
        txs.append((f"unpatch action {aid} for claim {cid}", [
            act(CM, "upsertaction", creator,
                action_data(row, cmm["symbol"], creator, int(row["usages"]), 0))]))
        local_actions[aid]["is_completed"] = 0
        local_actions[aid]["usages_left"] = local_actions[aid]["usages"]
    proof_photo = c["proof_photo"]
    if int(row["has_proof_photo"]) and not proof_photo:
        proof_photo = "https://cambiatus.io/replay-placeholder.jpg"
    proof_code, proof_time = "", 0
    if int(row["has_proof_code"]):
        proof_time = GEN_NOW
        proof = f"{aid}{name_to_uint64(c['claimer'])}{proof_time}"
        proof_code = hashlib.sha256(proof.encode()).hexdigest()[:8]
    txs.append((f"repair claim {cid} on action {aid}", [
        setindices(0, last_obj, last_act, cid - 1),
        act(CM, "claimaction", c["claimer"], {
            "community_id": cmm["symbol"], "action_id": aid,
            "maker": c["claimer"], "proof_photo": proof_photo,
            "proof_code": proof_code, "proof_time": proof_time})]))
    claims_now.add(cid)

# missing checks on pending claims
for ch in checks_rows:
    cid = int(ch["claim_id"])
    if cid not in {int(c["id"]) for c in pending}:
        continue
    if cid not in local_claims and cid not in claims_now:
        continue
    if ch["validator"] in local_checks.get(cid, set()):
        continue
    aid = int(next(c for c in pending if int(c["id"]) == cid)["action_id"])
    row = action_by_id[aid]
    code = obj_community[int(row["objective_id"])]
    if ch["validator"] not in members[code]:
        continue
    txs.append((f"repair check claim {cid} by {ch['validator']}", [
        act(CM, "verifyclaim", ch["validator"], {
            "community_id": cmm_by_code[code]["symbol"], "claim_id": cid,
            "verifier": ch["validator"], "vote": int(ch["is_verified"])})]))

# patches: local action state must match prod usages_left/is_completed
for row in actions_rows:
    aid = int(row["id"])
    la = local_actions.get(aid)
    target_ul, target_ic = int(row["usages_left"]), int(row["is_completed"])
    needs = (aid in created_now and (target_ul != int(row["usages"]) or target_ic)) or \
            (la and (int(la["usages_left"]) != target_ul or int(la["is_completed"]) != target_ic))
    if not needs:
        continue
    code = obj_community.get(int(row["objective_id"]))
    if code is None or not cmm_by_code[code]["has_objectives"]:
        continue
    cmm = cmm_by_code[code]
    creator = row["creator"] if row["creator"] in members[code] else cmm["creator"]
    data = action_data(row, cmm["symbol"], creator, target_ul, target_ic)
    data["action_id"] = aid
    txs.append((f"repair patch action {aid}", [act(CM, "upsertaction", creator, data)]))

# final indexes
txs.append(("final indexes", [setindices(
    int(indexes["last_used_sale_id"]), int(indexes["last_used_objective_id"]),
    int(indexes["last_used_action_id"]), int(indexes["last_used_claim_id"]))]))

with open(os.path.join(HERE, "repair.jsonl"), "w") as f:
    for label, actions in txs:
        f.write(json.dumps({"label": label, "tx": {"actions": actions}}) + "\n")
print(f"repair.jsonl: {len(txs)} txs")
