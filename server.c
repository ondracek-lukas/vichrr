// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#include "main.h"
#include "audioBuffer.h"
#include "surround.h"
#include "net.h"
#include "tty.h"

struct client {
	bool connected;
	struct sockaddr_storage addr;
	int64_t lastPacketUsec;
	float aioLatency;
	float restLatency;
	float dBAdj;
	sample_t lastReadBlock[STEREO_BLOCK_SIZE];
	struct surroundCtx surroundCtx;
	struct audioBuffer buffer;
	char name[NAME_LEN + 1];
};
struct client *clients[MAX_CLIENTS];

int udpSocket = -1;
pthread_t udpThread;
bindex_t blockIndex = 0;
bindex_t statusIndex = 0;
int64_t usecZero;

int64_t getUsec(int64_t zero) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	return tp.tv_sec * 1000000ull + tp.tv_nsec/1000 - zero;
}


volatile enum udpState {
	UDP_OPEN,
	UDP_CLOSED
} udpState;

#define msg(...) msg2(__VA_ARGS__, "")
#define msg2(fmt, ...) { \
	int64_t usec = getUsec(usecZero); \
	int64_t ms = usec / 1000; \
	int64_t s = ms / 1000; \
	int64_t m = s / 60; \
	int64_t h = m / 60; \
	ms -= 1000 * s; s -= 60 * m; m -= 60 * h; \
	printf("[%02d:%02d:%02d.%03d] " fmt "%s\n", h, m, s, ms, __VA_ARGS__); }

void *udpReceiver(void *none) {
	char packetRaw[sizeof(union packet) + 1];
	union packet *packet = (union packet *) &packetRaw;
	ssize_t size;
	struct sockaddr_storage addr;
	struct client *client;
	socklen_t addr_len = sizeof(addr);

	while ((size = recvfrom(udpSocket, packetRaw, sizeof(union packet), 0, (struct sockaddr *)&addr, &addr_len)) >= 0) {
		switch (packetRaw[0]) {
			case PACKET_HELO:
				packetRaw[size] = '\0';
				if (packet->cHelo.version != PROT_VERSION) {
					msg("Different version connection refused (%d instead %d)...", packet->cHelo.version, PROT_VERSION);
					break;
				}
				packet->cHelo.name[NAME_LEN] = '\0';
				ssize_t freeClient = -1;
				for (ssize_t i = 0; i < MAX_CLIENTS; i++) {
					if (clients[i] && clients[i]->connected) {
						if (netAddrsEqual(&clients[i]->addr, &addr)) {
							msg("Second helo packet from the same address refused...");
							freeClient = -2;
							break;
						}
					} else if (freeClient < 0) {
						freeClient = i;
					}
				}
				if (freeClient < 0) {
					if (freeClient == -1) {
						msg("Max number of clients (%d) exceeded, refusing new connection...", MAX_CLIENTS);
					}
					break;
				}
				if (clients[freeClient]) {
					client = clients[freeClient];
				} else {
					client = malloc(sizeof(struct client));
					if (!client) {
						msg("Cannot allocate memory for a new client, refusing...");
					}
				}
				client->addr = addr;
				strcpy(client->name, packet->cHelo.name);
				bufferClear(&client->buffer, 0);
				client->lastPacketUsec = getUsec(usecZero);
				client->aioLatency = packet->cHelo.aioLatency;
				client->dBAdj = packet->cHelo.dBAdj;
				surroundInitCtx(&client->surroundCtx, client->dBAdj, 0, 2);
				bufferOutputStatsReset(&client->buffer, true);
				__sync_synchronize();
				client->connected = true;
				clients[freeClient] = client;
				packet->sHelo.clientID = freeClient;
				packet->sHelo.initBlockIndex = blockIndex;
				sendto(udpSocket, packetRaw, sizeof(struct packetServerHelo), 0, (struct sockaddr *)&addr, sizeof(addr));
				msg("New client '%s' with id %d accepted...", client->name, freeClient);
				{
					size_t clientsCnt = 0;
					for (ssize_t i = 0; i < MAX_CLIENTS; i++) {
						if (!clients[i] || !clients[i]->connected) continue;
						clientsCnt++;
					}
					size_t clientI = 0;
					for (ssize_t i = 0; i < MAX_CLIENTS; i++) {
						if (!clients[i] || !clients[i]->connected) continue;
						surroundInitCtx(&clients[i]->surroundCtx, clients[i]->dBAdj, M_PI * ((float)clientI / (clientsCnt-1) - 0.5f), 2);
						clientI++;
					}
				}

				break;
			case PACKET_DATA:
				if (
						(size != sizeof(struct packetClientData)) ||
						!clients[packet->cData.clientID] || !clients[packet->cData.clientID]->connected ||
						!netAddrsEqual(&addr, &clients[packet->cData.clientID]->addr)
					) break;
				client = clients[packet->cData.clientID];
				client->restLatency = (float) MONO_BLOCK_SIZE / SAMPLE_RATE * 1000 *
					((int)blockIndex - packet->cData.recBlockIndex + packet->cData.blockIndex - client->buffer.readPos); // XXX check
				bufferWrite(&client->buffer, packet->cData.blockIndex, packet->cData.block);
				client->lastPacketUsec = getUsec(usecZero);
				break;
		}
		addr_len = sizeof(addr);
	}
	msg("UDP receiver error.");
	udpState = UDP_CLOSED;
}

int64_t getBlockUsec(bindex_t index) {
	return (int64_t)index * 1000000 * MONO_BLOCK_SIZE / SAMPLE_RATE;
}

void getStatusStr(char **s, struct client *client) {
	if (client->aioLatency > 0) {
		*s += sprintf(*s, "%-10s%3.0f+%-4.0fms ",
			client->name, client->aioLatency, client->restLatency);
	} else {
		*s += sprintf(*s, "%-10s  ?+%-4.0fms ",
			client->name, client->restLatency);
	}
	float avg, peak;
	bufferOutputStats(&client->buffer, &avg, &peak);
	ttyFormatSndLevel(s, avg + client->dBAdj, peak + client->dBAdj);
	*(*s)++ = '\n';
}

#define ERR(...) {msg(__VA_ARGS__); return 1; }
int main() {
	netInit();
	udpSocket = netOpenPort(STR(UDP_PORT));
	if (udpSocket < 0) {
		ERR("Cannot open port.");
	}

	udpState = UDP_OPEN;
	pthread_create(&udpThread, NULL, &udpReceiver, NULL);

	usecZero = getUsec(0);
	int64_t usecFreeSum = 0;
	struct packetServerData packet = { .type = PACKET_DATA };

	while (udpState == UDP_OPEN) {
		__sync_synchronize();

		// sound mixing [

		sample_t mixedBlock[STEREO_BLOCK_SIZE];
		sample_t *block = packet.block;
		memset(mixedBlock, 0, STEREO_BLOCK_SIZE * sizeof(sample_t));
		packet.blockIndex = blockIndex;
		for (size_t c = 0; c < MAX_CLIENTS; c++) {
			if (clients[c] && clients[c]->connected) {
				sample_t *clientBlock = clients[c]->lastReadBlock;
				surroundFilter(&clients[c]->surroundCtx, bufferReadNext(&clients[c]->buffer), clientBlock);
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++) {
					mixedBlock[i] += clientBlock[i];
				}
			}
		}
		for (size_t c = 0; c < MAX_CLIENTS; c++) {
			if (clients[c] && clients[c]->connected) {
				sample_t *clientBlock = clients[c]->lastReadBlock;
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++) {
					block[i] = mixedBlock[i] - clientBlock[i];
				}
				ssize_t err = sendto(udpSocket, &packet, sizeof(struct packetServerData), 0,
					(struct sockaddr *)&clients[c]->addr, sizeof(struct sockaddr_storage));
				if (err < 0) {
					msg("Sending to client %d '%s' failed, disconnected...", c, clients[c]->name);
					clients[c]->connected = false;
				}
			}
		}

		// ] end of sound mixing


		int64_t usec = getUsec(usecZero);
		int64_t usecWait = getBlockUsec(++blockIndex) - usec;
		usecFreeSum += usecWait;
		if (blockIndex % 50 == 0) {
			for (size_t c = 0; c < MAX_CLIENTS; c++) {
				if (clients[c] && clients[c]->connected && (usec - clients[c]->lastPacketUsec > 1000000)) {
					clients[c]->connected = false;
					msg("Client %d '%s' timeout, disconnected...", c, clients[c]->name);
				}
			}
		}
		if (blockIndex % BLOCKS_PER_STAT == 0) {
			struct packetStatusStr packet = {
				.type = PACKET_STATUS,
				.packetsCnt = 0,
				.statusIndex = statusIndex++};
			packet.packetsCnt = 0;
			for (size_t c = 0; c < MAX_CLIENTS; c++) {
				if (!clients[c] || !clients[c]->connected) continue;
				packet.packetsCnt++;
			}
			packet.packetsCnt = (packet.packetsCnt + 3) / STATUS_LINES_PER_PACKET;
			size_t c1 = 0;
			for (packet.packetIndex = 0; packet.packetIndex < packet.packetsCnt; packet.packetIndex++) {
				char *s = packet.str;
				for (int i = 0; i < STATUS_LINES_PER_PACKET; i++) {
					while (c1 < MAX_CLIENTS && (!clients[c1] || !clients[c1]->connected)) c1++;
					if (c1 >= MAX_CLIENTS) break;
					getStatusStr(&s, clients[c1]);
					c1++;
				}
				*s='\0';

				for (size_t c = 0; c < MAX_CLIENTS; c++) {
					if (clients[c] && clients[c]->connected) {
						ssize_t err = sendto(udpSocket, (void *)&packet, (void *)strchr(packet.str, '\0') - (void *)&packet, 0,
							(struct sockaddr *) &clients[c]->addr, sizeof(struct sockaddr_storage));
						/*
						if (err < 0) {
							msg("Sending to client %d '%s' failed, disconnected...", c, clients[c]->name);
							clients[c]->connected = false;
						}
						*/
					}
				}
			}
		}
		if (blockIndex % 1000 == 0) {
			msg("Sound mixer load: %6.2f %%", (float)(getBlockUsec(1000) - usecFreeSum)/getBlockUsec(1000) * 100);
			usecFreeSum = 0;
		}
		__sync_synchronize();
		if (usecWait > 0) {
			usleep(usecWait);
		} else {
			msg("Sound mixer was late by %lld us...", -usecWait);
		}
	}

	pthread_join(udpThread, NULL);
	netCleanup();
	msg("Exitting...");

	return 0;
 }
