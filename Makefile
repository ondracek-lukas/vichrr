# Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

FLAGS-client=-std=c++17 `wx-config --libs` `wx-config --cxxflags` -lportaudio
FLAGS-client-exe=-std=c++17 -lm -mthreads -lws2_32 -lportaudio
FLAGS-server=-O3 -march=native

override LDFLAGS += -lm -pthread

client:
all: client server client32.exe client64.exe

# server: server.c *.h
# 	$(CC) $< -o $@ $(CFLAGS) $(CFLAGS-server) $(LDFLAGS)

client: src/client.cpp src/*.hpp tmp/lang.gen.hpp
	$(CXX) $< -o $@ $(CXXFLAGS) $(FLAGS-client) $(LDFLAGS)


client.exe: src/client.cpp src/*.hpp tmp/lang.gen.hpp
	x86_64-w64-mingw32-g++ $< -o $@ $(FLAGS-client-exe)


tmp/lang.gen.hpp: src/langGen.pl lang/*
	mkdir -p tmp
	src/langGen.pl lang/* > $@

clean:
	rm -f client server client32.exe client64.exe
	rm -rf tmp




