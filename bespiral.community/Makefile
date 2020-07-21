.PHONY: bespiral.community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) $(src:.cpp=.abi)

bespiral.community.wasm: $(src)
	eosio-cpp -o $@ $^
	eosio-abigen $^ --contract=bespiral.community --output $(src:.cpp=.abi)

clean:
	rm $(obj)
