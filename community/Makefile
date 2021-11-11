.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) 
OPTS = -D'__TOKEN_ACCOUNT__=cambiatus.tk' -D'__BACKEND_ACCOUNT__=cambiatus' -D'__PROOF_EXPIRATION_SECS__=1800'

contract = cambiatus.cm
contract_authorization = $(contract)@active
authorization = lucca@active

# url = https://app.cambiatus.io
url = https://staging.cambiatus.io

cleos = cleos -u $(url) 
push_adm = $(cleos) push action $(contract) $^ -p $(contract_authorization)
push = $(cleos) push action $(contract) $^ -p $(authorization)

# eosio-cpp -w $(OPTS) -o $@ -abigen -R ./ricardian $^
community.wasm: $(src)
	eosio-cpp -w $(OPTS) -o $@ $^ --no-missing-ricardian-clause

clean:
	rm $(obj)

deploy:
	$(cleos) set contract $(contract) ../community

erase:
	$(push_adm) clean '["role", "", "0,MIZU"]'
	$(push_adm) clean '["member", "", "0,MIZU"]'


migrate:
	$(push_adm) migrateusers '["0,MIZU"]'

invite:
	$(push) netlink '["0,MIZU", "lucca", "memberdev123", "natural"]'

fill-role:
	$(push) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#ffffff", "permissions": ["invite", "claim", "order"]}'

	# Invalid color
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c489", "permissions": ["verify"]}'

	# Invalid Cambiatus permission
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c48", "permissions": ["verify", "something"]}'

	# Upsert check - color change and then permission change
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#5959a1", "permissions": ["invite", "claim", "order"]}'
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "validators", "color": "#f26c48", "permissions": ["verify"]}'



