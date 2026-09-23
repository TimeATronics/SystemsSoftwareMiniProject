CC      = gcc
CFLAGS  = -std=c17 -D_GNU_SOURCE -Wall -Wextra -O2
INC     = -Iinclude

COMMON  = src/common/db.c src/common/ipc.c src/common/util.c src/common/net.c
SERVER  = src/server/server.c src/server/dispatch.c src/server/auth.c \
          src/server/accounts.c src/server/transactions.c src/server/customers.c \
          src/server/loans.c src/server/feedback.c src/server/admin.c
CLIENT  = src/client/client.c src/common/net.c

all: server client

server: $(SERVER) $(COMMON) $(wildcard include/*.h)
	$(CC) $(CFLAGS) $(INC) -pthread -o server $(SERVER) $(COMMON)

client: $(CLIENT) $(wildcard include/*.h)
	$(CC) $(CFLAGS) $(INC) -o client $(CLIENT)

clean:
	rm -f server client
	rm -rf data
