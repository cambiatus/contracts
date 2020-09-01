.PHONY: community.wasm

src = $(wildcard *.cpp)
obj = $(src:.cpp=.wasm) $(src:.cpp=.abi)

community.wasm: $(src)
	eosio-cpp -o $@ $^

clean:
	rm $(obj)
