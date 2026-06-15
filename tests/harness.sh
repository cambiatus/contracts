#!/usr/bin/env bash
# Shared test utilities. Source this file from test scripts.

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

NODE_URL="http://127.0.0.1:8888"
CMM_CONTRACT="cambiatus.cm"
TK_CONTRACT="cambiatus.tk"

CLEOS="cleos -u $NODE_URL"
WALLET_NAME="cambiatus_dev"
WALLET_PW_FILE="/tmp/cambiatus-nodeos/wallet.pw"

_PASS=0
_FAIL=0

function suite() {
    echo ""
    echo -e "${CYAN}=== $1 ===${NC}"
}

# _pass <description>
function _pass() {
    echo -e "  ${GREEN}✓${NC} $1"
    ((_PASS++)) || true
}

# _fail <description> [detail_lines...]
function _fail() {
    local desc="$1"; shift
    echo -e "  ${RED}✗${NC} $desc"
    local line
    for line in "$@"; do
        echo "    $line"
    done
    ((_FAIL++)) || true
}

# assert_ok <description> <command...>
function assert_ok() {
    local desc="$1"; shift
    if output=$("$@" 2>&1); then
        _pass "$desc"
    else
        _fail "$desc" "CMD: $*" "OUT: $output"
    fi
}

# assert_fail <description> <expected_error_substring> <command...>
function assert_fail() {
    local desc="$1"
    local expected="$2"
    shift 2
    if output=$("$@" 2>&1); then
        _fail "$desc (expected failure, but succeeded)" "CMD: $*"
    elif echo "$output" | grep -q "$expected"; then
        _pass "$desc"
    else
        _fail "$desc (wrong error)" "expected: '$expected'" "got:      $output"
    fi
}

# assert_table <description> <contract> <scope> <table> <jq_query> <expected_value>
function assert_table() {
    local desc="$1" contract="$2" scope="$3" table="$4" query="$5" expected="$6"
    actual=$($CLEOS get table "$contract" "$scope" "$table" 2>/dev/null | jq -r "$query" 2>/dev/null)
    if [ "$actual" = "$expected" ]; then
        _pass "$desc"
    else
        _fail "$desc" "expected: '$expected'" "got:      '$actual'"
    fi
}

function get_table() {
    local contract="$1" scope="$2" table="$3" query="$4"
    $CLEOS get table "$contract" "$scope" "$table" 2>/dev/null | jq -r "$query" 2>/dev/null
}

function wallet_unlock() {
    if [ -f "$WALLET_PW_FILE" ]; then
        $CLEOS wallet unlock -n "$WALLET_NAME" --password "$(cat "$WALLET_PW_FILE")" 2>/dev/null || true
    fi
}

# sym_scope <SYMBOL_CODE> [precision]
# Returns the eosio::symbol.raw() uint64 used as multi-index scope.
# eosio packs symbol_code as chars right-to-left into uint64 bytes, then
# shifts left 8 bits and ORs in the precision.
function sym_scope() {
    local sym="$1"
    local prec="${2:-0}"
    python3 -c "
v = 0
for c in reversed('$sym'):
    v = (v << 8) | ord(c)
print((v << 8) | $prec)
"
}

function summary() {
    echo ""
    echo "================================"
    local total=$((_PASS + _FAIL))
    if [ $_FAIL -eq 0 ]; then
        echo -e "${GREEN}All $_PASS / $total tests passed${NC}"
    else
        echo -e "${RED}$_FAIL failed${NC} | ${GREEN}$_PASS passed${NC} | $total total"
        exit 1
    fi
}
