all: 9pfs

9pfs: 9p.o 9p-rpc.o 9p-err.c main.o netfs.o
	$(CC) $(CFLAGS) $^ -o $@ -l netfs -l shouldbeinlibc -l ihash -pthread

clean:
	rm -f *.o 9pfs

.PHONY: clean
