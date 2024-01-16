CC = gcc
CFLAGS = -Wall -g
LIBS = -lm -pthread -lrt -lzmq
DEFS = -D ZMQ_AVAILABLE=1
TARGETS = $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) $(DEFS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGETS)
