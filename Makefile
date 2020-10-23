# Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

LDFLAGS=-lportaudio -lm -pthread
CFLAGS=-g -std=gnu99

client:
all: client server client32.exe client64.exe

%: %.c *.h
	gcc $< -o $@ $(CFLAGS) $(LDFLAGS)

%32.exe: %.c *.h
	i686-w64-mingw32-gcc $< -o $@ -mthreads -lws2_32 $(LDFLAGS)
%64.exe: %.c *.h
	x86_64-w64-mingw32-gcc $< -o $@ -mthreads -lws2_32 $(LDFLAGS)

clean:
	rm -f client server client32.exe client64.exe
