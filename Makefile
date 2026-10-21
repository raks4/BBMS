# Requires OpenSSL development files (libssl-dev / openssl-devel / brew install openssl).
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
override CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
LDLIBS  ?= -lcrypto

APP      := bloodbank
LIB_SRC  := $(filter-out src/main.c,$(wildcard src/*.c))
LIB_OBJ  := $(patsubst src/%.c,build/%.o,$(LIB_SRC))
TESTS    := $(patsubst tests/%.c,build/tests/%,$(wildcard tests/test_*.c))

all: $(APP)

$(APP): build/main.o $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/tests/%: tests/%.c $(LIB_OBJ) | build/tests
	$(CC) $(CPPFLAGS) $(CFLAGS) -Itests -o $@ $< $(LIB_OBJ) $(LDLIBS)

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$$t; done; echo "All tests passed."

build build/tests:
	mkdir -p $@

asan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"

clean:
	rm -rf build $(APP)

-include $(wildcard build/*.d)
.PHONY: all test asan clean
