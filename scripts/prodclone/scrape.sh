#!/usr/bin/env bash
# Scrape Cambiatus prod chain state needed for a local upgrade-validation clone.
set -euo pipefail
cd "$(dirname "$0")"

URL="https://app.cambiatus.io"
CM="cambiatus.cm"
TK="cambiatus.tk"

# get_table_rows pagination helper: table_all <code> <scope> <table> <outfile>
table_all() {
    local code="$1" scope="$2" table="$3" out="$4"
    local lower="" page rows
    : > "$out.tmp"
    while :; do
        page=$(curl -s "$URL/v1/chain/get_table_rows" -d "{\"json\":true,\"code\":\"$code\",\"scope\":\"$scope\",\"table\":\"$table\",\"limit\":1000,\"lower_bound\":\"$lower\"}")
        echo "$page" | jq -c '.rows[]' >> "$out.tmp"
        [ "$(echo "$page" | jq -r '.more')" = "true" ] || break
        lower=$(echo "$page" | jq -r '.next_key')
    done
    jq -s '.' "$out.tmp" > "$out"
    rm "$out.tmp"
    echo "$out: $(jq 'length' "$out") rows"
}

sym_scope() {
    python3 -c "
prec, code = '$1'.split(',')
v = 0
for c in reversed(code):
    v = (v << 8) | ord(c)
print((v << 8) | int(prec))"
}

echo "== contract code =="
curl -s "$URL/v1/chain/get_raw_code_and_abi" -d "{\"account_name\":\"$CM\"}" > cm_raw.json
jq -r '.wasm' cm_raw.json | base64 -d > prod_cm.wasm
curl -s "$URL/v1/chain/get_raw_code_and_abi" -d "{\"account_name\":\"$TK\"}" > tk_raw.json
jq -r '.wasm' tk_raw.json | base64 -d > prod_tk.wasm
curl -s "$URL/v1/chain/get_abi" -d "{\"account_name\":\"$TK\"}" | jq '.abi' > tk_abi.json
shasum -a 256 prod_cm.wasm prod_tk.wasm

echo "== global tables =="
table_all "$CM" "$CM" community communities.json
table_all "$CM" "$CM" action actions.json
table_all "$CM" "$CM" claim claims.json
table_all "$CM" "$CM" check checks.json
table_all "$CM" "$CM" indexes indexes.json

echo "== per-community tables =="
jq -r '.[].symbol' communities.json | while read -r sym; do
    scope=$(sym_scope "$sym")
    code=${sym#*,}
    table_all "$CM" "$scope" member "member_$code.json"
    table_all "$CM" "$scope" role "role_$code.json"
    table_all "$CM" "$scope" objective "objective_$code.json"
    table_all "$TK" "$code" stat "stat_$code.json"
    table_all "$TK" "$code" expiryopts "expiry_$code.json"
done

echo "== validator tables (one scope per action with explicit validators) =="
lower=""
: > validator_scopes.txt
while :; do
    page=$(curl -s "$URL/v1/chain/get_table_by_scope" -d "{\"code\":\"$CM\",\"table\":\"validator\",\"limit\":1000,\"lower_bound\":\"$lower\"}")
    echo "$page" | jq -r '.rows[].scope' >> validator_scopes.txt
    lower=$(echo "$page" | jq -r '.more')
    [ -n "$lower" ] && [ "$lower" != "null" ] || break
done
: > validators.tmp
while read -r scope; do
    page=$(curl -s "$URL/v1/chain/get_table_rows" -d "{\"json\":true,\"code\":\"$CM\",\"scope\":\"$scope\",\"table\":\"validator\",\"limit\":1000}")
    echo "$page" | jq -c '.rows[]' >> validators.tmp
done < validator_scopes.txt
jq -s '.' validators.tmp > validators.json && rm validators.tmp
echo "validators.json: $(jq 'length' validators.json) rows ($(wc -l < validator_scopes.txt | tr -d ' ') scopes)"

echo "== scrape complete =="
