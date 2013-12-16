
cache_example: cache_example.o ../../build/obj/homestead/cache.o
	g++ -L../../usr/lib $^ -lpthread -lthrift -lcassandra -o cache_example

cache_example.o: cache_example.cpp
	g++ -I../../include -I../../usr/include -c $^

clean:
	rm cache_example.o cahce_example

run:
	@LD_LIBRARY_PATH=../../usr/lib/ ./cache_example

debug:
	@LD_LIBRARY_PATH=../../usr/lib/ gdb ./cache_example
