#!/usr/bin/env bash
# Validate the contract upgrade against prod-cloned local state.
# Phases: pre-dump → upgrade → post-dump diff → behavior checks.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="/Users/lucca/Development/cpp/cambiatus/contracts"
NODE_URL="http://127.0.0.1:8888"
CLEOS="cleos -u $NODE_URL"
CM="cambiatus.cm"

PASS=0
FAIL=0
ok()   { PASS=$((PASS+1)); echo "  ✓ $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  ✗ $1"; }
check_ok()   { local d="$1"; shift; if "$@" > /dev/null 2>&1; then ok "$d"; else bad "$d"; fi; }
check_fail() { local d="$1" msg="$2"; shift 2; local out; out=$("$@" 2>&1); if echo "$out" | grep -q "$msg"; then ok "$d"; else bad "$d (got: $(echo "$out" | tail -1))"; fi; }

sym_scope() {
    python3 -c "
prec, code = '$1'.split(',')
v = 0
for c in reversed(code):
    v = (v << 8) | ord(c)
print((v << 8) | int(prec))"
}

table_dump() { # <scope> <table> — full sorted dump to stdout
    local scope="$1" table="$2" lower="" page
    while :; do
        page=$(curl -s "$NODE_URL/v1/chain/get_table_rows" \
            -d "{\"json\":true,\"code\":\"$CM\",\"scope\":\"$scope\",\"table\":\"$table\",\"limit\":1000,\"lower_bound\":\"$lower\"}")
        echo "$page" | jq -c '.rows[]'
        [ "$(echo "$page" | jq -r '.more')" = "true" ] || break
        lower=$(echo "$page" | jq -r '.next_key')
    done
}

dump_state() { # <outfile-prefix>
    local pfx="$1"
    {
        table_dump "$CM" community
        table_dump "$CM" action
        table_dump "$CM" claim
        table_dump "$CM" check
        table_dump "$CM" indexes
        jq -r '.[].symbol' "$HERE/communities.json" | while read -r sym; do
            scope=$(sym_scope "$sym")
            echo "## $sym"
            table_dump "$scope" member
            table_dump "$scope" role
            table_dump "$scope" objective
        done
        while read -r vscope; do
            table_dump "$vscope" validator
        done < <($CLEOS get scope "$CM" --table validator --limit 1000 | jq -r '.rows[].scope')
    } > "$HERE/$pfx.dump"
    wc -l < "$HERE/$pfx.dump" | tr -d ' '
}

echo "=== pre-upgrade state dump ==="
PRE_ROWS=$(dump_state pre)
echo "  rows: $PRE_ROWS  sha256: $(shasum -a 256 "$HERE/pre.dump" | cut -c1-16)"

echo "=== UPGRADE: set contract to new build ==="
$CLEOS set abi  "$CM" "$REPO/community/community.abi"  -p "$CM" > /dev/null || { echo "set abi FAILED"; exit 1; }
$CLEOS set code "$CM" "$REPO/community/community.wasm" -p "$CM" > /dev/null || { echo "set code FAILED"; exit 1; }
echo "  new code hash: $($CLEOS get code "$CM" | awk '{print $3}')"

echo "=== post-upgrade state dump ==="
POST_ROWS=$(dump_state post)
echo "  rows: $POST_ROWS  sha256: $(shasum -a 256 "$HERE/post.dump" | cut -c1-16)"
if cmp -s "$HERE/pre.dump" "$HERE/post.dump"; then
    ok "table state byte-identical across upgrade ($PRE_ROWS rows)"
else
    bad "table state DIFFERS across upgrade"
    diff "$HERE/pre.dump" "$HERE/post.dump" | head -10
fi

echo "=== behavior: old flows still work ==="
# pick a small community for membership test
SYM="0,COFI"
SCOPE=$(sym_scope "$SYM")
CREATOR=$(jq -r '.[] | select(.symbol=="0,COFI") | .creator' "$HERE/communities.json")

$CLEOS create account eosio upgradetest1 \
    EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV \
    EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV > /dev/null 2>&1
check_ok "netlink new member (creator invite)" \
    $CLEOS push action "$CM" netlink "[\"$SYM\", \"$CREATOR\", \"upgradetest1\", \"natural\"]" -p "$CREATOR"
roles_now=$($CLEOS get table "$CM" "$SCOPE" member -L upgradetest1 -U upgradetest1 --limit 1 | jq -c '.rows[0].roles')
[ "$roles_now" = '["member"]' ] && ok "new member got default member role" || bad "default role wrong: $roles_now"

# pending claim with explicit validators: pick one where some listed validator hasn't voted
echo "--- explicit-validator verifyclaim on pre-existing pending claim ---"
PICK=$(python3 "$HERE/pick_claim.py" explicit)
if [ -n "$PICK" ]; then
    CLAIM_ID=$(echo "$PICK" | cut -d' ' -f1)
    VERIFIER=$(echo "$PICK" | cut -d' ' -f2)
    CSYM=$(echo "$PICK" | cut -d' ' -f3)
    check_ok "explicit validator verifies pending claim $CLAIM_ID" \
        $CLEOS push action "$CM" verifyclaim "[\"$CSYM\", $CLAIM_ID, \"$VERIFIER\", 1]" -p "$VERIFIER"
else
    bad "no suitable explicit-validator pending claim found"
fi

echo "=== behavior: new auth enforced ==="
MUDA_SCOPE=$(sym_scope "0,MUDA")
MUDA_CREATOR=$(jq -r '.[] | select(.symbol=="0,MUDA") | .creator' "$HERE/communities.json")
# a plain member of MUDA (no admin role)
PLAIN=$($CLEOS get table "$CM" "$MUDA_SCOPE" member --limit 50 | jq -r --arg c "$MUDA_CREATOR" '.rows[] | select(.name != $c and .roles == ["member"]) | .name' | head -1)
check_fail "non-admin member cannot assignroles" "only the community creator or an admin can assign roles" \
    $CLEOS push action "$CM" assignroles "{\"community_id\":\"0,MUDA\",\"member\":\"$PLAIN\",\"roles\":[\"member\"]}" -p "$PLAIN"
check_fail "non-admin member cannot upsertrole" "only the community creator or an admin can manage roles" \
    $CLEOS push action "$CM" upsertrole "{\"community_id\":\"0,MUDA\",\"name\":\"rogue\",\"color\":\"#123456\",\"permissions\":[\"invite\"]}" -p "$PLAIN"

echo "=== backfill rehearsal: admin role for every community ==="
while read -r sym; do
    code=${sym#*,}
    scope=$(sym_scope "$sym")
    creator=$(jq -r --arg s "$sym" '.[] | select(.symbol==$s) | .creator' "$HERE/communities.json")
    # second admin = first non-creator member
    second=$($CLEOS get table "$CM" "$scope" member --limit 20 | jq -r --arg c "$creator" '.rows[] | select(.name != $c) | .name' | head -1)
    targets="$creator"
    [ -n "$second" ] && targets="$creator,$second"
    if "$REPO/scripts/backfill_roles.sh" -u "$NODE_URL" -s "$sym" -p "$creator" -m "$targets" > "$HERE/.bf_$code.log" 2>&1; then
        ok "backfill_roles $code (admin → $targets)"
    else
        bad "backfill_roles $code: $(tail -1 "$HERE/.bf_$code.log")"
    fi
done < <(jq -r '.[].symbol' "$HERE/communities.json")

echo "=== CPU worst case: admin (non-creator) manages roles on MUDA (6893 members) ==="
MUDA_SECOND=$($CLEOS get table "$CM" "$MUDA_SCOPE" member --limit 20 | jq -r --arg c "$MUDA_CREATOR" '.rows[] | select(.name != $c) | .name' | head -1)
out=$($CLEOS push action "$CM" upsertrole "{\"community_id\":\"0,MUDA\",\"name\":\"cputest\",\"color\":\"#ffffff\",\"permissions\":[\"invite\"]}" -p "$MUDA_SECOND" 2>&1)
if echo "$out" | grep -q "executed transaction"; then
    us=$(echo "$out" | grep -o '[0-9]* us' | head -1)
    ok "admin upsertrole on MUDA executed in $us (worst-case member scan)"
else
    bad "admin upsertrole on MUDA failed: $(echo "$out" | tail -1)"
fi
out=$($CLEOS push action "$CM" assignroles "{\"community_id\":\"0,MUDA\",\"member\":\"$MUDA_SECOND\",\"roles\":[\"member\",\"admin\"]}" -p "$MUDA_SECOND" 2>&1)
if echo "$out" | grep -q "executed transaction"; then
    us=$(echo "$out" | grep -o '[0-9]* us' | head -1)
    ok "admin assignroles on MUDA executed in $us"
else
    bad "admin assignroles on MUDA failed: $(echo "$out" | tail -1)"
fi

echo "=== migrate_action_validators rehearsal (all communities) ==="
BEFORE_SCOPES=$($CLEOS get scope "$CM" --table validator --limit 2000 | jq '.rows | length')
while read -r sym; do
    code=${sym#*,}
    creator=$(jq -r --arg s "$sym" '.[] | select(.symbol==$s) | .creator' "$HERE/communities.json")
    if "$REPO/scripts/migrate_action_validators.sh" -u "$NODE_URL" -s "$sym" -p "$creator" -I -a > "$HERE/.mig_$code.log" 2>&1; then
        n=$(grep -c "→ role-based" "$HERE/.mig_$code.log" || true)
        ok "migrate $code: $n actions switched ($(grep -c "skipped" "$HERE/.mig_$code.log" || true) skipped)"
    else
        bad "migrate $code: $(tail -1 "$HERE/.mig_$code.log")"
    fi
done < <(jq -r '.[].symbol' "$HERE/communities.json")
AFTER_SCOPES=$($CLEOS get scope "$CM" --table validator --limit 2000 | jq '.rows | length')
echo "  validator scopes: $BEFORE_SCOPES before → $AFTER_SCOPES after"

echo "--- role-based verifyclaim after migration ---"
PICK=$(python3 "$HERE/pick_claim.py" rolebased)
if [ -n "$PICK" ]; then
    CLAIM_ID=$(echo "$PICK" | cut -d' ' -f1)
    VERIFIER=$(echo "$PICK" | cut -d' ' -f2)
    CSYM=$(echo "$PICK" | cut -d' ' -f3)
    check_ok "role-based verify of claim $CLAIM_ID by non-listed member $VERIFIER" \
        $CLEOS push action "$CM" verifyclaim "[\"$CSYM\", $CLAIM_ID, \"$VERIFIER\", 1]" -p "$VERIFIER"
else
    bad "no suitable role-based pending claim found"
fi

echo ""
echo "================================"
echo "$PASS passed | $FAIL failed"
[ "$FAIL" -eq 0 ]
