.PHONY: bespiral.token.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) $(src:.cpp=.abi)

bespiral.token.wasm: $(src)
	eosio-cpp -o $@ $^
	eosio-abigen $^ --contract=bespiral.token --output $(src:.cpp=.abi)

clean:
	rm $(obj)
