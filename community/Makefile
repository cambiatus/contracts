.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) 
OPTS = -D'__TOKEN_ACCOUNT__=cambiatus.tk' -D'__BACKEND_ACCOUNT__=cambiatus' -D'__PROOF_EXPIRATION_SECS__=1800'

community.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ -abigen -R ./ricardian $^

clean:
	rm $(obj)
