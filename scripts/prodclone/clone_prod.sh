#!/usr/bin/env bash
# Rebuild prod chain state on a fresh local node running the PROD contract code.
# Run generate_replay.py first. Then: ./clone_prod.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="/Users/lucca/Development/cpp/cambiatus/contracts"
NODE_URL="http://127.0.0.1:8888"
CLEOS="cleos -u $NODE_URL"
CM="cambiatus.cm"
TK="cambiatus.tk"
BACKEND="cambiatus"
DEV_PUB_KEY="EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
DEV_PRIV_KEY="5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
WALLET_NAME="cambiatus_dev"
WALLET_DIR="/tmp/cambiatus-nodeos"
WALLET_PW_FILE="$WALLET_DIR/wallet.pw"
GET_SENDER_DIGEST="f0af56d2c5a48d60a4a5b5c903edfb7db3a736a94ed589d0b797df33ff9d3e1d"

echo "=== fresh node ==="
"$REPO/tests/node.sh" fresh

echo "=== wallet ==="
mkdir -p "$WALLET_DIR"
pkill -x keosd 2>/dev/null || true
sleep 1
keosd --unlock-timeout 999999 > /dev/null 2>&1 &
sleep 2
if ! $CLEOS wallet create -n "$WALLET_NAME" --file "$WALLET_PW_FILE" 2>/dev/null; then
    if ! $CLEOS wallet unlock -n "$WALLET_NAME" --password "$(cat "$WALLET_PW_FILE")" 2>/dev/null; then
        rm -f ~/eosio-wallet/"$WALLET_NAME".wallet ~/.local/share/eosio/wallet/"$WALLET_NAME".wallet
        $CLEOS wallet create -n "$WALLET_NAME" --file "$WALLET_PW_FILE"
    fi
fi
$CLEOS wallet import -n "$WALLET_NAME" --private-key "$DEV_PRIV_KEY" 2>/dev/null || true

echo "=== chain features ==="
curl -s -X POST "$NODE_URL/v1/producer/schedule_protocol_feature_activations" \
    -d '{"protocol_features_to_activate": ["0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"]}' > /dev/null
sleep 1
$CLEOS set abi  eosio "$REPO/tests/eosio.contracts/eosio.boot.abi"  -p eosio > /dev/null
$CLEOS set code eosio "$REPO/tests/eosio.contracts/eosio.boot.wasm" -p eosio > /dev/null
sleep 1
$CLEOS push action eosio activate "[\"$GET_SENDER_DIGEST\"]" -p eosio > /dev/null
sleep 1
echo "  GET_SENDER active"

echo "=== system accounts ==="
for acc in "$CM" "$TK" "$BACKEND"; do
    $CLEOS get account "$acc" > /dev/null 2>&1 \
        || $CLEOS create account eosio "$acc" "$DEV_PUB_KEY" "$DEV_PUB_KEY" > /dev/null
done

echo "=== deploy PROD contracts ==="
$CLEOS set abi  "$CM" "$HERE/cm_abi.json"    -p "$CM" > /dev/null
$CLEOS set code "$CM" "$HERE/prod_cm.wasm"   -p "$CM" > /dev/null
$CLEOS set abi  "$TK" "$HERE/tk_abi.json"    -p "$TK" > /dev/null
$CLEOS set code "$TK" "$HERE/prod_tk.wasm"   -p "$TK" > /dev/null
echo "  cm code hash: $($CLEOS get code "$CM" | awk '{print $3}')"

echo "=== permissions ==="
$CLEOS set account permission "$TK" active \
    "{\"threshold\": 1, \"keys\": [{\"key\": \"$DEV_PUB_KEY\", \"weight\": 1}], \"accounts\": [{\"permission\": {\"actor\": \"$CM\", \"permission\": \"eosio.code\"}, \"weight\": 1}]}" \
    owner -p "$TK@owner" > /dev/null
$CLEOS set account permission "$CM" active \
    "{\"threshold\": 1, \"keys\": [{\"key\": \"$DEV_PUB_KEY\", \"weight\": 1}], \"accounts\": [{\"permission\": {\"actor\": \"$TK\", \"permission\": \"eosio.code\"}, \"weight\": 1}]}" \
    owner -p "$CM@owner" > /dev/null
$CLEOS set account permission "$CM" active --add-code -p "$CM@owner" > /dev/null
$CLEOS set account permission "$TK" active --add-code -p "$TK@owner" > /dev/null

echo "=== replay phases ==="
push_phase() {
    local file="$1"
    local total ok=0 fail=0 line label
    total=$(wc -l < "$HERE/$file" | tr -d ' ')
    : > "$HERE/$file.failed"
    while IFS= read -r line; do
        label=$(echo "$line" | jq -r '.label')
        echo "$line" | jq -c '.tx' > "$HERE/.push_tx.json"
        if cleos -u "$NODE_URL" push transaction "$HERE/.push_tx.json" > /dev/null 2>"$HERE/.push_err"; then
            ok=$((ok + 1))
        else
            fail=$((fail + 1))
            printf '%s\t%s\n' "$label" "$(tr '\n' ' ' < "$HERE/.push_err")" >> "$HERE/$file.failed"
        fi
        if [ $(( (ok + fail) % 100 )) -eq 0 ]; then
            echo "  $file: $((ok + fail))/$total (failed: $fail)"
        fi
    done < "$HERE/$file"
    echo "  $file done: $ok ok, $fail failed"
}

for phase in p1_accounts p2_communities p3_netlink p4_roles p5_objectives \
             p6_actions p7_claims p8_checks p9_action_patches p10_indexes; do
    echo "--- $phase ---"
    push_phase "$phase.jsonl"
done

echo "=== clone complete ==="
