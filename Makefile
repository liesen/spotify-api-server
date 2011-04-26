CC=gcc
CFLAGS=-std=c99 -Wall `apr-1-config --cflags --cppflags`
SOURCES=appkey.c account.c diff.c server.c

all: $(SOURCES) server

server:
	$(CC) $(CFLAGS) -ggdb -I/usr/include/apr-1 -I/usr/include/subversion-1 -lspotify -lapr-1 -lsvn_diff-1 -lsvn_subr-1 -ljansson -levent -levent_pthreads $(SOURCES) -o $@ 

clean:
	rm -f *.o server
	rm -rf .settings .cache

