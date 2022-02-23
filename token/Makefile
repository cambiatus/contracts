.PHONY: token.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm)
OPTS = -D'__COMMUNITY_ACCOUNT__=cambiatus.cm'

contract = cambiatus.tk
authorization = lucca@active
contract_authorization = $(contract)@active

url = https://staging.cambiatus.io
# url = https://demo.cambiatus.io
# url = https://app.cambiatus.io

cleos = cleos -u $(url)
push = $(cleos) push action $(contract) $^ -p $(authorization)
push_adm = $(cleos) push action $(contract) $^ -p $(contract_authorization)

token.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ $^ -abigen -R ./ricardian

clean:
	rm $(obj)

deploy:
	$(cleos) set contract $(contract) ../token

issue:
	# $(push_adm) issue '["karla", "1000 MIZU", ""]'
