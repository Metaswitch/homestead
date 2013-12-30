
cache_example: cache_example.o ../../build/obj/homestead/cache.o ../../build/obj/homestead/log.o ../../build/obj/homestead/logger.o
	g++ -L../../usr/lib $^ -lpthread -lthrift -lcassandra -lrt -o cache_example

cache_example.o: cache_example.cpp
	g++ -I../../include -I../../usr/include -I../../modules/cpp-common/include -c $^

clean:
	rm cache_example.o cache_example

run:
	@LD_LIBRARY_PATH=../../usr/lib/ ./cache_example

debug:
	@LD_LIBRARY_PATH=../../usr/lib/ gdb ./cache_example
