CC=gcc
CFLAGS=-Wall -Wextra -pedantic -Werror -g
LIBS=-lpulse -lsndfile

pactl : pactl.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean :
	rm pactl

