all: 9pfs

MIG ?= mig

IO_SRCS = $(patsubst %.c,%.o,$(wildcard io-*.c))
FSYS_SRCS = $(patsubst %.c,%.o,$(wildcard fsys-*.c))
FS_SRCS = $(patsubst %.c,%.o,$(wildcard file-*.c) $(wildcard dir-*.c))

SRCS =  9p.o 9pfs.o 9p-rpc.o 9p-err.o main.o \
	nref.o nrele.o nput.o \
	fid-alloc.o fid-free.o \
	make-node.o make-peropen.o make-protid.o \
	release-protid.o release-peropen.o \
	ioServer.o fsysServer.o fsServer.o \
	pager.o \
	$(IO_SRCS) $(FSYS_SRCS) $(FS_SRCS)

$(FSYS_SRCS) main.o: fsys_S.h
$(IO_SRCS) main.o: io_S.h
$(FS_SRCS) main.o: fs_S.h

LIBS = shouldbeinlibc ihash ports iohelp fshelp pager

9pfs: $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ -pthread $(foreach LIB,$(LIBS),-l $(LIB))

%Server.c %_S.h:
	echo "#include <hurd/"$*".defs>" | $(MIG) \
		-n -DSERVERPREFIX=S_ -imacros mig-mutate.h \
		-server $*Server.c -sheader $*_S.h \
		/dev/stdin

clean:
	rm -f *.o *Server.c *_S.h 9pfs

.PHONY: clean
