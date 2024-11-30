CC = gcc
CFLAGS = -O3 -msse4 -g -march=native -funroll-loops

all: attacker

attacker:
	$(CC) -DWORKLOAD_READ $(CFLAGS) attacker.c -o attacker -lpthread -lnuma


clean:
	rm -f attacker *.o