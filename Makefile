CFLAGS = -std=c99 -Wall -I/usr/include/subversion-1 $(shell apr-1-config --includes)
LDLIBS = -lspotify -levent -levent_pthreads -ljansson -lsvn_diff-1 -lsvn_subr-1 $(shell apr-1-config --link-ld --libs)

SOURCES = appkey.c account.c diff.c json.c server.c

override CFLAGS += $(shell apr-1-config --cflags)
override CPPFLAGS += $(shell apr-1-config --cppflags)
override LDFLAGS += $(shell apr-1-config --ldflags)

all: server

server:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SOURCES) $(LDFLAGS) -o $@ $(LDLIBS)

clean:
	rm -f *.o server
	rm -rf .settings .cache

