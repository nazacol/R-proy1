CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
.PHONY: all clean test
all: server client
server: server.cpp common.hpp
	$(CXX) $(CXXFLAGS) server.cpp -o server
client: client.cpp common.hpp
	$(CXX) $(CXXFLAGS) client.cpp -o client
test: all
	python3 tests/test_project.py
clean:
	rm -f server client
