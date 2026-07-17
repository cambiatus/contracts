#!/usr/bin/env bash
# Community contract integration tests.
# Requires a running bootstrapped local chain (make bootstrap).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_ROOT/tests/harness.sh"

wallet_unlock

TST_SCOPE=$(sym_scope TST)

# ── Community CRUD ────────────────────────────────────────────────────────────

suite "Community: create"

assert_ok "create a second community" \
    $CLEOS push action "$CMM_CONTRACT" create \
    '["0 AAA", "alice", "", "Alpha Community", "desc", "0 AAA", "0 AAA", 0, 0, 0, 0, "alpha", ""]' \
    -p alice

assert_table "TST community row exists" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" community \
    '.rows[] | select(.symbol == "0,TST") | .name' \
    "Test Community"

assert_fail "reject duplicate symbol" "symbol already exists" \
    $CLEOS push action "$CMM_CONTRACT" create \
    '["0 TST", "alice", "", "Dup", "dup", "0 TST", "0 TST", 0, 0, 0, 0, "dup", ""]' \
    -p alice

assert_fail "reject mismatched reward symbol" "unmatched symbols" \
    $CLEOS push action "$CMM_CONTRACT" create \
    '["0 XYZ", "alice", "", "Test", "desc", "0 AAA", "0 XYZ", 0, 0, 0, 0, "xyz", ""]' \
    -p alice

suite "Community: update"

assert_ok "creator can update community" \
    $CLEOS push action "$CMM_CONTRACT" update \
    '["0 TST", "", "Test Community Updated", "new desc", "1 TST", "2 TST", 1, 0, 0, 0, "test", ""]' \
    -p alice

assert_table "name was updated" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" community \
    '.rows[] | select(.symbol == "0,TST") | .name' \
    "Test Community Updated"

assert_fail "non-creator cannot update" "missing authority" \
    $CLEOS push action "$CMM_CONTRACT" update \
    '["0 TST", "", "Hijack", "x", "1 TST", "2 TST", 1, 0, 0, 0, "test", ""]' \
    -p bob

# ── Membership ────────────────────────────────────────────────────────────────

suite "Community: membership"

# member table is scoped by symbol.raw()
assert_table "alice is a member" \
    "$CMM_CONTRACT" "$TST_SCOPE" member \
    '.rows[] | select(.name == "alice") | .name' \
    "alice"

assert_table "bob is a member" \
    "$CMM_CONTRACT" "$TST_SCOPE" member \
    '.rows[] | select(.name == "bob") | .name' \
    "bob"

assert_fail "cannot invite non-existent account" "new user account doesn't exists" \
    $CLEOS push action "$CMM_CONTRACT" netlink \
    '["0,TST", "alice", "nosuchacct1", "natural"]' \
    -p alice

assert_fail "non-member inviter rejected" "inviter is not part of the community" \
    $CLEOS push action "$CMM_CONTRACT" netlink \
    '["0,TST", "cambiatus", "eosio", "natural"]' \
    -p cambiatus

# ── Roles ─────────────────────────────────────────────────────────────────────
# upsertrole/assignroles accept the community creator, the contract itself
# (cambiatus.cm), or any member holding a role with the "admin" permission.

suite "Community: roles"

assert_ok "create validator role (requires contract auth)" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "validator", "color": "#ff0000", "permissions": ["verify"]}' \
    -p "$CMM_CONTRACT"

assert_ok "create admin role with award permission" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "admin", "color": "#0000ff", "permissions": ["award"]}' \
    -p "$CMM_CONTRACT"

# role table is scoped by symbol.raw()
assert_table "validator role stored" \
    "$CMM_CONTRACT" "$TST_SCOPE" role \
    '.rows[] | select(.name == "validator") | .permissions[0]' \
    "verify"

# assignroles: signed by community creator (alice)
assert_ok "assign validator role to carol" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "carol", "roles": ["member", "validator"]}' \
    -p alice

assert_ok "assign validator role to dave" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "dave", "roles": ["member", "validator"]}' \
    -p alice

assert_ok "assign validator role to eve" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "eve", "roles": ["member", "validator"]}' \
    -p alice

assert_ok "assign admin role to alice (for reward tests)" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "alice", "roles": ["member", "admin"]}' \
    -p alice

assert_fail "cannot assign non-existent role" "this role doesn't exist" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["nonexistent"]}' \
    -p alice

# ── Roles: admin permission ───────────────────────────────────────────────────
# Members holding a role with the "admin" permission can manage roles and
# assignments just like the creator. Non-admin members are rejected.

assert_ok "creator can upsertrole directly" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "manager", "color": "#00ff00", "permissions": ["admin"]}' \
    -p alice

assert_fail "non-admin member cannot upsertrole" "only the community creator or an admin can manage roles" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "rogue", "color": "#123456", "permissions": ["invite"]}' \
    -p bob

assert_fail "non-admin member cannot assignroles" "only the community creator or an admin can assign roles" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "carol", "roles": ["member"]}' \
    -p bob

assert_ok "grant manager (admin) role to bob" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["member", "manager"]}' \
    -p alice

assert_ok "admin-role holder can upsertrole" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "greeter", "color": "#abcdef", "permissions": ["invite"]}' \
    -p bob

assert_ok "admin-role holder can assignroles" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "carol", "roles": ["member", "validator"]}' \
    -p bob

# restore bob to plain member so later permission tests start from known state
assert_ok "restore bob to member role" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["member"]}' \
    -p alice

# ── Objectives ───────────────────────────────────────────────────────────────

suite "Community: objectives"

assert_ok "alice creates an objective" \
    $CLEOS push action "$CMM_CONTRACT" upsertobjctv \
    '["0,TST", 0, "Reduce plastic waste", "alice"]' \
    -p alice

# objective table is scoped by symbol.raw()
OBJ_ID=$(get_table "$CMM_CONTRACT" "$TST_SCOPE" objective '.rows[-1].id')
echo "    objective id: $OBJ_ID"

assert_table "objective stored with correct description" \
    "$CMM_CONTRACT" "$TST_SCOPE" objective \
    ".rows[] | select(.id == $OBJ_ID) | .description" \
    "Reduce plastic waste"

assert_ok "alice can edit her objective" \
    $CLEOS push action "$CMM_CONTRACT" upsertobjctv \
    "[\"0,TST\", $OBJ_ID, \"Reduce plastic and glass waste\", \"alice\"]" \
    -p alice

assert_fail "non-editor cannot edit objective" "You must be either the creator" \
    $CLEOS push action "$CMM_CONTRACT" upsertobjctv \
    "[\"0,TST\", $OBJ_ID, \"Evil edit\", \"bob\"]" \
    -p bob

# ── Actions ──────────────────────────────────────────────────────────────────
# verifications must be odd and >= 3 (contract constraint).
# validators_str: if non-empty, explicit list (>= verifications entries).
# validators_str: if empty, role-based mode — any member with verify permission can validate.

suite "Community: actions"

# 3 explicit validators (carol, dave, eve), 3 verifications required (odd, >=3)
assert_ok "create claimable action with explicit validators" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Pick up litter\", \"5 TST\", \"1 TST\", 0, 10, 10, 3, \"claimable\", \"carol-dave-eve\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')
echo "    action id: $ACT_ID"

assert_table "action stored with correct description" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" action \
    ".rows[] | select(.id == $ACT_ID) | .description" \
    "Pick up litter"

# Role-based claimable action: empty validators_str, any member with verify role can validate
assert_ok "create claimable action with role-based validators (empty validators_str)" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Pick up glass\", \"4 TST\", \"1 TST\", 0, 10, 10, 3, \"claimable\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

ROLE_ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')
echo "    role-based action id: $ROLE_ACT_ID"

assert_ok "create automatic action" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Plant a tree\", \"3 TST\", \"0 TST\", 0, 5, 5, 0, \"automatic\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

AUTO_ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')
echo "    auto action id: $AUTO_ACT_ID"

assert_fail "verifications must be odd and >= 3" "You need at least three validators" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Bad action\", \"1 TST\", \"0 TST\", 0, 0, 0, 2, \"claimable\", \"carol-dave\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

# ── Action management auth (creator OR community admin) ───────────────────────
# upsertaction now accepts the action creator OR any member holding the admin
# permission — the same model as upsertrole/assignroles. This is what lets a
# community admin migrate an action's explicit validators to role-based without
# the original creator's key (the prod backfill scenario).

suite "Community: action admin auth"

# Dedicated action so the downstream claim tests are unaffected. Created by
# alice with explicit validators; a non-creator admin will migrate it.
assert_ok "alice creates action to be admin-migrated" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Admin migrate target\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"carol-dave-eve\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

MIG_ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')
echo "    admin-migrate action id: $MIG_ACT_ID"

assert_table "target action starts with 3 explicit validators" \
    "$CMM_CONTRACT" "$MIG_ACT_ID" validator '.rows | length' "3"

# eve is a plain member (no admin permission) and not the creator → rejected.
assert_fail "non-admin non-creator cannot manage action" "only the action creator or a community admin can manage actions" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", $MIG_ACT_ID, $OBJ_ID, \"Admin migrate target\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p eve

# Grant bob the admin-permission "manager" role created earlier in the suite.
assert_ok "grant manager (admin) role to bob for action migration" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["member", "manager"]}' \
    -p alice

# Admin bob (NOT the creator) migrates the action to role-based by passing an
# empty validators_str. The creator field stays alice (the modify path never
# rewrites it) — bob never needed alice's key.
assert_ok "community admin migrates action to role-based" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", $MIG_ACT_ID, $OBJ_ID, \"Admin migrate target\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p bob

assert_table "explicit validators cleared after admin migration" \
    "$CMM_CONTRACT" "$MIG_ACT_ID" validator '.rows | length' "0"

assert_table "action creator unchanged after admin edit" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" action ".rows[] | select(.id == $MIG_ACT_ID) | .creator" "alice"

# Regression: the creator can still manage their own action directly.
assert_ok "creator can still manage own action" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", $MIG_ACT_ID, $OBJ_ID, \"Admin migrate target v2\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

# Restore bob to plain member so downstream tests start from a known state.
assert_ok "restore bob to member role (post action-auth)" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["member"]}' \
    -p alice

# ── Claim flow (approve) ──────────────────────────────────────────────────────
# Majority = ceil(3/2) = 2. Two approvals resolve the claim as approved.

suite "Community: claim approve flow"

assert_ok "bob claims the action" \
    $CLEOS push action "$CMM_CONTRACT" claimaction \
    "[\"0,TST\", $ACT_ID, \"bob\", \"\", \"\", 0]" \
    -p bob

CLAIM_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" claim '.rows[-1].id')
echo "    claim id: $CLAIM_ID"

assert_table "claim starts as pending" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "bob") | .status' \
    "pending"

assert_fail "non-validator cannot verify" "Verifier is not in the action validator list" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM_ID, \"bob\", 1]" \
    -p bob

assert_ok "carol approves (1 of 3 — still pending)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM_ID, \"carol\", 1]" \
    -p carol

assert_table "one approval — still pending" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "bob") | .status' \
    "pending"

assert_ok "dave approves (2 of 3 — majority reached, claim approved)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM_ID, \"dave\", 1]" \
    -p dave

assert_table "two approvals — claim approved" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "bob") | .status' \
    "approved"

assert_fail "cannot vote on resolved claim" "Can't vote on already verified claim" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM_ID, \"eve\", 1]" \
    -p eve

# ── Claim flow (reject) ───────────────────────────────────────────────────────

suite "Community: claim reject flow"

assert_ok "eve claims the action" \
    $CLEOS push action "$CMM_CONTRACT" claimaction \
    "[\"0,TST\", $ACT_ID, \"eve\", \"\", \"\", 0]" \
    -p eve

CLAIM2_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" claim '.rows[-1].id')

assert_ok "carol rejects (1 of 3)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM2_ID, \"carol\", 0]" \
    -p carol

assert_ok "dave rejects (2 of 3 — majority, claim rejected)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $CLAIM2_ID, \"dave\", 0]" \
    -p dave

assert_table "two rejections — claim rejected" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "eve") | .status' \
    "rejected"

# ── Claim flow (role-based validators) ───────────────────────────────────────
# ROLE_ACT_ID has empty validators_str — any member with verify permission can validate.
# The default "member" role includes verify, so all TST members can verify.
# Non-members are rejected via has_permission's membership check.

suite "Community: claim flow (role-based validators)"

assert_ok "alice claims the role-based action" \
    $CLEOS push action "$CMM_CONTRACT" claimaction \
    "[\"0,TST\", $ROLE_ACT_ID, \"alice\", \"\", \"\", 0]" \
    -p alice

assert_table "role-based claim starts as pending" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "alice") | .status' \
    "pending"

# Non-members are blocked by the has_permission membership check
ROLE_CLAIM_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" claim '.rows[-1].id')
assert_fail "non-member cannot validate role-based claim" "user is not part of the communtiy" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $ROLE_CLAIM_ID, \"cambiatus\", 1]" \
    -p cambiatus

assert_ok "carol approves role-based claim (1 of 3)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $ROLE_CLAIM_ID, \"carol\", 1]" \
    -p carol

assert_table "one approval — still pending" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "alice") | .status' \
    "pending"

assert_ok "dave approves role-based claim (2 of 3 — majority, approved)" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $ROLE_CLAIM_ID, \"dave\", 1]" \
    -p dave

assert_table "role-based claim approved" \
    "$CMM_CONTRACT" "$CMM_CONTRACT" claim \
    '.rows[] | select(.claimer == "alice") | .status' \
    "approved"

# ── Verify permission enforcement (test matrix rows 3 and 5) ─────────────────
# Row 3: in explicit list but lacks verify role → rejected "no role"
# Row 5: role-based action (empty list), member lacks verify role → rejected "no role"
#
# Setup: create "restricted" role (no verify), strip bob to only that role.
# Teardown: restore bob to ["member"] so automatic reward test works.

suite "Community: verify permission enforcement"

assert_ok "create restricted role (no verify permission)" \
    $CLEOS push action "$CMM_CONTRACT" upsertrole \
    '{"community_id": "0,TST", "name": "restricted", "color": "#888888", "permissions": ["claim"]}' \
    -p "$CMM_CONTRACT"

assert_ok "assign bob restricted role only (removes verify)" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["restricted"]}' \
    -p alice

# Row 3: bob is in the explicit validator list but lacks verify role
assert_ok "create action with bob in explicit validator list" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Verify perm test\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"bob-carol-dave\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

PERM_ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')

assert_ok "eve claims the verify-perm-test action" \
    $CLEOS push action "$CMM_CONTRACT" claimaction \
    "[\"0,TST\", $PERM_ACT_ID, \"eve\", \"\", \"\", 0]" \
    -p eve

PERM_CLAIM_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" claim '.rows[-1].id')

# bob IS in the list — passes list check, fails permission check
assert_fail "in-list validator without verify role is rejected" "you cannot verify with your current roles" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $PERM_CLAIM_ID, \"bob\", 1]" \
    -p bob

# Row 5: role-based action, bob lacks verify role
assert_ok "create role-based action for perm test" \
    $CLEOS push action "$CMM_CONTRACT" upsertaction \
    "[\"0,TST\", 0, $OBJ_ID, \"Role perm test\", \"1 TST\", \"0 TST\", 0, 5, 5, 3, \"claimable\", \"\", 0, \"alice\", 0, 0, \"\", \"\"]" \
    -p alice

ROLE_PERM_ACT_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" action '.rows[-1].id')

assert_ok "carol claims the role-perm-test action" \
    $CLEOS push action "$CMM_CONTRACT" claimaction \
    "[\"0,TST\", $ROLE_PERM_ACT_ID, \"carol\", \"\", \"\", 0]" \
    -p carol

ROLE_PERM_CLAIM_ID=$(get_table "$CMM_CONTRACT" "$CMM_CONTRACT" claim '.rows[-1].id')

assert_fail "member without verify role cannot verify role-based claim" "you cannot verify with your current roles" \
    $CLEOS push action "$CMM_CONTRACT" verifyclaim \
    "[\"0,TST\", $ROLE_PERM_CLAIM_ID, \"bob\", 1]" \
    -p bob

# Restore bob to default member role so automatic reward test works
assert_ok "restore bob to member role" \
    $CLEOS push action "$CMM_CONTRACT" assignroles \
    '{"community_id": "0,TST", "member": "bob", "roles": ["member"]}' \
    -p alice

# ── Automatic reward ──────────────────────────────────────────────────────────
# alice has the "admin" role with "award" permission (assigned above)

suite "Community: automatic reward"

assert_ok "alice awards automatic action to bob" \
    $CLEOS push action "$CMM_CONTRACT" reward \
    "[\"0,TST\", $AUTO_ACT_ID, \"bob\", \"alice\", \"reward memo\"]" \
    -p alice

# The reward action's inline token issue must retain the caller-supplied memo.
REWARD_TX=$($CLEOS push action "$CMM_CONTRACT" reward \
    "[\"0,TST\", $AUTO_ACT_ID, \"bob\", \"alice\", \"second reward memo\"]" \
    -p alice -j | jq -r '.transaction_id')
REWARD_ISSUE_MEMO=$($CLEOS get transaction "$REWARD_TX" \
    | jq -r '.. | objects | select(.act?.name? == "issue") | .act.data.memo' \
    | head -n 1)
if [ "$REWARD_ISSUE_MEMO" = "second reward memo" ]; then
    _pass "inline issue preserves reward memo"
else
    _fail "inline issue preserves reward memo" \
        "expected: 'second reward memo'" "got:      '$REWARD_ISSUE_MEMO'"
fi

assert_fail "reward memo over 256 bytes is rejected" "memo has more than 256 bytes" \
    $CLEOS push action "$CMM_CONTRACT" reward \
    "[\"0,TST\", $AUTO_ACT_ID, \"bob\", \"alice\", \"$(printf 'x%.0s' {1..257})\"]" \
    -p alice

assert_fail "non-member cannot award" "verifier doesn't belong to the community" \
    $CLEOS push action "$CMM_CONTRACT" reward \
    "[\"0,TST\", $AUTO_ACT_ID, \"bob\", \"cambiatus\", \"\"]" \
    -p cambiatus

assert_fail "member without award permission cannot award" "you cannot award with your current roles" \
    $CLEOS push action "$CMM_CONTRACT" reward \
    "[\"0,TST\", $AUTO_ACT_ID, \"bob\", \"bob\", \"\"]" \
    -p bob

summary
