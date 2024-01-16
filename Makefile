CC = gcc
CFLAGS = -Wall -g
LIBS = -lm -pthread -lrt -lzmq
DEFS = -D ZMQ_AVAILABLE=0
TARGETS = $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGETS)
