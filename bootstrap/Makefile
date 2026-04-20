CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic
TARGET   = luna-boot
SRC      = luna_bootstrap.c

.PHONY: all clean test test-stdlib

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test: $(TARGET)
	@printf 'fn main() -> int {\n    return 0\n}\n' > /tmp/hello.luna
	./$(TARGET) /tmp/hello.luna -o /tmp/hello -v
	@chmod +x /tmp/hello
	@/tmp/hello ; echo "exit=$$?"

# Smoke-test every pure-Luna module under src/stdlib_new/ by compiling
# <name>.luna + <name>_test.luna and running the produced binary.
test-stdlib: $(TARGET)
	@LUNA_BOOT=./bootstrap/$(TARGET) bash ../src/stdlib_new/run_tests.sh

clean:
	rm -f $(TARGET)
