.PHONY: build build-community build-token \
        node-start node-fresh node-stop node-reset node-status node-logs \
        bootstrap test test-only \
        clean

# ── Build ─────────────────────────────────────────────────────────────────────

build: build-community build-token

build-community:
	$(MAKE) -C community

build-token:
	$(MAKE) -C token

# ── Local node ───────────────────────────────────────────────────────────────

node-start:
	./tests/node.sh start

node-fresh:
	./tests/node.sh fresh

node-stop:
	./tests/node.sh stop

node-reset:
	./tests/node.sh reset

node-status:
	./tests/node.sh status

node-logs:
	./tests/node.sh logs

# ── Bootstrap ─────────────────────────────────────────────────────────────────
# Deploy contracts and seed chain. Run after `make node-fresh` (or `make test`).

bootstrap: build
	bash tests/bootstrap.sh

# ── Tests ─────────────────────────────────────────────────────────────────────
# Full run: reset chain, compile, deploy, run all test suites.

test: build node-fresh bootstrap
	bash tests/test_token.sh
	bash tests/test_community.sh

# Run tests against already-running bootstrapped chain (faster iteration).
test-only:
	bash tests/test_token.sh
	bash tests/test_community.sh

# ── Clean ─────────────────────────────────────────────────────────────────────

clean:
	$(MAKE) -C community clean
	$(MAKE) -C token clean
