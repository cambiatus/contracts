#!/usr/bin/env bash

# Make sure we don't use any unset variables
set -ux

WALLET_PASSWORD="PW5JZy495U6ATmFSFUmh7zBj7RDbW9fERKtwmcPbAWc75efVeKQ35"
CONTRACT="cambiatus.cm"
TOKEN_CONTRACT="cambiatus.tk"
BACKEND_ACC="cambiatus"
PUBLIC_KEY="EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

pkill -f nodeos
rm -rf nohup.out

echo "killed all running nodeos..."

nohup nodeos -e -p eosio \
    --data-dir .nodeos/data \
    --config-dir .nodeos/config \
    --plugin eosio::producer_plugin \
    --plugin eosio::producer_api_plugin \
    --plugin eosio::chain_api_plugin \
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

echo "local node is running..."

sleep 2s

cleos wallet unlock --password $WALLET_PASSWORD

echo "wallet unlocked"

echo "setting up bios and required flags"

curl -X POST http://127.0.0.1:8888/v1/producer/schedule_protocol_feature_activations -d '{"protocol_features_to_activate": ["0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"]}'

sleep 1s

cleos set code eosio ./eosio.contracts/eosio.bios.wasm -p eosio
cleos set abi eosio ./eosio.contracts/eosio.bios.abi -p eosio

echo "eosio bios contract deployed"

sleep 1s

cleos push action eosio activate '["f0af56d2c5a48d60a4a5b5c903edfb7db3a736a94ed589d0b797df33ff9d3e1d"]' -p eosio # GET_SENDER

sleep 1s

cleos create account eosio $CONTRACT $PUBLIC_KEY $PUBLIC_KEY
cleos set account permission $CONTRACT active --add-code
cleos create account eosio $TOKEN_CONTRACT $PUBLIC_KEY $PUBLIC_KEY
cleos set account permission $TOKEN_CONTRACT active --add-code
cleos create account eosio $BACKEND_ACC $PUBLIC_KEY $PUBLIC_KEY

cleos set code $CONTRACT ../community/community.wasm
cleos set abi $CONTRACT ../community/community.abi

cleos set code $TOKEN_CONTRACT ../token/token.wasm
cleos set abi $TOKEN_CONTRACT ../token/token.abi

echo "contracts were deployed"

sleep 1s

cleos create account eosio 'claimcreator' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimclaimer' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimverif1' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimverif2' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimverif3' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimverif4' $PUBLIC_KEY $PUBLIC_KEY
cleos create account eosio 'claimverif5' $PUBLIC_KEY $PUBLIC_KEY
