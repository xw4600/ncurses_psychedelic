CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS  := -lncurses -lm

psychedelic: psychedelic.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f psychedelic

.PHONY: clean
