# Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

LDFLAGS-client=-lportaudio
CFLAGS-server=-O3
override LDFLAGS += -lm -pthread
override CFLAGS += -std=gnu99

client:
all: client server client32.exe client64.exe

server: server.c *.h
	$(CC) $< -o $@ $(CFLAGS) $(CFLAGS-server) $(LDFLAGS)

%: %.c *.h
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LDFLAGS-client)

%32.exe: %.c *.h
	i686-w64-mingw32-gcc $< -o $@ -mthreads -lws2_32 $(CFLAGS) $(LDFLAGS) $(LDFLAGS-client)
%64.exe: %.c *.h
	x86_64-w64-mingw32-gcc $< -o $@ -mthreads -lws2_32 $(CFLAGS) $(LDFLAGS) $(LDFLAGS-client)

clean:
	rm -f client server client32.exe client64.exe
