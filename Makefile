CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: p1_server p1_client p2_server p2_client

p1_server: p1_server.c
	$(CC) $(CFLAGS) -o p1_server p1_server.c

p1_client: p1_client.c
	$(CC) $(CFLAGS) -o p1_client p1_client.c

p2_server: p2_server.c
	$(CC) $(CFLAGS) -o p2_server p2_server.c

p2_client: p2_client.c
	$(CC) $(CFLAGS) -o p2_client p2_client.c

clean:
	rm -f p1_server p1_client p2_server p2_client
