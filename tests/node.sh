#!/usr/bin/env bash
# Manage a local nodeos instance via Docker for development and testing.
# Usage: ./tests/node.sh {start|fresh|stop|reset|wait|logs|status}
set -euo pipefail

CONTAINER_NAME="cambiatus-nodeos"
VOLUME_NAME="cambiatus-nodeos-data"
NODE_URL="http://127.0.0.1:8888"
EOSIO_IMAGE="eosio/eosio:v2.1.0"

function node_run() {
    local extra_args="${1:-}"
    # Store chain data in a Docker named volume (native OrbStack ext4), not a
    # host bind mount. Bind mounts go through virtiofs, whose stat-cache lag
    # makes nodeos' create_directories('/data/nodeos') intermittently throw
    # EEXIST on a freshly-wiped dir. A named volume sidesteps virtiofs entirely.
    docker volume create "$VOLUME_NAME" > /dev/null
    echo "Starting nodeos..."
    # shellcheck disable=SC2086
    docker run -d \
        --name "$CONTAINER_NAME" \
        -p 8888:8888 \
        -p 9876:9876 \
        -v "$VOLUME_NAME:/data" \
        "$EOSIO_IMAGE" \
        nodeos -e -p eosio \
            --data-dir /data/nodeos \
            --plugin eosio::producer_plugin \
            --plugin eosio::producer_api_plugin \
            --plugin eosio::chain_api_plugin \
            --plugin eosio::history_plugin \
            --plugin eosio::history_api_plugin \
            --plugin eosio::http_plugin \
            --http-server-address=0.0.0.0:8888 \
            --access-control-allow-origin='*' \
            --contracts-console \
            --http-validate-host=false \
            --verbose-http-errors \
            --filter-on='*' \
            --disable-replay-opts \
            --wasm-runtime=eos-vm \
            --max-transaction-time=5000 \
            --resource-monitor-space-threshold=99 \
            $extra_args \
        > /dev/null
    node_wait
}

function node_start() {
    if node_running; then
        echo "nodeos already running (container $CONTAINER_NAME)"
        return 0
    fi
    node_run
}

function node_fresh() {
    echo "Starting fresh nodeos (erasing all chain data)..."
    node_stop
    docker volume rm "$VOLUME_NAME" > /dev/null 2>&1 || true
    # --delete-all-blocks is belt-and-suspenders: wipes any stale state if a
    # leftover volume somehow survived removal.
    node_run "--delete-all-blocks"
}

function node_stop() {
    if docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .; then
        docker stop "$CONTAINER_NAME" > /dev/null
        echo "Stopped nodeos (container $CONTAINER_NAME)"
    fi
    docker rm -f "$CONTAINER_NAME" > /dev/null 2>&1 || true
    # Kill any local nodeos that might be holding port 8888
    pkill -x nodeos 2>/dev/null || true
}

function node_reset() {
    node_stop
    node_fresh
}

function node_running() {
    docker ps -q --filter "name=^${CONTAINER_NAME}$" | grep -q .
}

function node_wait() {
    echo -n "Waiting for nodeos to be ready"
    for _ in $(seq 1 40); do
        if curl -s "$NODE_URL/v1/chain/get_info" > /dev/null 2>&1; then
            echo " ready."
            return 0
        fi
        echo -n "."
        sleep 1
    done
    echo ""
    echo "ERROR: nodeos did not start in time. Last log lines:"
    docker logs --tail 20 "$CONTAINER_NAME" 2>/dev/null || true
    exit 1
}

function node_status() {
    if node_running; then
        local info
        info=$(curl -s "$NODE_URL/v1/chain/get_info" 2>/dev/null | jq -r '"head_block=\(.head_block_num) lib=\(.last_irreversible_block_num)"' 2>/dev/null || echo "unreachable")
        echo "nodeos running (container $CONTAINER_NAME) — $info"
    else
        echo "nodeos not running"
    fi
}

case "${1:-help}" in
    start)   node_start ;;
    fresh)   node_fresh ;;
    stop)    node_stop ;;
    reset)   node_reset ;;
    wait)    node_wait ;;
    logs)    docker logs -f "$CONTAINER_NAME" ;;
    status)  node_status ;;
    *)
        echo "Usage: $0 {start|fresh|stop|reset|wait|logs|status}"
        exit 1
        ;;
esac
