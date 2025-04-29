CFLAGS = -Wall
LDFLAGS = -static
TARGET = writer

all: $(TARGET)

$(TARGET): writer.c
	$(CROSS_COMPILE)gcc $(CFLAGS) $(LDFLAGS) writer.c -o $@

clean:
	rm -f writer

.PHONY: all clean
