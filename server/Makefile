CC = gcc
CFLAGS = -Wall -Werror
MAIN = aesdsocket.c
BIN = aesdsocket

all: $(BIN)

$(BIN): $(MAIN)
	$(CC) $(CFLAGS) $(MAIN) -o $(BIN)

clean:
	rm -rf $(BIN)
