.PHONY: token.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm)
OPTS = -D'__COMMUNITY_ACCOUNT__=cambiatus.cm'

contract = cambiatus.tk
url = https://staging.cambiatus.io

cleos = cleos -u $(url)

token.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ $^ -abigen -R ./ricardian

clean:
	rm $(obj)

deploy:
	$(cleos) set contract $(contract) ../token
