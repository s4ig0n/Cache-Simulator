CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra

SRCS = src/main.cpp src/cache.cpp src/trace.cpp

cachesim: $(SRCS) src/cache.h src/trace.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f cachesim cachesim.exe

.PHONY: clean
