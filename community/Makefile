.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) 
OPTS = -D'__TOKEN_ACCOUNT__=cambiatus.tk'

community.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ $^

clean:
	rm $(obj)
