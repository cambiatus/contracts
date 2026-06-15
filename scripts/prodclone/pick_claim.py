#!/usr/bin/env python3
"""Pick a pending claim + verifier from the LOCAL cloned chain for validation.

Usage: pick_claim.py explicit|rolebased
Prints: "<claim_id> <verifier> <symbol>" or nothing if no candidate.

explicit  — claim on an action that still has explicit validators; verifier is
            a listed validator who hasn't voted; claim has zero checks so one
            vote can't resolve it (verifications >= 3).
rolebased — claim on an action whose validators were migrated away (local
            validator table empty, prod list non-empty); verifier is a member
            with the verify permission who was NOT in the original list.
"""
import json
import sys
import os
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
NODE = "http://127.0.0.1:8888"
CM = "cambiatus.cm"
MODE = sys.argv[1]


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


def sym_scope(sym):
    prec, code = sym.split(",")
    v = 0
    for c in reversed(code):
        v = (v << 8) | ord(c)
    return (v << 8) | int(prec)


communities = load("communities.json")
actions = {int(r["id"]): r for r in load("actions.json")}
obj_community = {}
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    for o in load(f"objective_{code}.json"):
        obj_community[int(o["id"])] = cmm

orig_validators = {}
for v in load("validators.json"):
    orig_validators.setdefault(int(v["action_id"]), set()).add(v["validator"])

pending = [c for c in load("claims.json") if c["status"] == "pending"]

# members with verify permission, per community symbol (from local chain)
verify_members = {}


def members_with_verify(cmm):
    sym = cmm["symbol"]
    if sym not in verify_members:
        scope = sym_scope(sym)
        roles = {r["name"]: set(r["permissions"]) for r in table(scope, "role")}
        verify_members[sym] = {
            m["name"] for m in table(scope, "member")
            if any("verify" in roles.get(rl, set()) for rl in m["roles"])}
    return verify_members[sym]


local_actions = {int(r["id"]): r for r in table(CM, "action")}
local_claim_ids = {int(r["id"]) for r in table(CM, "claim")}
checked_claims = {int(ch["claim_id"]) for ch in table(CM, "check")}
validator_cache = {}

for c in pending:
    cid, aid = int(c["id"]), int(c["action_id"])
    if cid not in local_claim_ids or cid in checked_claims:
        continue
    row = actions.get(aid)
    if not row:
        continue
    cmm = obj_community.get(int(row["objective_id"]))
    if not cmm:
        continue
    la = local_actions.get(aid)
    if not la:
        continue
    if int(la["is_completed"]) or int(la["verifications"]) < 3:
        continue
    if int(la["usages"]) > 0 and int(la["usages_left"]) < 1:
        continue
    if aid not in validator_cache:
        validator_cache[aid] = {v["validator"] for v in table(aid, "validator")}
    local_vals = validator_cache[aid]
    verifiers = members_with_verify(cmm)
    if MODE == "explicit":
        cand = sorted(local_vals & verifiers)
        if cand:
            print(f"{cid} {cand[0]} {cmm['symbol']}")
            break
    else:  # rolebased
        if local_vals or not orig_validators.get(aid):
            continue
        cand = sorted(verifiers - orig_validators[aid] - {c["claimer"]})
        if cand:
            print(f"{cid} {cand[0]} {cmm['symbol']}")
            break
