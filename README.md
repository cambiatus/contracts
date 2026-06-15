# EOS Smart Contracts for Cambiatus

Two EOSIO smart contracts: `community/` (`cambiatus.cm`) manages the social layer and `token/` (`cambiatus.tk`) is the token contract.

## Prerequisites

- [eosio.cdt](https://github.com/EOSIO/eosio.cdt) v1.8.x — provides `eosio-cpp`
- EOSIO v2.1.x — provides `cleos` and `keosd`
- [Docker](https://docs.docker.com/get-docker/) — nodeos runs in a container
- `jq`

## Build

```bash
make build              # compile both contracts
cd community && make    # community only → community.wasm + community.abi
cd token && make        # token only → token.wasm + token.abi
```

## Local Development & Testing

Nodeos runs in a Docker container (`eosio/eosio:v2.1.0`). `cleos` and `keosd` run on the host.

```bash
make test        # fresh chain + compile + bootstrap + run all 90 tests
make test-only   # re-run tests on an already-running bootstrapped chain (fast)
```

Other node commands:

```bash
make node-fresh   # start a clean nodeos container at http://127.0.0.1:8888
make bootstrap    # deploy contracts + seed TST community (requires running node)
make node-stop    # stop the nodeos container
make node-status  # show head block / LIB
make node-logs    # tail nodeos container logs
```

### Seed state

Bootstrap creates these accounts (all share one dev key):

| Account | Role |
|---|---|
| `alice` | community creator, token issuer |
| `bob`, `carol`, `dave`, `eve` | members |
| `cambiatus.cm` | community contract |
| `cambiatus.tk` | token contract |
| `cambiatus` | backend account |

Community `TST`: mcc type, max 1M, min -100, inviter_reward 1 TST, invited_reward 2 TST.

Dev key pair:
- Private: `5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3`
- Public: `EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV`

## Deploy

```bash
cd community && make deploy   # deploys to https://app.cambiatus.io by default
cd token && make deploy
```

Override the target network via the `url` Makefile variable:

```bash
make deploy url=https://staging.cambiatus.io
```
