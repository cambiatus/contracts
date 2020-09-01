.PHONY: token.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) $(src:.cpp=.abi)

token.wasm: $(src)
	eosio-cpp -o $@ $^

clean:
	rm $(obj)
