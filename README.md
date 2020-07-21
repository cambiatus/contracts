# EOS Smart Contracts for Cambiatus

All of our Cambiatus Smart contracts lives here. You'll find everything you need to run here.

## Install

```
git clone https://github.com/cambiatus/contracts.git
```

Our docker setup include two nodes `keosd` and `nodeosd`. The `keosd` is responsible for running the wallet and as well the `cleos` commmand, since this way `cleos` won't spin up its own version of `keosd` daemon everytime you invoke it. The other `nodeos` node is responsible for running the blockchain and compiling files.


## Attention

This repo only works using [eosio.cdt](https://github.com/EOSIO/eosio.cdt/releases/tag/v1.5.0) version `v1.5.0`

### If you have problems compiling

We map one virtual disk volume to the folder `/contracts` in order for us to be able to compile all of EOS Smart contract dependencies. If this isn't working for you you'll need to link your eos instalation to our Docker images. You can change the `volume:` keys on `docker-compose.yml` to match your EOS installation path:

```
# Install EOS as described in https://developers.eos.io
cd YOUR_EOS_REPO_PATH
./eosio_install.sh
cd build/contracts
pwd
```

With the result of `pwd` you can map it on the `docker-compose.yml`:

```
  nodeosd:
    container_name: nodeosd
    ...
    volumes:
      - nodeos-data-volume:/opt/eosio/bin/data-dir
      - ../contracts:/contracts # here, change ..contracts to the result of your pwd here, before `:`
```

### Setup aliases

It can be a little tricky to interact with EOS using docker, those alias will help you around:

```
alias cleos='docker-compose -f /Users/lucca/Development/cpp/eos/Docker/docker-compose.yml exec keosd /opt/eosio/bin/cleos -u http://nodeosd:8888 --wallet-url http://localhost:8900'

alias eosiocpp='docker-compose -f /Users/lucca/Development/cpp/eos/eos/Docker/docker-compose.yml exec nodeosd /opt/eosio/bin/eosiocpp'
```
