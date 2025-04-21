CFLAGS = -Wall
LDFLAGS = -static

writer: writer.c
	$(CROSS_COMPILE)gcc $(CFLAGS) $(LDFLAGS) writer.c -o $@

clean:
	rm -f writer
