CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS  := -lncurses -lm

ncurses_psychedelic: ncurses_psychedelic.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f ncurses_psychedelic

.PHONY: clean
