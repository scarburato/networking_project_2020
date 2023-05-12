CC:=gcc
CFLAGS:=-D_POSIX_C_SOURCE=200809L -std=c11 -Wall -O0 -g

all: build build/ds build/peer build/generator

clean:
	rm -r build/* */*.o
	
build/ds: ds/main.c ds/console_logic.o shared/utility.o shared/prompt.o shared/protocol.o shared/peers_info.o
	$(CC) $(CFLAGS) -o $@ $^

build/peer: peer/main.c peer/console_logic.o shared/protocol.o shared/prompt.o shared/peers_info.o peer/net.o shared/utility.o peer/disk.o peer/query.o
	$(CC) $(CFLAGS) -o $@ $^

build/generator: generator/main.c peer/disk.o peer/net.o shared/protocol.o shared/utility.o
	$(CC) $(CFLAGS) -o $@ $^
	
build: build/config
	mkdir -vp build
	
build/config:
	mkdir -vp build/config

ds/console_logic.o: ds/console_logic.c ds/console_logic.h
	$(CC) $(CFLAGS) -c -o $@ $<

peer/%.o: peer/%.c peer/%.h
	$(CC) $(CFLAGS) -c -o $@ $<

shared/%.o: shared/%.c shared/%.h
	$(CC) $(CFLAGS) -c -o $@ $<
