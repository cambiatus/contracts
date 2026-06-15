#!/usr/bin/env python3
"""Generate ordered transaction batches that rebuild prod chain state locally.

Reads the scraped table JSON in this directory, emits pNN_*.jsonl files
(one JSON transaction per line, cleos `push transaction` format) plus
manifest.json recording every fidelity deviation.
"""
import json
import hashlib
import time
import glob
import os
import re
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
CM = "cambiatus.cm"
TK = "cambiatus.tk"
BACKEND = "cambiatus"
DEV_PUB = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
GEN_NOW = int(time.time())
FUTURE_DEADLINE = GEN_NOW + 365 * 24 * 3600

manifest = defaultdict(list)


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


def act(account, name, actor, data):
    return {
        "account": account,
        "name": name,
        "authorization": [{"actor": actor, "permission": "active"}],
        "data": data,
    }


def setindices(sale, obj, action, claim):
    return act(CM, "setindices", CM, {
        "sale_id": sale, "objective_id": obj,
        "action_id": action, "claim_id": claim,
    })


def write_phase(fname, txs):
    path = os.path.join(HERE, fname)
    with open(path, "w") as f:
        for label, actions in txs:
            f.write(json.dumps({"label": label, "tx": {"actions": actions}}) + "\n")
    print(f"{fname}: {len(txs)} txs")


def chunk(items, n):
    for i in range(0, len(items), n):
        yield items[i:i + n]


communities = load("communities.json")
indexes = load("indexes.json")[0]
actions_rows = sorted(load("actions.json"), key=lambda r: int(r["id"]))
claims_rows = load("claims.json")
checks_rows = load("checks.json")
validators_rows = load("validators.json")

members = {}     # code -> {name: row}
roles = {}       # code -> [row]
objectives = {}  # code -> [row]
stats = {}       # code -> row|None
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    members[code] = {r["name"]: r for r in load(f"member_{code}.json")}
    roles[code] = load(f"role_{code}.json")
    objectives[code] = sorted(load(f"objective_{code}.json"), key=lambda r: int(r["id"]))
    st = load(f"stat_{code}.json")
    stats[code] = st[0] if st else None

# objective id -> community code; action id -> row
obj_community = {}
for code, objs in objectives.items():
    for o in objs:
        obj_community[int(o["id"])] = code
action_by_id = {int(r["id"]): r for r in actions_rows}
validators_by_action = defaultdict(list)
for v in validators_rows:
    validators_by_action[int(v["action_id"])].append(v["validator"])

# ── p1: accounts ──────────────────────────────────────────────────────────────
existing = {CM, TK, BACKEND, "eosio"}
all_accounts = set()
for code in members:
    all_accounts.update(members[code].keys())
all_accounts -= existing
NAME_RE = re.compile(r"^[a-z1-5.]{1,12}$")
bad = [a for a in sorted(all_accounts) if not NAME_RE.match(a)]
for a in bad:
    manifest["invalid_account_names"].append(a)
    all_accounts.discard(a)

auth_obj = {"threshold": 1, "keys": [{"key": DEV_PUB, "weight": 1}],
            "accounts": [], "waits": []}
txs = []
for batch in chunk(sorted(all_accounts), 100):
    actions = [act("eosio", "newaccount", "eosio", {
        "creator": "eosio", "name": a,
        "owner": auth_obj, "active": auth_obj,
    }) for a in batch]
    txs.append((f"accounts {batch[0]}..{batch[-1]}", actions))
write_phase("p1_accounts.jsonl", txs)

# ── p2: communities + tokens ─────────────────────────────────────────────────
txs = []
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    prec = cmm["symbol"].split(",")[0]
    creator = cmm["creator"]
    zero = f"0 {code}" if prec == "0" else f"0.{'0'*int(prec)} {code}"
    actions = [act(CM, "create", creator, {
        "cmm_asset": zero,
        "creator": creator,
        "logo": cmm["logo"],
        "name": cmm["name"],
        "description": cmm["description"],
        "inviter_reward": cmm["inviter_reward"],
        "invited_reward": cmm["invited_reward"],
        "has_objectives": cmm["has_objectives"],
        "has_shop": cmm["has_shop"],
        "has_kyc": cmm["has_kyc"],
        "auto_invite": 0,
        "subdomain": "",
        "website": "",
    })]
    st = stats[code]
    if st:
        actions.append(act(TK, "create", st["issuer"], {
            "issuer": st["issuer"],
            "max_supply": st["max_supply"],
            "min_balance": st["min_balance"],
            "type": st["type"],
        }))
    else:
        manifest["communities_without_token"].append(code)
    txs.append((f"community {code}", actions))
write_phase("p2_communities.jsonl", txs)

# ── p3: netlink (BFS by invite tree) ─────────────────────────────────────────
txs = []
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    creator = cmm["creator"]
    sym = cmm["symbol"]
    placed = {creator}
    remaining = {n: r for n, r in members[code].items()
                 if n != creator and n in all_accounts | existing}
    ordered = []
    while remaining:
        progressed = [r for n, r in remaining.items() if r["inviter"] in placed]
        if not progressed:
            # orphan invite chains: reattach to creator
            for r in remaining.values():
                manifest["netlink_inviter_rewritten"].append(
                    {"community": code, "member": r["name"], "orig_inviter": r["inviter"]})
                ordered.append({**r, "inviter": creator})
                placed.add(r["name"])
            break
        for r in sorted(progressed, key=lambda r: r["name"]):
            ordered.append(r)
            placed.add(r["name"])
            del remaining[r["name"]]
    for batch in chunk(ordered, 50):
        actions = [act(CM, "netlink", BACKEND, {
            "community_id": sym,
            "inviter": r["inviter"],
            "new_user": r["name"],
            "user_type": r["user_type"] if r["user_type"] in ("natural", "juridical") else "natural",
        }) for r in batch]
        txs.append((f"netlink {code} {batch[0]['name']}..", actions))
write_phase("p3_netlink.jsonl", txs)

# ── p4: roles + assignments ──────────────────────────────────────────────────
txs = []
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    sym = cmm["symbol"]
    creator = cmm["creator"]
    role_names = {r["name"] for r in roles[code]}
    actions = [act(CM, "upsertrole", CM, {
        "community_id": sym,
        "name": r["name"],
        "color": "#000000",
        "permissions": r["permissions"],
    }) for r in roles[code]]
    if actions:
        txs.append((f"roles {code}", actions))
    assigns = []
    for n, r in members[code].items():
        if n not in all_accounts | {creator}:
            continue
        rl = [x for x in r["roles"] if x in role_names]
        if rl != r["roles"]:
            manifest["member_unknown_roles_dropped"].append(
                {"community": code, "member": n, "orig": r["roles"]})
        if rl != ["member"]:
            assigns.append(act(CM, "assignroles", creator, {
                "community_id": sym, "member": n, "roles": rl or ["member"],
            }))
    for batch in chunk(assigns, 50):
        txs.append((f"assignroles {code}", batch))
write_phase("p4_roles.jsonl", txs)

# ── p5: objectives (id-forced) ───────────────────────────────────────────────
txs = []
last_obj = 0
for cmm in communities:
    code = cmm["symbol"].split(",")[1]
    if not objectives[code]:
        continue
    if not cmm["has_objectives"]:
        manifest["objectives_skipped_no_flag"].append(code)
        continue
    sym = cmm["symbol"]
    pairs = []
    for o in objectives[code]:
        oid = int(o["id"])
        editor = o["creator"] if o["creator"] in members[code] else cmm["creator"]
        if editor != o["creator"]:
            manifest["objective_editor_substituted"].append(
                {"objective": oid, "orig": o["creator"], "used": editor})
        pairs.append(setindices(0, oid - 1, 0, 0))
        pairs.append(act(CM, "upsertobjctv", editor, {
            "community_id": sym, "objective_id": 0,
            "description": o["description"], "editor": editor,
        }))
        last_obj = max(last_obj, oid)
    for batch in chunk(pairs, 50):  # 25 objective creations per tx
        txs.append((f"objectives {code}", batch))
write_phase("p5_objectives.jsonl", txs)

# ── p6: actions create (full usages, not completed, deadlines clamped) ──────
pending_claims = sorted(
    (c for c in claims_rows if c["status"] == "pending"),
    key=lambda c: int(c["id"]))
pending_ids = {int(c["id"]) for c in pending_claims}
checks_by_claim = defaultdict(list)
for ch in checks_rows:
    checks_by_claim[int(ch["claim_id"])].append(ch)

txs = []
replayable_actions = set()
last_act = 0
for row in actions_rows:
    aid = int(row["id"])
    code = obj_community.get(int(row["objective_id"]))
    if code is None:
        manifest["actions_skipped_orphan_objective"].append(aid)
        continue
    cmm = next(c for c in communities if c["symbol"].endswith("," + code))
    if not cmm["has_objectives"]:
        manifest["actions_skipped_no_objectives_flag"].append(aid)
        continue
    sym = cmm["symbol"]
    creator = row["creator"] if row["creator"] in members[code] else cmm["creator"]
    if creator != row["creator"]:
        manifest["action_creator_substituted"].append(
            {"action": aid, "orig": row["creator"], "used": creator})
    deadline = int(row["deadline"])
    if deadline > 0 and deadline <= GEN_NOW:
        manifest["action_deadline_clamped"].append(aid)
        deadline = FUTURE_DEADLINE
    vals = [v for v in validators_by_action.get(aid, []) if v in members[code]]
    dropped = set(validators_by_action.get(aid, [])) - set(vals)
    if dropped:
        manifest["action_validators_dropped_nonmember"].append(
            {"action": aid, "dropped": sorted(dropped)})
    verifications = int(row["verifications"])
    if row["verification_type"] == "claimable" and vals and (
            len(vals) < 2 or len(vals) < verifications):
        manifest["action_switched_rolebased_too_few_validators"].append(aid)
        vals = []
    actions = [
        setindices(0, last_obj, aid - 1, 0),
        act(CM, "upsertaction", creator, {
            "community_id": sym,
            "action_id": 0,
            "objective_id": int(row["objective_id"]),
            "description": row["description"],
            "reward": row["reward"],
            "verifier_reward": row["verifier_reward"],
            "deadline": deadline,
            "usages": int(row["usages"]),
            "usages_left": int(row["usages"]),
            "verifications": verifications,
            "verification_type": row["verification_type"],
            "validators_str": "-".join(vals),
            "is_completed": 0,
            "creator": creator,
            "has_proof_photo": row["has_proof_photo"],
            "has_proof_code": row["has_proof_code"],
            "photo_proof_instructions": row["photo_proof_instructions"],
            "image": "",
        }),
    ]
    replayable_actions.add(aid)
    last_act = max(last_act, aid)
    txs.append((f"action {aid} ({code})", actions))
txs = [(f"{b[0][0]} +{len(b)-1} more", [a for _, acts in b for a in acts])
       for b in chunk(txs, 10)]
write_phase("p6_actions.jsonl", txs)

# ── p7: pending claims (id-forced; proof codes regenerated) ─────────────────
txs = []
last_claim = 0
replayed_claims = set()
for c in pending_claims:
    cid = int(c["id"])
    aid = int(c["action_id"])
    if aid not in replayable_actions:
        manifest["claims_skipped_action_not_replayed"].append(cid)
        continue
    row = action_by_id[aid]
    code = obj_community[int(row["objective_id"])]
    cmm = next(x for x in communities if x["symbol"].endswith("," + code))
    if c["claimer"] not in members[code]:
        manifest["claims_skipped_claimer_not_member"].append(cid)
        continue
    if row["verification_type"] != "claimable":
        manifest["claims_skipped_action_not_claimable"].append(cid)
        continue
    proof_photo = c["proof_photo"]
    if int(row["has_proof_photo"]) and not proof_photo:
        proof_photo = "https://cambiatus.io/replay-placeholder.jpg"
        manifest["claim_proof_photo_placeholder"].append(cid)
    proof_code, proof_time = "", 0
    if int(row["has_proof_code"]):
        proof_time = GEN_NOW
        proof = f"{aid}{name_to_uint64(c['claimer'])}{proof_time}"
        proof_code = hashlib.sha256(proof.encode()).hexdigest()[:8]
        manifest["claim_proof_code_regenerated"].append(cid)
    actions = [
        setindices(0, last_obj, last_act, cid - 1),
        act(CM, "claimaction", c["claimer"], {
            "community_id": cmm["symbol"],
            "action_id": aid,
            "maker": c["claimer"],
            "proof_photo": proof_photo,
            "proof_code": proof_code,
            "proof_time": proof_time,
        }),
    ]
    replayed_claims.add(cid)
    last_claim = max(last_claim, cid)
    txs.append((f"claim {cid} on action {aid}", actions))
txs = [(f"{b[0][0]} +{len(b)-1} more", [a for _, acts in b for a in acts])
       for b in chunk(txs, 10)]
write_phase("p7_claims.jsonl", txs)

# ── p8: checks on pending claims ─────────────────────────────────────────────
txs = []
batch = []
for cid in sorted(replayed_claims):
    aid = int(next(c for c in pending_claims if int(c["id"]) == cid)["action_id"])
    row = action_by_id[aid]
    code = obj_community[int(row["objective_id"])]
    cmm = next(x for x in communities if x["symbol"].endswith("," + code))
    for ch in checks_by_claim.get(cid, []):
        if ch["validator"] not in members[code]:
            manifest["checks_skipped_validator_not_member"].append(
                {"claim": cid, "validator": ch["validator"]})
            continue
        batch.append(act(CM, "verifyclaim", ch["validator"], {
            "community_id": cmm["symbol"],
            "claim_id": cid,
            "verifier": ch["validator"],
            "vote": int(ch["is_verified"]),
        }))
for b in chunk(batch, 25):
    txs.append((f"checks {b[0]['data']['claim_id']}..", b))
write_phase("p8_checks.jsonl", txs)

# ── p9: action patches (usages_left / is_completed to prod values) ──────────
txs = []
for row in actions_rows:
    aid = int(row["id"])
    if aid not in replayable_actions:
        continue
    if int(row["usages_left"]) == int(row["usages"]) and not int(row["is_completed"]):
        continue
    code = obj_community[int(row["objective_id"])]
    cmm = next(x for x in communities if x["symbol"].endswith("," + code))
    creator = row["creator"] if row["creator"] in members[code] else cmm["creator"]
    deadline = int(row["deadline"])
    if deadline > 0 and deadline <= GEN_NOW:
        deadline = FUTURE_DEADLINE
    vals = [v for v in validators_by_action.get(aid, []) if v in members[code]]
    verifications = int(row["verifications"])
    if row["verification_type"] == "claimable" and vals and (
            len(vals) < 2 or len(vals) < verifications):
        vals = []
    txs.append((f"patch action {aid}", [act(CM, "upsertaction", creator, {
        "community_id": cmm["symbol"],
        "action_id": aid,
        "objective_id": int(row["objective_id"]),
        "description": row["description"],
        "reward": row["reward"],
        "verifier_reward": row["verifier_reward"],
        "deadline": deadline,
        "usages": int(row["usages"]),
        "usages_left": int(row["usages_left"]),
        "verifications": verifications,
        "verification_type": row["verification_type"],
        "validators_str": "-".join(vals),
        "is_completed": int(row["is_completed"]),
        "creator": creator,
        "has_proof_photo": row["has_proof_photo"],
        "has_proof_code": row["has_proof_code"],
        "photo_proof_instructions": row["photo_proof_instructions"],
        "image": "",
    })]))
write_phase("p9_action_patches.jsonl", txs)

# ── p10: final indexes to prod values ────────────────────────────────────────
write_phase("p10_indexes.jsonl", [("final indexes", [setindices(
    int(indexes["last_used_sale_id"]),
    int(indexes["last_used_objective_id"]),
    int(indexes["last_used_action_id"]),
    int(indexes["last_used_claim_id"]),
)])])

with open(os.path.join(HERE, "manifest.json"), "w") as f:
    json.dump({k: v for k, v in manifest.items()}, f, indent=1)
print("manifest:", {k: len(v) for k, v in manifest.items()})
print(f"members total: {sum(len(m) for m in members.values())}, "
      f"accounts to create: {len(all_accounts)}, "
      f"pending claims replayed: {len(replayed_claims)}")
