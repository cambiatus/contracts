.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) 
OPTS = -D'__TOKEN_ACCOUNT__=cambiatus.tk' -D'__BACKEND_ACCOUNT__=cambiatus' -D'__PROOF_EXPIRATION_SECS__=1800'
# url = https://app.cambiatus.io
url = https://staging.cambiatus.io

community.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ -abigen -R ./ricardian $^

clean:
	rm $(obj)

deploy:
	cleos -u $(url) set contract cambiatus.cm ../community