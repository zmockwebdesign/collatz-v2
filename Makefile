# Collatz v2 - Build system
# GMP required for collatz_hash: brew install gmp (macOS)

CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -Wall
LDFLAGS_GMP = -lgmp -lgmpxx

# Homebrew paths (macOS) - omit if GMP is in system path
HOMEBREW_PREFIX ?= /opt/homebrew
ifeq ($(shell uname -s),Darwin)
  GMP_CXXFLAGS = -I$(HOMEBREW_PREFIX)/include
  GMP_LDFLAGS = -L$(HOMEBREW_PREFIX)/lib
endif

.PHONY: all clean benchmark test v4 crypto

all: collatz_fast_hash benchmark_all collatz_hash crf_collision collatz_crypto

# CollatzHash V4 - the main fast hash (no external deps)
collatz_fast_hash: collatz_fast_hash.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Head-to-head benchmark (no external deps)
benchmark_all: benchmark_all.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Original GMP-based hash
collatz_hash: collatz_hash.cpp
	$(CXX) $(CXXFLAGS) $(GMP_CXXFLAGS) -o $@ $< $(GMP_LDFLAGS) $(LDFLAGS_GMP)

# CRF collision tester
crf_collision: crf_collision.cpp
	$(CXX) $(CXXFLAGS) -pthread -o $@ $<

v4: collatz_fast_hash benchmark_all

# Collatz Crypto Suite (hash + AEAD + MAC, no external deps)
collatz_crypto: collatz_crypto.cpp collatz_perm.h collatz_crypto.h
	$(CXX) $(CXXFLAGS) -o $@ collatz_crypto.cpp

crypto: collatz_crypto
	@./collatz_crypto --test && ./collatz_crypto --bench

clean:
	rm -f collatz_fast_hash benchmark_all collatz_hash crf_collision collatz_crypto

benchmark: v4
	@./benchmark_all

test: collatz_fast_hash collatz_crypto
	@./collatz_fast_hash --test
	@./collatz_crypto --test
