CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic
TARGET   = luna-boot
SRC      = luna_bootstrap.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test: $(TARGET)
	@printf 'fn main() -> int {\n    return 0\n}\n' > /tmp/hello.luna
	./$(TARGET) /tmp/hello.luna -o /tmp/hello -v
	@chmod +x /tmp/hello
	@/tmp/hello ; echo "exit=$$?"

clean:
	rm -f $(TARGET)
