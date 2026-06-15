#!/usr/bin/env bash
#
# Backfill a named role for an existing community and assign it to members.
#
# Creates (or updates) the role via upsertrole, then assigns it to each given
# account via assignroles. assignroles REPLACES a member's whole role list on
# chain, so this script first fetches the member's current roles and appends
# the new one — existing roles are preserved.
#
# The signer must be the community creator or a member holding a role with
# the "admin" permission (needs the contract built from this repo on or after
# the admin-permission change).
#
# Requires: cleos, jq, python3. The signer's key must be in an unlocked wallet.
#
# Examples:
#   # default: create an "admin" role (permissions: ["admin"]) and assign it
#   ./backfill_roles.sh -u https://app.cambiatus.io -s "0,BES" -p creatoracct \
#       -m firstadmin,secondadmin
#
#   # named validator role with the verify permission
#   ./backfill_roles.sh -u https://app.cambiatus.io -s "0,BES" -p creatoracct \
#       -r validator -P verify -c '#00aa55' -m val1,val2
#
# Flags:
#   -u  node URL (required)
#   -s  community symbol as "precision,CODE" (required)
#   -p  signing account (required)
#   -m  comma-separated accounts to receive the role (required)
#   -r  role name                       (default: admin)
#   -P  comma-separated permissions     (default: admin)
#   -c  role color                      (default: #bf360c)
#   -n  dry run — print the cleos commands without pushing

set -euo pipefail

CONTRACT="${CMM_CONTRACT:-cambiatus.cm}"
URL="" SYM="" SIGNER="" MEMBERS=""
ROLE="admin" PERMS="admin" COLOR="#bf360c" DRY_RUN=0

usage() {
    sed -n '2,34p' "$0" | cut -c3-
    exit 1
}

while getopts "u:s:p:m:r:P:c:nh" opt; do
    case "$opt" in
        u) URL="$OPTARG" ;;
        s) SYM="$OPTARG" ;;
        p) SIGNER="$OPTARG" ;;
        m) MEMBERS="$OPTARG" ;;
        r) ROLE="$OPTARG" ;;
        P) PERMS="$OPTARG" ;;
        c) COLOR="$OPTARG" ;;
        n) DRY_RUN=1 ;;
        *) usage ;;
    esac
done

if [ -z "$URL" ] || [ -z "$SYM" ] || [ -z "$SIGNER" ] || [ -z "$MEMBERS" ]; then
    usage
fi

# eosio::symbol.raw() — scope of the member/role tables
SCOPE=$(python3 -c "
prec, code = '$SYM'.split(',')
v = 0
for c in reversed(code.strip()):
    v = (v << 8) | ord(c)
print((v << 8) | int(prec))
")

push() { # <action> <json>
    echo "+ cleos -u $URL push action $CONTRACT $1 '$2' -p $SIGNER@active"
    if [ "$DRY_RUN" -eq 0 ]; then
        cleos -u "$URL" push action "$CONTRACT" "$1" "$2" -p "$SIGNER@active" > /dev/null
    fi
}

perms_json=$(jq -cn --arg p "$PERMS" '$p | split(",")')

echo "== upsertrole: $ROLE ($PERMS) on $SYM =="
push upsertrole "$(jq -cn \
    --arg cmm "$SYM" --arg name "$ROLE" --arg color "$COLOR" --argjson perms "$perms_json" \
    '{community_id: $cmm, name: $name, color: $color, permissions: $perms}')"

echo "== assignroles =="
IFS=',' read -ra ACCOUNTS <<< "$MEMBERS"
for acct in "${ACCOUNTS[@]}"; do
    # --key-type name forces name-encoding of bounds (all-numeric account
    # names like 111111111111 otherwise parse as integers and match nothing)
    current=$(cleos -u "$URL" get table "$CONTRACT" "$SCOPE" member \
        --key-type name --index 1 -L "$acct" -U "$acct" --limit 1 \
        | jq -c --arg a "$acct" '.rows[] | select(.name == $a) | .roles')

    if [ -z "$current" ]; then
        echo "!! $acct is not a member of $SYM — skipped"
        continue
    fi
    if echo "$current" | jq -e --arg r "$ROLE" 'index($r) != null' > /dev/null; then
        echo "-- $acct already has role $ROLE — skipped"
        continue
    fi

    merged=$(echo "$current" | jq -c --arg r "$ROLE" '. + [$r]')
    push assignroles "$(jq -cn \
        --arg cmm "$SYM" --arg m "$acct" --argjson roles "$merged" \
        '{community_id: $cmm, member: $m, roles: $roles}')"
done

echo "done."
