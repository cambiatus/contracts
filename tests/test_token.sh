#!/usr/bin/env bash
# Token contract integration tests.
# Requires a running bootstrapped local chain (make bootstrap).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_ROOT/tests/harness.sh"

wallet_unlock

# ── Initial state (from bootstrap) ───────────────────────────────────────────

suite "Token: initial state"

assert_table "TST token stat row exists" \
    "$TK_CONTRACT" "TST" stat \
    '.rows[0].issuer' \
    "alice"

assert_table "TST max supply correct" \
    "$TK_CONTRACT" "TST" stat \
    '.rows[0].max_supply' \
    "1000000 TST"

assert_table "TST min balance correct" \
    "$TK_CONTRACT" "TST" stat \
    '.rows[0].min_balance' \
    "-100 TST"

# Each member got 2 TST invited_reward via netlink in bootstrap
assert_table "bob has 2 TST from invite reward" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "2 TST"

assert_table "carol has 2 TST from invite reward" \
    "$TK_CONTRACT" "carol" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "2 TST"

# ── Transfers ─────────────────────────────────────────────────────────────────

suite "Token: transfers"

assert_ok "alice transfers 50 TST to bob" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["alice", "bob", "50 TST", "test transfer"]' \
    -p alice

assert_table "bob has 52 TST (2 from invite + 50 transferred)" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "52 TST"

assert_ok "bob transfers 10 TST to carol" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "carol", "10 TST", "peer transfer"]' \
    -p bob

assert_table "carol has 12 TST" \
    "$TK_CONTRACT" "carol" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "12 TST"

assert_table "bob has 42 TST" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "42 TST"

assert_fail "cannot transfer to non-member" "doesn't belong to the community" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["alice", "eosio", "1 TST", "should fail"]' \
    -p alice

assert_fail "cannot transfer to self" "cannot transfer to self" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["alice", "alice", "1 TST", "self"]' \
    -p alice

assert_fail "cannot transfer zero" "quantity must be positive" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["alice", "bob", "0 TST", "zero"]' \
    -p alice

# ── Minimum balance (overdraft) ───────────────────────────────────────────────
# min_balance = -100 TST for TST token

suite "Token: minimum balance enforcement"

# First drain bob to 0 (he has 42 TST)
assert_ok "bob drains his balance to 0" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "alice", "42 TST", "drain to zero"]' \
    -p bob

assert_table "bob balance is 0" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "0 TST"

assert_ok "bob can overdraft (0 → -50, min is -100)" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "alice", "50 TST", "overdraft to -50"]' \
    -p bob

assert_table "bob balance is -50" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "-50 TST"

assert_fail "overdraft below min_balance rejected (-50 - 60 = -110 < -100)" "overdrawn community limit" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "alice", "60 TST", "too deep overdraft"]' \
    -p bob

assert_ok "bob can transfer exactly to min_balance (-50 - 50 = -100)" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "alice", "50 TST", "drain to min"]' \
    -p bob

assert_table "bob balance is -100 (at min_balance)" \
    "$TK_CONTRACT" "bob" accounts \
    '.rows[] | select(.balance | contains("TST")) | .balance' \
    "-100 TST"

assert_fail "cannot go below -100" "overdrawn community limit" \
    $CLEOS push action "$TK_CONTRACT" transfer \
    '["bob", "alice", "1 TST", "one below min"]' \
    -p bob

# ── Token update ─────────────────────────────────────────────────────────────

suite "Token: update"

assert_ok "issuer can increase max_supply" \
    $CLEOS push action "$TK_CONTRACT" update \
    '["2000000 TST", "-200 TST"]' \
    -p alice

assert_table "max_supply updated to 2000000" \
    "$TK_CONTRACT" "TST" stat \
    '.rows[0].max_supply' \
    "2000000 TST"

assert_table "min_balance updated to -200" \
    "$TK_CONTRACT" "TST" stat \
    '.rows[0].min_balance' \
    "-200 TST"

assert_fail "cannot update with mismatched symbols" "All assets must share the same symbol" \
    $CLEOS push action "$TK_CONTRACT" update \
    '["2000000 TST", "-200 EXP"]' \
    -p alice

# ── Expiry token (separate community) ────────────────────────────────────────

suite "Token: expiry type"

assert_ok "create EXP community" \
    $CLEOS push action "$CMM_CONTRACT" create \
    '["0 EXP", "alice", "", "Expiry Community", "expiry test", "0 EXP", "0 EXP", 0, 0, 0, 0, "exp", ""]' \
    -p alice

assert_ok "create expiry-type token" \
    $CLEOS push action "$TK_CONTRACT" create \
    '["alice", "1000000 EXP", "0 EXP", "expiry"]' \
    -p alice

assert_table "EXP token type is expiry" \
    "$TK_CONTRACT" "EXP" stat \
    '.rows[0].type' \
    "expiry"

assert_fail "mcc token cannot use setexpiry" "you can only configure tokens of the" \
    $CLEOS push action "$TK_CONTRACT" setexpiry \
    '["0,TST", 3600, 7200, "1 TST"]' \
    -p alice

# setexpiry on expiry token works and schedules a deferred retire action
assert_ok "setexpiry on EXP token schedules expiry" \
    $CLEOS push action "$TK_CONTRACT" setexpiry \
    '["0,EXP", 86400, 172800, "10 EXP"]' \
    -p alice

assert_table "expiry options stored for EXP" \
    "$TK_CONTRACT" "$TK_CONTRACT" expiryopts \
    '.rows[] | select(.currency == "0,EXP") | .natural_expiration_period' \
    "86400"

summary
