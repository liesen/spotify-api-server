CC=gcc
CFLAGS=-std=c99 -Wall
SOURCES=appkey.c account.c diff.c json.c server.c

all: $(SOURCES) server

server:
	$(CC) $(CFLAGS) `apr-1-config --includes --ldflags --link-ld --cflags --cppflags` -I/usr/include/subversion-1 -lspotify -lsvn_diff-1 -lsvn_subr-1 -ljansson -levent -levent_pthreads $(SOURCES) -o $@

clean:
	rm -f *.o server
	rm -rf .settings .cache

