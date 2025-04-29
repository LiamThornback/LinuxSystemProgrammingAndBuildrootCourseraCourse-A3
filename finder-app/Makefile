CFLAGS = -Wall
LDFLAGS = -static
TARGET = writer
CC ?= gcc

all: $(TARGET)

$(TARGET): writer.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(LDFLAGS) writer.c -o $@

clean:
	rm -f writer

.PHONY: all clean
