#!/usr/bin/env bash
# Bootstrap a fresh local chain: wallet, accounts, contracts, permissions, seed data.
# Run after `tests/node.sh fresh`. Idempotent on a fresh chain.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NODE_URL="http://127.0.0.1:8888"
CLEOS="cleos -u $NODE_URL"

CMM_CONTRACT="cambiatus.cm"
TK_CONTRACT="cambiatus.tk"
BACKEND_ACC="cambiatus"

DEV_PUB_KEY="EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
DEV_PRIV_KEY="5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"

WALLET_NAME="cambiatus_dev"
WALLET_DIR="/tmp/cambiatus-nodeos"
WALLET_PW_FILE="$WALLET_DIR/wallet.pw"

# ── Wallet ────────────────────────────────────────────────────────────────────

function setup_wallet() {
    echo "=== Wallet ==="
    mkdir -p "$WALLET_DIR"

    # Restart keosd fresh to avoid stale lock state
    pkill -x keosd 2>/dev/null || true
    sleep 1
    keosd --unlock-timeout 999999 &
    sleep 2

    # Try to create wallet; if it already exists, delete and recreate
    if ! $CLEOS wallet create -n "$WALLET_NAME" --file "$WALLET_PW_FILE" 2>/dev/null; then
        $CLEOS wallet delete_key -n "$WALLET_NAME" 2>/dev/null || true
        # Unlock existing wallet so we can use it, otherwise recreate
        if [ -f "$WALLET_PW_FILE" ]; then
            if ! $CLEOS wallet unlock -n "$WALLET_NAME" --password "$(cat "$WALLET_PW_FILE")" 2>/dev/null; then
                # Password wrong or wallet corrupt — delete and recreate
                rm -f ~/eosio-wallet/"$WALLET_NAME".wallet 2>/dev/null || true
                rm -f ~/.local/share/eosio/wallet/"$WALLET_NAME".wallet 2>/dev/null || true
                $CLEOS wallet create -n "$WALLET_NAME" --file "$WALLET_PW_FILE"
            fi
        fi
    fi
    echo "  wallet ready"

    $CLEOS wallet import -n "$WALLET_NAME" --private-key "$DEV_PRIV_KEY" 2>/dev/null \
        && echo "  dev key imported" \
        || echo "  dev key already imported"
}

# ── Chain features ────────────────────────────────────────────────────────────

GET_SENDER_DIGEST="f0af56d2c5a48d60a4a5b5c903edfb7db3a736a94ed589d0b797df33ff9d3e1d"

function feature_active() {
    local digest="$1"
    curl -s -X POST "$NODE_URL/v1/chain/get_activated_protocol_features" \
        -d '{"lower_bound":1}' 2>/dev/null \
        | grep -q "\"$digest\""
}

function activate_features() {
    echo "=== Chain features ==="

    if feature_active "$GET_SENDER_DIGEST"; then
        echo "  GET_SENDER already active"
        return 0
    fi

    # PREACTIVATE_FEATURE — unlocks the activate action on the system contract
    curl -s -X POST "$NODE_URL/v1/producer/schedule_protocol_feature_activations" \
        -d '{"protocol_features_to_activate": ["0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"]}' \
        | jq -r '"  preactivate: \(.result)"' 2>/dev/null || echo "  preactivate sent"
    sleep 1

    # Set ABI before code so setabi runs through native handler (no WASM dispatch yet).
    # eosio.boot.wasm crashes on setabi dispatch under x86 emulation.
    $CLEOS set abi  eosio "$REPO_ROOT/tests/eosio.contracts/eosio.boot.abi"  -p eosio > /dev/null
    $CLEOS set code eosio "$REPO_ROOT/tests/eosio.contracts/eosio.boot.wasm" -p eosio > /dev/null
    sleep 1
    echo "  eosio.boot deployed"

    # GET_SENDER (required by community contract's get_sender() call)
    $CLEOS push action eosio activate \
        "[\"$GET_SENDER_DIGEST\"]" \
        -p eosio > /dev/null
    sleep 1
    echo "  GET_SENDER activated"
}

# ── Accounts ──────────────────────────────────────────────────────────────────

function create_accounts() {
    echo "=== Accounts ==="
    # All test accounts share the single dev key for simplicity
    local accounts=("$CMM_CONTRACT" "$TK_CONTRACT" "$BACKEND_ACC" "alice" "bob" "carol" "dave" "eve")
    for acc in "${accounts[@]}"; do
        if $CLEOS get account "$acc" > /dev/null 2>&1; then
            echo "  exists: $acc"
        else
            $CLEOS create account eosio "$acc" "$DEV_PUB_KEY" "$DEV_PUB_KEY" > /dev/null
            echo "  created: $acc"
        fi
    done
}

# ── Deploy contracts ──────────────────────────────────────────────────────────

function deploy_contracts() {
    echo "=== Contracts ==="
    $CLEOS set contract "$CMM_CONTRACT" "$REPO_ROOT/community" community.wasm community.abi -p "$CMM_CONTRACT" > /dev/null
    echo "  deployed: $CMM_CONTRACT (community)"
    $CLEOS set contract "$TK_CONTRACT" "$REPO_ROOT/token" token.wasm token.abi -p "$TK_CONTRACT" > /dev/null
    echo "  deployed: $TK_CONTRACT (token)"
}

# ── Permissions ───────────────────────────────────────────────────────────────

function set_permissions() {
    echo "=== Permissions ==="

    # cambiatus.cm can use cambiatus.tk@active (for issue/initacc inline actions)
    $CLEOS set account permission "$TK_CONTRACT" active \
        "{\"threshold\": 1, \"keys\": [{\"key\": \"$DEV_PUB_KEY\", \"weight\": 1}], \"accounts\": [{\"permission\": {\"actor\": \"$CMM_CONTRACT\", \"permission\": \"eosio.code\"}, \"weight\": 1}]}" \
        owner -p "$TK_CONTRACT@owner" > /dev/null
    echo "  $CMM_CONTRACT can act as $TK_CONTRACT@active"

    # cambiatus.tk can use cambiatus.cm@active (for netlink inline actions from token)
    $CLEOS set account permission "$CMM_CONTRACT" active \
        "{\"threshold\": 1, \"keys\": [{\"key\": \"$DEV_PUB_KEY\", \"weight\": 1}], \"accounts\": [{\"permission\": {\"actor\": \"$TK_CONTRACT\", \"permission\": \"eosio.code\"}, \"weight\": 1}]}" \
        owner -p "$CMM_CONTRACT@owner" > /dev/null
    echo "  $TK_CONTRACT can act as $CMM_CONTRACT@active"

    # eosio.code on each contract so they can use their own active in inline actions
    $CLEOS set account permission "$CMM_CONTRACT" active --add-code -p "$CMM_CONTRACT@owner" > /dev/null
    $CLEOS set account permission "$TK_CONTRACT"  active --add-code -p "$TK_CONTRACT@owner"  > /dev/null
    echo "  eosio.code added to both contracts"
}

# ── Seed data ─────────────────────────────────────────────────────────────────
# Creates a TST community with alice as creator, bob/carol/dave as members.
# Token has inviter_reward=1TST, invited_reward=2TST so members start with a balance.

function seed_data() {
    echo "=== Seed data ==="

    # Community (creator alice is automatically netlinked; reward skipped because inviter==new_user)
    $CLEOS push action "$CMM_CONTRACT" create \
        '["0 TST", "alice", "", "Test Community", "Local dev seed", "1 TST", "2 TST", 1, 0, 0, 0, "test", ""]' \
        -p alice > /dev/null
    echo "  community TST created (creator: alice)"

    # Token (must be created after community; alice is issuer)
    $CLEOS push action "$TK_CONTRACT" create \
        '["alice", "1000000 TST", "-100 TST", "mcc"]' \
        -p alice > /dev/null
    echo "  token TST created (issuer: alice, max: 1000000, min: -100)"

    # Issue initial supply to alice (token.issue requires cambiatus.tk@active authority)
    $CLEOS push action "$TK_CONTRACT" issue \
        '["alice", "10000 TST", "bootstrap"]' \
        -p "$TK_CONTRACT" > /dev/null
    echo "  issued 10000 TST to alice"

    # Netlink members — each invited user gets 2 TST issued automatically via inline action,
    # inviter (alice) gets 1 TST per invite
    for member in bob carol dave eve; do
        $CLEOS push action "$CMM_CONTRACT" netlink \
            "[\"0,TST\", \"alice\", \"$member\", \"natural\"]" \
            -p alice > /dev/null
        echo "  netlinked: $member (receives 2 TST; alice receives 1 TST)"
    done

    echo ""
    echo "Bootstrap complete. Chain ready at $NODE_URL"
    echo "Accounts: alice (creator/issuer), bob, carol, dave, eve (members)"
    echo "Token: TST | Community: TST"
}

setup_wallet
activate_features
create_accounts
deploy_contracts
set_permissions
seed_data
