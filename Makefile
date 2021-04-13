CFLAGS = -g3 -Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pedantic -std=gnu99 -Werror -D_GNU_SOURCE -std=gnu99

CC = gcc
EXECS = 33sh 33noprompt
PROMPT = -DPROMPT
.PHONY: all clean

all: $(EXECS)

33sh: sh2.c jobs.c
	$(CC) $(CFLAGS) $^ -o $@ $(PROMPT)

33noprompt: sh2.c jobs.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(EXECS)
