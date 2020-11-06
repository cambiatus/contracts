# EOS Smart Contracts for Cambiatus

All of our Cambiatus Smart contracts lives here. You'll find everything you need to run here.

## Install

```
git clone https://github.com/cambiatus/contracts.git
```

Our docker setup include two nodes `keosd` and `nodeosd`. The `keosd` is responsible for running the wallet and as well the `cleos` commmand, since this way `cleos` won't spin up its own version of `keosd` daemon everytime you invoke it. The other `nodeos` node is responsible for running the blockchain and compiling files.

## Build

Building the Community Contract

```
cd community
make
```

## Attention

This repo only works using [eosio.cdt](https://github.com/EOSIO/eosio.cdt/releases/tag/v1.7.0) version `v1.7.0`

## Setting up a Local EOS Environment

To setup a local environment you need to install locally a version of EOSIO;
if installed properly, it will add `nodeos` and `cleos` to your binaries.

Then you can execute the following commands:

```sh
WALLET_PASSWORD="YOUR--EOS--WALLET--PASS"
CONTRACT="cambiatus.cm"
TOKEN_CONTRACT="cambiatus.tk"
BACKEND_ACC="cambiatus"
PUBLIC_KEY="EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

nohup nodeos -e -p eosio \
    --data-dir .nodeos/data \
    --config-dir .nodeos/config \
    --plugin eosio::producer_plugin \
    --plugin eosio::producer_api_plugin \
    --plugin eosio::chain_api_plugin \
    --plugin eosio::history_plugin \
    --plugin eosio::history_api_plugin \
    --plugin eosio::http_plugin \
    --plugin eosio::state_history_plugin \
    --access-control-allow-origin='*' \
    --contracts-console \
    --http-validate-host=false \
    --trace-history \
    --chain-state-history \
    --verbose-http-errors \
    --filter-on='*' \
    --disable-replay-opts \
    --delete-all-blocks &

curl -X POST http://127.0.0.1:8888/v1/producer/schedule_protocol_feature_activations -d '{"protocol_features_to_activate": ["0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"]}'

cleos set code eosio ./tests/eosio.contracts/eosio.bios.wasm -p eosio
cleos set abi eosio ./tests/eosio.contracts/eosio.bios.abi -p eosio

cleos push action eosio activate '["f0af56d2c5a48d60a4a5b5c903edfb7db3a736a94ed589d0b797df33ff9d3e1d"]' -p eosio # GET_SENDER

cleos create account eosio $CONTRACT $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio $TOKEN_CONTRACT $PUBLIC_KEY $PUBLIC_KEY

cleos set account permission $TOKEN_CONTRACT active '{"threshold": 1, "keys": [{"key": "'$PUBLIC_KEY'", "weight": 1}], "accounts": [{"permission": {"actor": "'$CONTRACT'", "permission": "eosio.code"}, "weight": 1}]}' owner

cleos set account permission $CONTRACT active '{"threshold": 1, "keys": [{"key": "'$PUBLIC_KEY'", "weight": 1}], "accounts": [{"permission": {"actor": "'$TOKEN_CONTRACT'", "permission": "eosio.code"}, "weight": 1}]}' owner

cleos set account permission $CONTRACT active --add-code
cleos set account permission $TOKEN_CONTRACT active --add-code

cleos create account eosio $BACKEND_ACC $PUBLIC_KEY $PUBLIC_KEY

cleos set code $CONTRACT ../community/community.wasm
cleos set abi $CONTRACT ../community/community.abi

cleos set code $TOKEN_CONTRACT ../token/token.wasm
cleos set abi $TOKEN_CONTRACT ../token/token.abi
```
