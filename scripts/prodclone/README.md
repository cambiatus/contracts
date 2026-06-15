# Prod-chain clone for upgrade validation

Rebuilds production chain state (all communities, members, roles, objectives,
actions, validators, pending claims, checks) on a fresh local node running the
*currently deployed* prod contract, so a `set contract` upgrade can be
validated against real data before deploying.

## Order

```bash
./scrape.sh             # pull prod tables + deployed wasm/abi (writes ./\*.json)
./generate_replay.py    # build pNN_*.jsonl transaction batches
./clone_prod.sh         # fresh node + prod contracts + replay all phases
./gen_repair.py         # diff local vs prod, emit repair.jsonl for gaps
#  push repair.jsonl with the same pusher loop, repeat until stable
./validate_upgrade.sh   # state dump → upgrade → byte-diff → behavior matrix
```

Run from a scratch directory — the scripts read/write JSON in their own dir.

## Known fidelity deviations (see manifest.json after generate)

- Claim proof codes are regenerated (sha256 prefix needs a fresh proof_time).
- Actions with `verifications` < 3 or even (pre-validation-era rows) are
  clamped to the contract's current rules; if their explicit validator list
  becomes too small they switch to role-based.
- BEM-style tokens whose issuer ≠ community creator are created with
  issuer = creator (`token::create`'s inline netlink for that path is broken).
- Members whose inviter chain is broken are re-linked to the creator.
- Token balances are not cloned (rewards mint locally as flows replay).

## Pitfalls that cost time

- Push the jsonl lines with `printf '%s' "$line"`, never `echo` — zsh echo
  interprets `\n` inside JSON strings and corrupts transactions.
- `cleos get table -L/-U` on all-numeric account names needs
  `--key-type name --index 1` or the bound parses as an integer.
- `tests/bootstrap.sh` leaves keosd running with stdout inherited — never
  pipe `make bootstrap` output, redirect to a file.
