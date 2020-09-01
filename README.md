# EOS Smart Contracts for Cambiatus

All of our Cambiatus Smart contracts lives here. You'll find everything you need to run here.

## Install

```
git clone https://github.com/cambiatus/contracts.git
```

Our docker setup include two nodes `keosd` and `nodeosd`. The `keosd` is responsible for running the wallet and as well the `cleos` commmand, since this way `cleos` won't spin up its own version of `keosd` daemon everytime you invoke it. The other `nodeos` node is responsible for running the blockchain and compiling files.

## Attention

This repo only works using [eosio.cdt](https://github.com/EOSIO/eosio.cdt/releases/tag/v1.7.0) version `v1.7.0`
