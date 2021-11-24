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
	# $(push_adm) clean '["member", "", "0,MIZU"]'


migrate-users:
	$(push_adm) migrateusers '["0,MIZU"]'

migrate-obj:
	$(push_adm) migrateobj '["0,AMTA"]'
	$(push_adm) migrateobj '["0,AMTB"]'

invite:
	$(push) netlink '["0,MIZU", "lucca", "memberdev123", "natural"]'

fill-role:
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#ffffff", "permissions": ["invite", "claim", "order"]}'
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "member", "color": "#ffffff", "permissions": ["invite", "order"]}'
	# $(push) upsertrole '{"community_id": "0,MIZU", "name": "enabler", "color": "#ffffff", "permissions": ["award", "verify"]}'
	$(push) upsertrole '{"community_id": "0,MIZU", "name": "validator", "color": "#f26c48", "permissions": ["verify"]}'

assign-role:
	$(push) assignrole '{"community_id": "0,MIZU", "member": "lucca", "roles": ["member", "validator"]} '

objective:
	$(push) upsertobjctv '["0,MIZU", 0, "## New community goal", "lucca"]'

action:
	$(push) upsertaction '["0,MIZU", 0, 94 , "New claimable action", "30 MIZU", "3 MIZU", 0, 0, 0, 3, "claimable", "lucca-karla-henriquebuss", 0, "lucca", 0, 0, ""]'
	$(push) upsertaction '["0,MIZU", 0, 94 , "Automatic action", "30 MIZU", "3 MIZU", 0, 0, 0, 3, "automatic", "lucca-karla-henriquebuss", 0, "lucca", 0, 0, ""]'

reward:
	$(push) reward '["0,MIZU", 172, "karla", "lucca"]'

claim:
	$(push) claimaction '["0,MIZU", 171, "lucca", "", "", 0]'

verify:
	$(push) verifyclaim '["0,MIZU", 336, "lucca", 1]'