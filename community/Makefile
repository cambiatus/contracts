.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) 
OPTS = -D'__TOKEN_ACCOUNT__=cambiatus.tk' -D'__BACKEND_ACCOUNT__=cambiatus' -D'__PROOF_EXPIRATION_SECS__=1800'

contract = cambiatus.cm
authorization = $(contract)@active

# url = https://app.cambiatus.io
url = https://staging.cambiatus.io

# eosio-cpp -w $(OPTS) -o $@ -abigen -R ./ricardian $^
community.wasm: $(src)
	eosio-cpp -w $(OPTS) -o $@ $^

clean:
	rm $(obj)

deploy:
	cleos -u $(url) set contract $(contract) ../community

erase:
	cleos -u $(url) push action $(contract) clean '["role", "", "0,MIZU"]' -p $(authorization)

fill:
	cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#ffffff", "permissions": ["invite", "claim", "order"]}' -p lucca@active
	cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c48", "permissions": []}' -p lucca@active

	# Invalid color
	# cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c489", "permissions": ["verify"]}' -p lucca@active

	# Invalid Cambiatus permission
	# cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c48", "permissions": ["verify", "something"]}' -p lucca@active

	# Upsert check - color change and then permission change
	cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#5959a1", "permissions": ["invite", "claim", "order"]}' -p lucca@active
	cleos -u $(url) push action $(contract) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c48", "permissions": ["verify"]}' -p lucca@active

