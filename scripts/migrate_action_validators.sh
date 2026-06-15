#!/usr/bin/env bash
#
# Migrate claimable actions from explicit validator lists to role-based
# validation by re-pushing upsertaction with validators_str = "".
#
# After migration, any community member whose roles carry the "verify"
# permission can verify claims on the action. Claims already pending keep
# their recorded checks; only new checks use the role-based rule.
#
# Every field is read back from the chain row so the re-push changes only the
# validator list. The original creator is preserved in the "creator" field;
# the signer (-p) only needs upsertaction authority — the action creator, any
# community admin, or the contract account (cambiatus.cm) itself. Signing as
# cambiatus.cm lets the migration run centrally with no user key, the same
# self-auth path used for the role backfill.
#
#   * The action image lives only in the Postgres DB (written by
#     event-source), not on chain. Pass -d <postgres_url> so the script can
#     read it back and re-send it; without it you must pass -I to acknowledge
#     that the DB image of each migrated action will be cleared.
#
# Skipped automatically: non-claimable actions, completed actions, actions
# whose deadline already passed (the contract rejects them), actions already
# role-based (no explicit validators), and actions whose creator is no longer
# a community member (is_member(creator) would reject the re-push).
#
# Requires: cleos, jq, python3; psql when -d is used.
#
# Usage:
#   ./migrate_action_validators.sh -u <node_url> -s "0,SYM" -p <signer> \
#       [-d postgres://...] [-I] [-n] (-a | action_id [action_id ...])
#
# Flags:
#   -u  node URL (required)
#   -s  community symbol "precision,CODE" (required)
#   -p  signing account: action creator, community admin, or cambiatus.cm (required)
#   -d  Postgres URL of the cambiatus DB, used to read action images
#   -I  proceed without DB access (clears DB images of migrated actions)
#   -a  migrate every eligible action of the community
#   -n  dry run — print the cleos commands without pushing

set -euo pipefail

CONTRACT="${CMM_CONTRACT:-cambiatus.cm}"
URL="" SYM="" SIGNER="" DB_URL=""
ALL=0 NO_IMAGE=0 DRY_RUN=0

usage() {
    sed -n '2,41p' "$0" | cut -c3-
    exit 1
}

while getopts "u:s:p:d:Ianh" opt; do
    case "$opt" in
        u) URL="$OPTARG" ;;
        s) SYM="$OPTARG" ;;
        p) SIGNER="$OPTARG" ;;
        d) DB_URL="$OPTARG" ;;
        I) NO_IMAGE=1 ;;
        a) ALL=1 ;;
        n) DRY_RUN=1 ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))

if [ -z "$URL" ] || [ -z "$SYM" ] || [ -z "$SIGNER" ]; then
    usage
fi
if [ "$ALL" -eq 0 ] && [ $# -eq 0 ]; then
    echo "error: pass -a or at least one action_id" >&2
    usage
fi
if [ -z "$DB_URL" ] && [ "$NO_IMAGE" -eq 0 ]; then
    echo "error: the action image is only stored in the Postgres DB. Pass" >&2
    echo "       -d <postgres_url> to preserve images, or -I to clear them." >&2
    exit 1
fi

# eosio::symbol.raw() — scope of the objective table
SCOPE=$(python3 -c "
prec, code = '$SYM'.split(',')
v = 0
for c in reversed(code.strip()):
    v = (v << 8) | ord(c)
print((v << 8) | int(prec))
")

NOW=$(date +%s)

push() { # <json>
    echo "+ cleos -u $URL push action $CONTRACT upsertaction '...' -p $SIGNER@active"
    if [ "$DRY_RUN" -eq 0 ]; then
        cleos -u "$URL" push action "$CONTRACT" upsertaction "$1" -p "$SIGNER@active" > /dev/null
    fi
}

# Collect candidate action ids
ids=()
if [ "$ALL" -eq 1 ]; then
    obj_ids=$(cleos -u "$URL" get table "$CONTRACT" "$SCOPE" objective --limit 1000 \
        | jq -c '[.rows[].id]')
    lower=0
    while :; do
        page=$(cleos -u "$URL" get table "$CONTRACT" "$CONTRACT" action --limit 500 -L "$lower")
        while IFS= read -r id; do
            ids+=("$id")
        done < <(echo "$page" | jq -r --argjson objs "$obj_ids" \
            '.rows[] | select(.objective_id as $o | $objs | index($o) != null) | .id')
        [ "$(echo "$page" | jq -r '.more')" = "true" ] || break
        lower=$(echo "$page" | jq -r '.next_key')
    done
else
    ids=("$@")
fi

echo "== migrating ${#ids[@]} candidate action(s) on $SYM =="
migrated=0
for id in "${ids[@]}"; do
    if ! [[ "$id" =~ ^[0-9]+$ ]]; then
        echo "!! '$id' is not a numeric action id — skipped"
        continue
    fi

    row=$(cleos -u "$URL" get table "$CONTRACT" "$CONTRACT" action \
        -L "$id" -U "$id" --limit 1 | jq -c '.rows[0] // empty')
    if [ -z "$row" ]; then
        echo "!! action $id not found — skipped"
        continue
    fi

    vt=$(echo "$row" | jq -r '.verification_type')
    if [ "$vt" != "claimable" ]; then
        echo "-- action $id is $vt, not claimable — skipped"
        continue
    fi
    if [ "$(echo "$row" | jq -r '.is_completed')" = "1" ]; then
        echo "-- action $id is completed — skipped"
        continue
    fi
    deadline=$(echo "$row" | jq -r '.deadline')
    if [ "$deadline" -gt 0 ] && [ "$deadline" -le "$NOW" ]; then
        echo "-- action $id deadline already passed — skipped"
        continue
    fi
    n_validators=$(cleos -u "$URL" get table "$CONTRACT" "$id" validator --limit 1000 \
        | jq -r '.rows | length')
    if [ "$n_validators" -eq 0 ]; then
        echo "-- action $id already role-based — skipped"
        continue
    fi

    # Legacy actions created before the contract enforced odd>=3 verifications
    # can't be re-saved (upsertaction would reject them). Leave them explicit.
    verifs=$(echo "$row" | jq -r '.verifications')
    if [ "$verifs" -lt 3 ] || [ $((verifs % 2)) -eq 0 ]; then
        echo "-- action $id verifications=$verifs (not odd>=3) — contract would reject re-save, skipped"
        continue
    fi

    creator=$(echo "$row" | jq -r '.creator')
    # The contract still requires the action's creator to be a community
    # member (is_member check). If they've left, the re-push would be rejected,
    # so skip and surface it rather than aborting the whole batch.
    n_member_rows=$(cleos -u "$URL" get table "$CONTRACT" "$SCOPE" member \
        --key-type name --index 1 -L "$creator" -U "$creator" --limit 1 \
        | jq -r --arg c "$creator" '[.rows[] | select(.name == $c)] | length')
    if [ "$n_member_rows" -eq 0 ]; then
        echo "!! action $id creator $creator is no longer a member of $SYM — skipped"
        continue
    fi

    image=""
    if [ -n "$DB_URL" ]; then
        image=$(psql "$DB_URL" -tA -c "select coalesce(image, '') from actions where id = $id")
    fi

    data=$(echo "$row" | jq -c --arg cmm "$SYM" --arg creator "$creator" --arg img "$image" '{
        community_id: $cmm,
        action_id: .id,
        objective_id: .objective_id,
        description: .description,
        reward: .reward,
        verifier_reward: .verifier_reward,
        deadline: .deadline,
        usages: .usages,
        usages_left: .usages_left,
        verifications: .verifications,
        verification_type: .verification_type,
        validators_str: "",
        is_completed: .is_completed,
        creator: $creator,
        has_proof_photo: .has_proof_photo,
        has_proof_code: .has_proof_code,
        photo_proof_instructions: .photo_proof_instructions,
        image: $img
    }')

    echo "== action $id: $n_validators explicit validator(s) → role-based =="
    # A single push failure must not abort the whole batch (set -e); log & go on.
    if push "$data"; then
        migrated=$((migrated + 1))
    else
        echo "!! action $id push FAILED — skipped (continuing)"
    fi
done

echo "done. migrated $migrated action(s)."
