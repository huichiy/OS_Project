CC = gcc
CFLAGS = -Wall -Iinclude -g
LDFLAGS = -lpthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lrt
endif

all: server client

server: src/server.o src/game_logic.o
	$(CC) -o server src/server.o src/game_logic.o $(LDFLAGS)

client: src/client.o
	$(CC) -o client src/client.o $(LDFLAGS)

src/server.o: src/server.c include/common.h include/game_logic.h
	$(CC) $(CFLAGS) -c src/server.c -o src/server.o

src/client.o: src/client.c include/common.h
	$(CC) $(CFLAGS) -c src/client.c -o src/client.o

src/game_logic.o: src/game_logic.c include/common.h include/game_logic.h
	$(CC) $(CFLAGS) -c src/game_logic.c -o src/game_logic.o

clean:
	rm -f src/*.o server client game_log.txt
