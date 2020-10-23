// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include "main.h"

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#include "metronomeRes.h"
#include "audioBuffer.h"
#include "stereoBuffer.h"
#include "surround.h"
#include "net.h"
#include "tty.h"

struct client {
	bool connected;
	uint8_t id;
	struct sockaddr_storage addr;
	int64_t lastPacketUsec;
	float aioLatency;
	float restLatency;
	int metrDelay;
	float dBAdj;
	bindex_t lastKeyPress;
	sample_t lastReadBlock[STEREO_BLOCK_SIZE];
	struct surroundCtx surroundCtx;
	struct audioBuffer buffer;
	char name[NAME_LEN + 1];
};
struct client *clients[MAX_CLIENTS];
struct client *clientsOrdered[MAX_CLIENTS]; // may temporarily contain duplicit/missing items

int udpSocket = -1;
pthread_t udpThread;
bindex_t blockIndex = 0;
int64_t usecZero;

struct {
	bool enabled;
	FILE *file;
	bindex_t startTime;
} recording;

struct {
	bool enabled;
	float beatsPerMinute;
	size_t beatsPerBar;
	ssize_t lastBeatIndex;
	size_t lastBeatBarIndex;
	bindex_t lastBeatTime;
	struct stereoBuffer buffer;
} metronome;

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

struct client *getClient(size_t c) {
	if (!clients[c] || !clients[c]->connected) return NULL;
	return clients[c];
}

#define FOR_CLIENTS(CLIENT) \
	for (size_t CLIENT##_INDEX = 0; CLIENT##_INDEX < MAX_CLIENTS; CLIENT##_INDEX++) \
	for (struct client *CLIENT = getClient(CLIENT##_INDEX); CLIENT; CLIENT=NULL)

#define FOR_CLIENTS_ORDERED(CLIENT) \
	for (size_t CLIENT##_INDEX = 0; CLIENT##_INDEX < MAX_CLIENTS; CLIENT##_INDEX++) \
	for (struct client *CLIENT = clientsOrdered[CLIENT##_INDEX]; CLIENT && CLIENT->connected; CLIENT=NULL)

void clientsSurroundReinit() {
	size_t clientsCnt = 0;
	FOR_CLIENTS(client) clientsCnt++;

	size_t i = 0;
	FOR_CLIENTS_ORDERED(client) {
		surroundInitCtx(&client->surroundCtx, client->dBAdj, M_PI * ((float)i++ / (clientsCnt-1 + 1e-20) - 0.5f), 2);
	}
}

struct client *newClient() {
	struct client *client = NULL;
	for (ssize_t i = 0; i < MAX_CLIENTS; i++) {
		if (!clients[i]) {
			client = malloc(sizeof(struct client));
			if (!client) {
				msg("Cannot allocate memory for a new client, refusing...");
				return NULL;
			}
			client->id = i;
			client->connected = false;
			__sync_synchronize();
			clients[i] = client;
			break;
		} else if (!clients[i]->connected) {
			client = clients[i];
			break;
		}
	}

	if (!client) {
		msg("Max number of clients (%d) exceeded, refusing new connection...", MAX_CLIENTS);
		return NULL;
	}

	size_t i = 0;
	for (size_t j = 0; j < MAX_CLIENTS; j++) {
		if (clientsOrdered[j] && clientsOrdered[j]->connected && (clientsOrdered[j] != client)) {
			if (i < j) {
				clientsOrdered[i] = clientsOrdered[j];
			}
			i++;
		}
	}
	clientsOrdered[i++] = client;
	for (; i < MAX_CLIENTS; i++) {
		clientsOrdered[i] = NULL;
	}
	__sync_synchronize();
	return client;
}

void clientMoveUp(struct client *client) {
	ssize_t i = -1;
	for (ssize_t j = 0; j < MAX_CLIENTS; j++) {
		if (clientsOrdered[j] && clientsOrdered[j]->connected) {
			if (clientsOrdered[j] == client) {
				if (i >= 0) {
					clientsOrdered[j] = clientsOrdered[i];
					clientsOrdered[i] = client;
				}
				break;
			}
			i = j;
		}
	}
	clientsSurroundReinit();
}

void clientMoveDown(struct client *client) {
	ssize_t i = -1;
	for (ssize_t j = 0; j < MAX_CLIENTS; j++) {
		if (clientsOrdered[j] && clientsOrdered[j]->connected) {
			if (i >= 0) {
				clientsOrdered[i] = clientsOrdered[j];
				clientsOrdered[j] = client;
				break;
			} else if (clientsOrdered[j] == client) {
				i = j;
			}
		}
	}
	clientsSurroundReinit();
}

ssize_t udpSendPacket(struct client *client, void *packet, size_t size) {
	return sendto(udpSocket, packet, size, 0, (struct sockaddr *)&client->addr, sizeof(client->addr));
}

void udpRecvHelo(struct client *client, struct packetClientHelo *packet) {
	strcpy(client->name, packet->name);
	bufferClear(&client->buffer, 0);
	client->lastPacketUsec = getUsec(usecZero);
	client->aioLatency = packet->aioLatency;
	client->dBAdj = packet->dBAdj;
	surroundInitCtx(&client->surroundCtx, client->dBAdj, 0, 2);
	bufferOutputStatsReset(&client->buffer, true);
	client->lastKeyPress = 0;
	client->metrDelay = 0;
	__sync_synchronize();
	client->connected = true;

	struct packetServerHelo packetR = {};
	packetR.clientID = client->id;
	packetR.initBlockIndex = blockIndex;
	strncpy(packetR.str, "durmjkhlJK\n" // +n
		"[d/u] move down/up in list\n"
		"[r]   turn recording on/off\n"
		"[m]   turn metronome on/off\n"
		"[j/k] decrease/increase beats per minute by 2\n"
		"[J/K]   ... by 20\n"
		"[h/l] decrease/increase beats per bar",
		//"[n]   set metronome by multiple presses in rhythm and turn it on",
		SHELO_STR_LEN);

	udpSendPacket(client, &packetR, (void *)strchr(packetR.str, '\0') - (void *)&packetR);
	clientsSurroundReinit();
}

void udpRecvData(struct client *client, struct packetClientData *packet) {
	client->restLatency = (float) MONO_BLOCK_SIZE / SAMPLE_RATE * 1000 *
		((int)blockIndex - packet->playBlockIndex + packet->blockIndex - client->buffer.readPos);
	bufferWrite(&client->buffer, packet->blockIndex, packet->block, false);
	client->lastPacketUsec = getUsec(usecZero);
}

void udpRecvKeyPress(struct client *client, struct packetKeyPress *packet) {
	switch (packet->key) {
		case 'u': // move up
			clientMoveUp(client);
			break;

		case 'd': // move down
			clientMoveDown(client);
			break;

		case 'r': // toggle recording
			if (!recording.enabled) {
				char filename[100];
				time_t t = time(NULL);
				strftime(filename, sizeof(filename), "rehearsal_%Y-%m-%d_%H-%M-%S." STR(SAMPLE_RATE) "s16le2ch", localtime(&t));
				recording.file = fopen(filename, "w");
				if (recording.file) {
					recording.startTime = blockIndex;
					__sync_synchronize();
					recording.enabled = true;
				}
			} else {
				recording.enabled = false;
				__sync_synchronize();
				if (recording.file) {
					fclose(recording.file);
					recording.file = NULL;
				}
			}
			break;

		case 'm': // toggle metronome
			if (metronome.enabled) {
				metronome.enabled = false;
			} else {
				metronome.lastBeatTime = 0;
				__sync_synchronize();
				metronome.enabled = true;
			}
			break;

		case 'j': // decrease beats per minute
			if (metronome.beatsPerMinute - 2 >= METR_MIN_BPM) {
				metronome.beatsPerMinute -= 2;
			}
			break;
		case 'k': // increase beats per minute
			if (metronome.beatsPerMinute + 2 <= METR_MAX_BPM) {
				metronome.beatsPerMinute += 2;
			}
			break;
		case 'J': // big decrease beats per minute
			if (metronome.beatsPerMinute - 20 >= METR_MIN_BPM) {
				metronome.beatsPerMinute -= 20;
			}
			break;
		case 'K': // big increase beats per minute
			if (metronome.beatsPerMinute + 20 <= METR_MAX_BPM) {
				metronome.beatsPerMinute += 20;
			}
			break;
		case 'h': // decrease beats per bar
			if (metronome.beatsPerBar > 0) {
				metronome.beatsPerBar--;
			}
			break;
		case 'l': // increase beats per bar
			if (metronome.beatsPerBar < METR_MAX_BPB) {
				metronome.beatsPerBar++;
			}
			break;
		case 'n': // multiple-press metronome activation
			break;
	}
}

void *udpReceiver(void *none) {
	char packetRaw[sizeof(union packet) + 1];
	union packet *packet = (union packet *) &packetRaw;
	ssize_t size;
	struct sockaddr_storage addr = {};
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

				{
					bool duplicate = false;
					FOR_CLIENTS(client2) {
						if (netAddrsEqual(&client2->addr, &addr)) {
							msg("Second helo packet from the same address refused...");
							duplicate = true;
							break;
						}
					}
					if (duplicate) break;
				}
				if (!(client = newClient())) {
					break;
				}
				client->addr = addr;
				udpRecvHelo(client, &packet->cHelo);
				msg("New client '%s' with id %d accepted...", client->name, client->id);
				break;
			case PACKET_DATA:
				if (
						(size != sizeof(struct packetClientData)) ||
						!(client = getClient(packet->cData.clientID)) ||
						!netAddrsEqual(&addr, &client->addr)
					) break;
				udpRecvData(client, &packet->cData);
				break;
			case PACKET_KEY_PRESS:
				if (
						(size != sizeof(struct packetKeyPress)) ||
						!(client = getClient(packet->cKeyP.clientID)) ||
						!netAddrsEqual(&addr, &client->addr) ||
						(client->lastKeyPress >= packet->cKeyP.playBlockIndex)
					) break;
				client->lastKeyPress = packet->cKeyP.playBlockIndex;
				msg("Key '%c' pressed by '%s'...", packet->cKeyP.key, client->name);
				udpRecvKeyPress(client, &packet->cKeyP);
				break;
		}
		__sync_synchronize();
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
		*s += sprintf(*s, " %-10s%3.0f+%-4.0fms ",
			client->name, client->aioLatency, client->restLatency);
	} else {
		*s += sprintf(*s, " %-10s  ?+%-4.0fms ",
			client->name, client->restLatency);
	}
	float avg, peak;
	bufferOutputStats(&client->buffer, &avg, &peak);
	ttyFormatSndLevel(s, avg + client->dBAdj, peak + client->dBAdj);
	*(*s)++ = '\n';
}

// init status:                  &(s=NULL), newLineClient, false
// commit line & begin new line: &s, newLineClient, false
// flush status:                 &s, NULL, true
void statusAppendLine(char **s, struct client *assignedClient, bool flush, bool log) {
	static size_t l = 0;
	static struct packetStatusStr packet = {
		.type = PACKET_STATUS,
		.packetsCnt = 255,
		.packetIndex = 0,
		.statusIndex = 0};

	static struct {
		struct client *client;
		char *s;
	} lines[STATUS_LINES_PER_PACKET];

	if (!*s) { // init
		packet.packetsCnt = 255;
		packet.packetIndex = 0;
		l = 0;
		*s = packet.str;
	} else {
		**s = '\0';
		if (log) {
			printf("%s", lines[l-1].s);
		}
		if ((l >= STATUS_LINES_PER_PACKET) || flush) { // flush
			if (flush) {
				packet.packetsCnt = packet.packetIndex + 1;
			}

			FOR_CLIENTS(client) {
				ssize_t k = -1;
				for (size_t i = 0; i < l; i++) {
					if (lines[i].client == client) {
						k = i;
						lines[i].s[0] = '.';
						break;
					}
				}
				udpSendPacket(client, &packet, (void *)*s - (void *)&packet);
				if (k >= 0) {
					lines[k].s[0] = ' ';
				}
			}

			l = 0;
			*s = packet.str;
			packet.packetIndex++;
			if (flush) {
				packet.statusIndex++;
				return;
			}
		}
	}

	lines[l].client = assignedClient;
	lines[l].s = *s;
	l++;
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

	metronome.enabled = false;
	metronome.beatsPerMinute = METR_DEFAULT_BPM;
	metronome.beatsPerBar = METR_DEFAULT_BPB;

	usecZero = getUsec(0);
	int64_t usecFreeSum = 0;
	int64_t usecFreeMin = INT64_MAX;
	struct packetServerData packet = { .type = PACKET_DATA };

	msg("Virtual Choir Rehearsal Room, server v" STR(APP_VERSION) " started.");

	while (udpState == UDP_OPEN) {
		__sync_synchronize();

		// sound mixing [

		sample_t mixedBlock[STEREO_BLOCK_SIZE];
		sample_t *block = packet.block;
		memset(mixedBlock, 0, STEREO_BLOCK_SIZE * sizeof(sample_t));
		packet.blockIndex = blockIndex;
		FOR_CLIENTS(client) {
			sample_t *clientBlock = client->lastReadBlock;
			surroundFilter(&client->surroundCtx, bufferReadNext(&client->buffer), clientBlock);
			for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++) {
				mixedBlock[i] += clientBlock[i];
			}
		}
		FOR_CLIENTS(client) {
			int playBeat = 0; // 1 for ordinary beat, 2 for main beat

			sample_t *metrBlock = NULL;
			if (metronome.enabled && metronome.lastBeatTime) {
				int delay = ((client->aioLatency > 0 ? client->aioLatency : 20) + client->restLatency) * SAMPLE_RATE / 1000 / MONO_BLOCK_SIZE;
				bool fadeIn = false, fadeOut = false;
				if (client->metrDelay == 0) {
					client->metrDelay = delay;
					fadeIn = true;
				} else {
					if ((float)abs(client->metrDelay - delay) * MONO_BLOCK_SIZE / SAMPLE_RATE * 1000 > 5) {
						fadeOut = true;
						delay = client->metrDelay;
						client->metrDelay = 0;
					} else {
						delay = client->metrDelay;
					}
				}
				metrBlock = sbufferRead(&metronome.buffer, blockIndex + delay, fadeIn, fadeOut);
			}

			sample_t *clientBlock = client->lastReadBlock;
			for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++) {
#ifdef DEBUG_HEAR_SELF
				block[i] = mixedBlock[i] + (metrBlock ? metrBlock[i] : 0);
#else
				block[i] = mixedBlock[i] - clientBlock[i] + (metrBlock ? metrBlock[i] : 0);
#endif
			}
			ssize_t err = udpSendPacket(client, &packet, sizeof(struct packetServerData));
			// ssize_t err = sendto(udpSocket, &packet, sizeof(struct packetServerData), 0,
			// 	(struct sockaddr *)&clients[c]->addr, sizeof(struct sockaddr_storage));
			if (err < 0) {
				msg("Sending to client %d '%s' failed, disconnected...", client->id, client->name);
				client->connected = false;
			}
		}

		if (recording.enabled) {
			fwrite(mixedBlock, sizeof(mixedBlock), 1, recording.file);
		}

		// ] end of sound mixing

		if (metronome.enabled) {
			bindex_t nextBeatTime;
			if (metronome.lastBeatTime == 0) {
				nextBeatTime = blockIndex + ((uint32_t) METR_DELAY_MSEC * SAMPLE_RATE / 1000 / MONO_BLOCK_SIZE);
				metronome.lastBeatIndex = -1;
				metronome.lastBeatBarIndex = -1;
				sbufferClear(&metronome.buffer, 0);
				FOR_CLIENTS(client) {
					client->metrDelay = 0;
				}
			} else {
				nextBeatTime = metronome.lastBeatTime + (float)SAMPLE_RATE / MONO_BLOCK_SIZE / metronome.beatsPerMinute * 60;
			}
			if (nextBeatTime <= blockIndex + ((uint32_t) METR_DELAY_MSEC * SAMPLE_RATE / 1000 / MONO_BLOCK_SIZE)) {
				metronome.lastBeatIndex++;
				bool mainBeat = false;
				if (metronome.beatsPerBar) {
					metronome.lastBeatBarIndex = (metronome.lastBeatBarIndex + 1) % metronome.beatsPerBar;
					mainBeat = metronome.lastBeatBarIndex == 0;
				}
				const sample_t *beat;
				size_t beatSize;
				if (mainBeat) {
					beat = (const sample_t *) metronomeRes1;
					beatSize = sizeof(metronomeRes1) / sizeof(sample_t);
				} else {
					beat = (const sample_t *) metronomeRes2;
					beatSize = sizeof(metronomeRes2) / sizeof(sample_t);
				}

				metronome.lastBeatTime = nextBeatTime;
				size_t i = 0;
				while (i < beatSize) {
					mixedBlock[i % STEREO_BLOCK_SIZE] = beat[i];
					if (++i % STEREO_BLOCK_SIZE == 0) {
						sbufferWrite(&metronome.buffer, nextBeatTime++, mixedBlock, true);
					}
				}
				if (i % STEREO_BLOCK_SIZE) {
					for (; i % STEREO_BLOCK_SIZE; i++) {
						mixedBlock[i % STEREO_BLOCK_SIZE] = 0;
					}
					sbufferWrite(&metronome.buffer, nextBeatTime++, mixedBlock, true);
				}
			}
		}
		blockIndex++;

		// status string [
		if (blockIndex % BLOCKS_PER_STAT == 0) {
			const bool log = blockIndex % BLOCKS_PER_SRV_STAT == 0;
			if (log) msg("\n");

			char *s = NULL;
			statusAppendLine(&s, NULL, false, log);
			s += sprintf(s, "---------------------  left\n");

			FOR_CLIENTS_ORDERED(client) {
				statusAppendLine(&s, client, false, log);
				getStatusStr(&s, client);
			}

			statusAppendLine(&s, NULL, false, log);
			s += sprintf(s, "---------------------  right\n");

			statusAppendLine(&s, NULL, false, log);
			*s++ = '\n';

			statusAppendLine(&s, NULL, false, log);
			s += sprintf(s, "metronome:        %3s %2d beats per bar, %3.0f beats per minute\n",
					(metronome.enabled ? "ON" : "OFF"), metronome.beatsPerBar, metronome.beatsPerMinute);

			statusAppendLine(&s, NULL, false, log);
			if (recording.enabled) {
				float durSec = (blockIndex - recording.startTime) * MONO_BLOCK_SIZE / SAMPLE_RATE;
				s += sprintf(s, "recording:         ON  %02d:%02d\n", (int)durSec / 60, (int)durSec % 60);
			} else {
				s += sprintf(s, "recording:        OFF\n");
			}

			statusAppendLine(&s, NULL, true, log);
			if (log) printf("\n");
		}
		// ] end of status string

		int64_t usec = getUsec(usecZero);
		int64_t usecWait = getBlockUsec(blockIndex) - usec;
		usecFreeMin = (usecFreeMin > usecWait ? usecWait : usecFreeMin);
		usecFreeSum += usecWait;

		if (blockIndex % BLOCKS_PER_SRV_STAT == 0) {
			printf("BLOCKS      play  lost  wait  skip  delay  metr       read    write\n", "");
			FOR_CLIENTS_ORDERED(client) {
				size_t play, lost, wait, skip;
				ssize_t delay;
				bufferSrvStatsReset(&client->buffer, &play, &lost, &wait, &skip, &delay);
				printf("%-10s %5u %5u %5u %5u %6d %5d   %8d %8d\n", client->name, play, lost, wait, skip, delay, (metronome.enabled ? client->metrDelay : 0),
						client->buffer.readPos, client->buffer.writeLastPos);
			}
			printf("\n");

			int64_t usecTot   = getBlockUsec(BLOCKS_PER_SRV_STAT);
			int64_t usecBlock = getBlockUsec(1);
			printf("Sound mixer load: %6.2f %% avg, %6.2f %% max\n\n",
					(float)(usecTot - usecFreeSum)/usecTot * 100,
					(float)(usecBlock - usecFreeMin)/usecBlock * 100);
			usecFreeSum = 0;
			usecFreeMin = INT64_MAX;
		}

		if (blockIndex % 50 == 0) {
			FOR_CLIENTS(client) {
				if (usec - client->lastPacketUsec > 1000000) {
					client->connected = false;
					msg("Client %d '%s' timeout, disconnected...", client->id, client->name);
				}
			}
		}

		if (blockIndex % 1000 == 0) {
		}

		__sync_synchronize();
		usecWait = getBlockUsec(blockIndex) - getUsec(usecZero);
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
