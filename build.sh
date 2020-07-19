#!/bin/bash

# Bespiral Community Contract
cd bespiral.community
echo "building bespiral.community...";
eosio-cpp -contract=bespiral.community -o bespiral.community.wasm -R ./ricardian bespiral.community.cpp
cd ..

# Bespiral Token Contract
cd bespiral.token
echo "building bespiral.token...";
eosio-cpp -contract=bespiral.token -o bespiral.token.wasm -R ./ricardian bespiral.token.cpp
cd ..
