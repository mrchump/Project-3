CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: p1_server p1_client p2_server p2_client Basic_disk_storage_system disk_client random_client

p1_server: p1_server.c
	$(CC) $(CFLAGS) -o p1_server p1_server.c

p1_client: p1_client.c
	$(CC) $(CFLAGS) -o p1_client p1_client.c

p2_server: p2_server.c
	$(CC) $(CFLAGS) -o p2_server p2_server.c

p2_client: p2_client.c
	$(CC) $(CFLAGS) -o p2_client p2_client.c


Basic_disk_storage_system: Basic_disk_storage_system.c
	$(CC) $(CFLAGS) -o Basic_disk_storage_system.exe Basic_disk_storage_system.c

disk_client: disk_client.c
	$(CC) $(CFLAGS) -o disk_client.exe disk_client.c

random_client: random_client.c
	$(CC) $(CFLAGS) -o random_client.exe random_client.c

clean:
	rm -f p1_server p1_client p2_server p2_client Basic_disk_storage_system.exe disk_client.exe random_client.exe
