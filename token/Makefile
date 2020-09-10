.PHONY: token.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm)
OPTS = -D'__COMMUNITY_ACCOUNT__=cambiatus.cm'

token.wasm: $(src)
	eosio-cpp $(OPTS) -o $@ $^
	# eosio-cpp $(OPTS) -o $@ $^ -abigen -R ./ricardian

clean:
	rm $(obj)
