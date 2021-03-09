// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include "main.h"

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <float.h>

#include "metronomeRes.h"
#include "audioBuffer.h"
#include "stereoBuffer.h"
#include "surround.h"
#include "net.h"
#include "tty.h"
#include "threadPriority.h"

struct client {
	bool connected;       // udp thread can set, main thread can unset
	bool connectedMain;   // main thread can change to equal connected
	bool connectedStatus; // status thread can change to equal connected
		// all connected* must be unset before reusing
	uint8_t id;
	struct sockaddr_storage addr;
	int64_t lastPacketUsec;
	float aioLatency;
	float restLatency;
	float restLatencyAvg;
	int leadingDelay;
	float dBAdj;
	bool muted;
	bool mutedMic;
	bool isLeader;
	bindex_t lastKeyPressIndex;
	bindex_t lastKeyPress;
	sample_t lastReadBlock[STEREO_BLOCK_SIZE];
	struct surroundCtx surroundCtx;
	struct audioBuffer buffer;
	struct packetStatusStr statusPacket;
	char *statusPacketPos;
	char name[NAME_LEN + 1];
};
struct client *clients[MAX_CLIENTS];
struct client *clientsOrdered[MAX_CLIENTS]; // may temporarily contain duplicit/missing items

int udpSocket = -1;
pthread_t udpThread;
bindex_t blockIndex = 0;
int64_t usecZero;

uint32_t schedPolicy;

struct {
	bool enabled;
	bool inclLeader;
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
} metronome;

struct {
	int delay;
	int newDelay;
	bool fadeIn;
	struct stereoBuffer buffer;
} leading;

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
	printf("[%02d:%02d:%02d.%03d] " fmt "%s\n", (int)(h), (int)(m), (int)(s), (int)(ms), __VA_ARGS__); }

inline struct client *getClient(size_t c) {
	struct client *client = clients[c];
	if (!client || !client->connected) return NULL;
	__sync_synchronize();
	return client;
}

inline struct client *getClientOrdered(size_t c) {
	struct client *client = clientsOrdered[c];
	if (!client || !client->connected) return NULL;
	__sync_synchronize();
	return client;
}
#define CLIENT_CONNECTED_FIELD connected

#define FOR_CLIENTS(CLIENT) \
	for (size_t CLIENT##_INDEX = 0; CLIENT##_INDEX < MAX_CLIENTS; CLIENT##_INDEX++) \
	for (struct client *CLIENT = clients[CLIENT##_INDEX]; CLIENT && CLIENT->CLIENT_CONNECTED_FIELD; CLIENT=NULL)

#define FOR_CLIENTS_ORDERED(CLIENT) \
	for (size_t CLIENT##_INDEX = 0; CLIENT##_INDEX < MAX_CLIENTS; CLIENT##_INDEX++) \
	for (struct client *CLIENT = clientsOrdered[CLIENT##_INDEX]; CLIENT && CLIENT->CLIENT_CONNECTED_FIELD; CLIENT=NULL)

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
			client->connectedMain = false;
			client->connectedStatus = false;
			__sync_synchronize();
			clients[i] = client;
			break;
		} else if (!clients[i]->connected && !clients[i]->connectedStatus) {
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
	sprintf(client->name, "%-" STR(NAME_LEN) "s", packet->name);

	bufferClear(&client->buffer, 0);
	client->lastPacketUsec = getUsec(usecZero);
	client->aioLatency = packet->aioLatency;
	client->dBAdj = packet->dBAdj;
	client->muted = false;
	client->mutedMic = false;
	surroundInitCtx(&client->surroundCtx, client->dBAdj, 0, 2);
	bufferOutputStatsReset(&client->buffer, true);
	client->lastKeyPress = 0;
	client->lastKeyPressIndex = 0;
	client->leadingDelay = 0;
	client->restLatencyAvg = FLT_MAX;
	__sync_synchronize();
	client->connected = true;

	struct packetServerHelo packetR = {};
	packetR.clientID = client->id;
	packetR.initBlockIndex = blockIndex;
	strncpy(packetR.str, "durRmjkhlJKLA-+",
		SHELO_STR_LEN);

	udpSendPacket(client, &packetR, (void *)strchr(packetR.str, '\0') - (void *)&packetR);
	clientsSurroundReinit();
}

void udpRecvData(struct client *client, struct packetClientData *packet) {
	client->restLatency = (float) MONO_BLOCK_SIZE / SAMPLE_RATE * 1000 *
		((int)blockIndex - packet->playBlockIndex + packet->blockIndex - client->buffer.readPos);
	if (client->restLatencyAvg == FLT_MAX) {
		client->restLatencyAvg = client->restLatency;
		client->mutedMic = false;
	} else {
		client->restLatencyAvg = STAT_LATENCY_MULTIPLIER * client->restLatencyAvg + (1 - STAT_LATENCY_MULTIPLIER) * client->restLatency;
	}
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

		case 'r': // toggle recording incl. leader
		case 'R': // ... excl. leader
			if (!recording.enabled) {
				char filename[100];
				time_t t = time(NULL);
				strftime(filename, sizeof(filename), "rehearsal_%Y-%m-%d_%H-%M-%S." STR(SAMPLE_RATE) "s16le2ch", localtime(&t));
				recording.file = fopen(filename, "w");
				if (recording.file) {
					recording.startTime = blockIndex;
					recording.inclLeader = (packet->key == 'r');
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
				FOR_CLIENTS(client) client->isLeader = false;
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
		case 'A': // mute incoming audio
			client->muted ^= 1;
			break;
		case '-': // decrease microphone volume
			client->dBAdj -= 2;
			clientsSurroundReinit();
			break;
		case '+': // increase microphone volume
			client->dBAdj += 2;
			clientsSurroundReinit();
			break;
		case 'L': // toggle leadership
			if (client->isLeader) {
				client->isLeader = false;
			} else {
				FOR_CLIENTS(client) client->isLeader = false;
				client->isLeader = true;
				metronome.enabled = false;
			}
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

	if (schedPolicy != SCHED_OTHER) {
		threadPriorityRealtime(1);
	} else {
		threadPriorityNice(1);
	}

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
						(client->lastKeyPressIndex >= packet->cKeyP.keyPressIndex)
					) break;
				client->lastKeyPress = packet->cKeyP.playBlockIndex;
				client->lastKeyPressIndex = packet->cKeyP.keyPressIndex;
				msg("Key '%c' pressed by '%s'...", packet->cKeyP.key, client->name);
				client->lastPacketUsec = getUsec(usecZero);
				udpRecvKeyPress(client, &packet->cKeyP);
				break;
			case PACKET_NOOP:
				if (
						(size != sizeof(struct packetClientNoop)) ||
						!(client = getClient(packet->cKeyP.clientID)) ||
						!netAddrsEqual(&addr, &client->addr)
					) break;
				client->lastPacketUsec = getUsec(usecZero);
				client->restLatency = FLT_MAX;
				client->restLatencyAvg = FLT_MAX;
				client->mutedMic = true;
				break;
		}
		__sync_synchronize();
		addr_len = sizeof(addr);
	}
	msg("UDP receiver error.");
	udpState = UDP_CLOSED;
	return NULL;
}

#undef CLIENT_CONNECTED_FIELD
#define CLIENT_CONNECTED_FIELD connectedStatus

int64_t getBlockUsec(bindex_t index) {
	return (int64_t)index * 1000000 * MONO_BLOCK_SIZE / SAMPLE_RATE;
}


pthread_t statusThread;
int statusLines = -1;
int64_t statusSleepPoints = 1;
bindex_t statusIndex = 0;
void statusSleepPoint(bool last) {
	static int cnt = 1;
	static int i = 0;
	usleep(1000000ull * BLOCKS_PER_STAT * MONO_BLOCK_SIZE / SAMPLE_RATE / cnt);
	i++;
	if (last) {
		cnt = i;
		i = 0;
	}

}
void statusAppend(struct client *client, char *s) {
	while (*s) *client->statusPacketPos++ = *s++;
}
void statusLineSep(bool last) { // calls usleep
	if (statusLines < 0) { // init
		statusLines = 0;
		FOR_CLIENTS(client) {
			client->statusPacket = (struct packetStatusStr) {
				.type = PACKET_STATUS,
				.packetsCnt = 255,
				.packetIndex = 0,
				.statusIndex = statusIndex};
			client->statusPacketPos = client->statusPacket.str;
		}
		statusIndex++;
	} else {
		if (!last) {
			FOR_CLIENTS(client) {
				statusAppend(client, "\n");
			}
		}
		statusLines++;
	}
	if ((statusLines >= STATUS_LINES_PER_PACKET) || last) {
		FOR_CLIENTS(client) {
			if (last) {
				client->statusPacket.packetsCnt = client->statusPacket.packetIndex + 1;
			}
			udpSendPacket(client, &client->statusPacket, (void *)client->statusPacketPos - (void *)&client->statusPacket);
			client->statusPacketPos = client->statusPacket.str;
			client->statusPacket.packetIndex++;
		}
		statusLines = last ? -1 : 0;
		statusSleepPoint(last);
	}
}

void *statusWorker(void *nothing) {
	if (schedPolicy == SCHED_OTHER) {
		threadPriorityNice(19);
	}
	while (udpState == UDP_OPEN) {
		__sync_synchronize();

		{
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (clients[i] && clients[i]->connected) clients[i]->connectedStatus = true;
			}
			__sync_synchronize();
			for (int i = 0; i < MAX_CLIENTS; i++) {
				if (clients[i] && !clients[i]->connected) clients[i]->connectedStatus = false;
			}
			int connectedCnt = 0;
			FOR_CLIENTS(c) connectedCnt++;
			if (connectedCnt == 0) {
				statusSleepPoint(true);
				continue;
			}
		}

		char str[80]; // temporary string, at most one line
		bool statusLog = statusIndex % (BLOCKS_PER_SRV_STAT/BLOCKS_PER_STAT) == 0;
		if (statusLog) msg("\n");

#define LN        statusLineSep(false); FOR_CLIENTS(C)
#define TXT(STR)  statusAppend(C, STR)

		LN TXT("---------------------  left");

		FOR_CLIENTS_ORDERED(client) {
			char *s = str;
			s += sprintf(s, "%-10s", client->name);
			if (client->aioLatency > 0) {
				s += sprintf(s, "%3.0f+", client->aioLatency);
			} else {
				s += sprintf(s, "  ?+");
			}
			if ((client->restLatencyAvg < 10000) && !client->muted) {
				s += sprintf(s, "%-4.0fms ", client->restLatencyAvg);
			} else {
				s += sprintf(s, "?   ms ");
			}
			*s++ = client->isLeader ? 'L' : ' ';

			float avg, peak;
			bufferOutputStats(&client->buffer, &avg, &peak);
			ttyFormatSndLevel(&s, avg + client->dBAdj, peak + client->dBAdj);

			if (statusLog) {
				printf("%s\n", str);
			}

			LN {
				TXT(C == client ? "." : " ");
				TXT(str);
			}
		}
		if (statusLog) { printf("\n"); }


		/*
		for (int i = 0; i < 15; i++) {
			LN TXT("fake user");
		}
		*/

		LN TXT("---------------------  right");
		LN TXT("");
		LN TXT("[d/u] move down/up in list");
		LN TXT("[+/-] decrease/increase microphone volume by 2 dB");

		LN {
			TXT("[M]   ");
			TXT(!C->mutedMic ? "mute microphone  " : "unmute microphone");
			TXT("   [A] ");
			TXT(!C->muted ? "mute incoming audio" : "unmute incoming audio");
		}

		LN;
		{ // leading track
			char *leadingTrackName = NULL;
			if (metronome.enabled) {
				leadingTrackName = "metronome ";
			} else {
				FOR_CLIENTS(client) {
					if (client->isLeader) leadingTrackName = client->name;
				}
			}
			char delayStr[6];
			snprintf(delayStr, 6, "%4lu", (uint64_t)leading.delay * MONO_BLOCK_SIZE * 1000 / SAMPLE_RATE);
			LN {
				TXT("Leading track:  ");
				if (leadingTrackName) {
					TXT(C->isLeader ? "you       " : leadingTrackName);
					TXT(delayStr);
					TXT(" ms");
				} else {
					TXT("-                ");
				}
				TXT("     [L] ");
				TXT(C->isLeader ? "cease leadership" : "become leader   ");
				TXT("  [m] ");
				TXT(metronome.enabled ? "stop metronome" : "start metronome");
			}
		}

		LN;
		sprintf(str, "%3lu", metronome.beatsPerBar);
		LN {
			TXT("Metronome:     ");
			TXT(str);
			TXT(" beats per bar      [h/l] -/+ 1");
		}

		sprintf(str, "%3.0f", metronome.beatsPerMinute);
		LN {
			TXT("               ");
			TXT(str);
			TXT(" beats per minute   [j/k] -/+ 2    [J/K] -/+ 20");
		}


		LN;
		if (recording.enabled) {
			float durSec = (blockIndex - recording.startTime) * MONO_BLOCK_SIZE / SAMPLE_RATE;
			sprintf(str, "%02d:%02d  %s. leader    [r/R] stop", (int)durSec / 60, (int)durSec % 60, recording.inclLeader ? "incl" : "excl");
		} else {
			sprintf(str, "%19s    [r/R] start incl./excl. leading track", "");
		}
		LN {
			TXT("Recording:     ");
			TXT(str);
		}

#undef TXT
#undef LN

		statusLineSep(true);
		if (statusLog) printf("\n");

		if (statusLog) {
			printf("BLOCKS      play  lost  wait  skip  delay  lead       read    write\n");
			FOR_CLIENTS_ORDERED(client) {
				size_t play, lost, wait, skip;
				ssize_t delay;
				bufferSrvStatsReset(&client->buffer, &play, &lost, &wait, &skip, &delay);
				printf("%-10s %5zu %5zu %5zu %5zu %6zd %5d   %8d %8d\n", client->name, play, lost, wait, skip, delay, client->leadingDelay,
						client->buffer.readPos, client->buffer.writeLastPos);
			}
			printf("\n");
		}
	}
	return NULL;
}

#undef CLIENT_CONNECTED_FIELD
#define CLIENT_CONNECTED_FIELD connectedMain

#define ERR(...) {msg(__VA_ARGS__); return 1; }
int main() {
	usecZero = getUsec(0);
	netInit();
	udpSocket = netOpenPort(STR(UDP_PORT));
	if (udpSocket < 0) {
		ERR("Cannot open port.");
	}

	udpState = UDP_OPEN;
	{
		const int64_t nsPerBlock = 1000000000ull * MONO_BLOCK_SIZE / SAMPLE_RATE;
		if (threadPriorityDeadline(nsPerBlock / 10, nsPerBlock, 0)) {
			schedPolicy = SCHED_DEADLINE;
			printf("Using deadline priority for sound mixer.\n");
		} else if (threadPriorityRealtime(2)) {
			schedPolicy = SCHED_RR;
			printf("Using realtime priorities.\n");
		} else {
			schedPolicy = SCHED_OTHER;
			printf("Using nice levels only.\n");
		}
	}


	if (pthread_create(&udpThread, NULL, &udpReceiver, NULL) != 0) ERR("Cannot create thread.");
	if (pthread_create(&statusThread, NULL, &statusWorker, NULL) != 0) ERR("Cannot create thread.");

	metronome.enabled = false;
	metronome.beatsPerMinute = METR_DEFAULT_BPM;
	metronome.beatsPerBar = METR_DEFAULT_BPB;

	leading.delay = 0;
	// leading.delay = (int64_t) METR_DELAY_MSEC * SAMPLE_RATE / 1000 / MONO_BLOCK_SIZE; // TODO dynamic adjustment

	int64_t usecFreeSum = 0;
	int64_t usecFreeMin = INT64_MAX;
	int64_t usecWakeDelaySum = 0;
	int64_t usecWakeDelayMax = 0;
	int64_t usecLoadMax = 0;
	int64_t usecAwaken = 0;
	struct packetServerData packet = { .type = PACKET_DATA };


	printf("\n");
	msg("Virtual Choir Rehearsal Room, server v" STR(APP_VERSION) " started.");

	while (udpState == UDP_OPEN) {
		__sync_synchronize();

		for (int i = 0; i < MAX_CLIENTS; i++) {
			if (clients[i] && clients[i]->connected) clients[i]->connectedMain = true;
		}

		// sound mixing [

		sample_t mixedBlock[STEREO_BLOCK_SIZE];
		sample_t *block = packet.block;
		memset(mixedBlock, 0, STEREO_BLOCK_SIZE * sizeof(sample_t));
		packet.blockIndex = blockIndex;
		bool leadingEnabled = metronome.enabled && metronome.lastBeatTime;
		FOR_CLIENTS(client) {
			sample_t *clientBlock = client->lastReadBlock;
			surroundFilter(&client->surroundCtx, bufferReadNext(&client->buffer), clientBlock);
			if (client->isLeader) {
				leadingEnabled = true;
				bool delayChange = leading.delay != leading.newDelay;
				sbufferWrite(&leading.buffer, blockIndex + leading.delay, clientBlock, true); // leading.fadeIn, delayChange XXX
				if (delayChange) {
					leading.delay = leading.newDelay;
					leading.fadeIn = true;
				}
			} else {
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++) {
					mixedBlock[i] += clientBlock[i];
				}
			}
		}

		int maxClientLeadingDelay = 0;
		FOR_CLIENTS(client) {
			if (client->muted) continue;
			sample_t *clientBlock = client->lastReadBlock;
			sample_t *leadingBlock = NULL;
			if (leadingEnabled && !client->isLeader) {
				int delay;
				if (client->restLatencyAvg != FLT_MAX) {
					delay = ((client->aioLatency > 0 ? client->aioLatency : 20) + client->restLatencyAvg) * SAMPLE_RATE / 1000 / MONO_BLOCK_SIZE;
				} else {
					delay = 0;
				}
				if (maxClientLeadingDelay < delay) maxClientLeadingDelay = delay;
				bool fadeIn = false, fadeOut = false;
				if (client->leadingDelay < 0) {
					client->leadingDelay = delay;
					fadeIn = true;
				} else {
					if ((float)abs(client->leadingDelay - delay) * MONO_BLOCK_SIZE / SAMPLE_RATE * 1000 > 5) {
						fadeOut = true;
						delay = client->leadingDelay;
						client->leadingDelay = -1;
					} else {
						delay = client->leadingDelay;
					}
				}
				leadingBlock = sbufferRead(&leading.buffer, blockIndex + delay, fadeIn, fadeOut);

				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++)
					block[i] = mixedBlock[i] - clientBlock[i] + leadingBlock[i];

			} else if (client->isLeader) {
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++)
					block[i] = mixedBlock[i];
			} else {
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++)
					block[i] = mixedBlock[i] - clientBlock[i];
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
			if (leadingEnabled && recording.inclLeader) {
				sample_t *leadingBlock = sbufferRead(&leading.buffer, blockIndex, false, false);
				for (size_t i = 0; i < STEREO_BLOCK_SIZE; i++)
					mixedBlock[i] += leadingBlock[i];
			}
			fwrite(mixedBlock, sizeof(mixedBlock), 1, recording.file);
		}

		if (leadingEnabled) {
			if (leading.delay < maxClientLeadingDelay) {
				leading.newDelay = maxClientLeadingDelay + 10 * SAMPLE_RATE / MONO_BLOCK_SIZE / 1000 + 1;
			}
		} else {
			leading.delay = 0;
			leading.newDelay = 0;
		}

		// ] end of sound mixing

		if (metronome.enabled) {
			leading.delay = leading.newDelay;
			bindex_t nextBeatTime;
			if (metronome.lastBeatTime == 0) {
				nextBeatTime = blockIndex + leading.delay;
				metronome.lastBeatIndex = -1;
				metronome.lastBeatBarIndex = -1;
				sbufferClear(&leading.buffer, 0);
				FOR_CLIENTS(client) {
					client->leadingDelay = -1; // XXX same for leader?
				}
			} else {
				nextBeatTime = metronome.lastBeatTime + (float)SAMPLE_RATE / MONO_BLOCK_SIZE / metronome.beatsPerMinute * 60;
			}
			if (nextBeatTime <= blockIndex + leading.delay) {
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
						sbufferWrite(&leading.buffer, nextBeatTime++, mixedBlock, true);
					}
				}
				if (i % STEREO_BLOCK_SIZE) {
					for (; i % STEREO_BLOCK_SIZE; i++) {
						mixedBlock[i % STEREO_BLOCK_SIZE] = 0;
					}
					sbufferWrite(&leading.buffer, nextBeatTime++, mixedBlock, true);
				}
			}
		}
		blockIndex++;


		// timing [

		int64_t usec = getUsec(usecZero);
		int64_t usecFree = getBlockUsec(blockIndex) - usec;
		usecFreeMin = (usecFreeMin > usecFree ? usecFree : usecFreeMin);
		usecFreeSum += usecFree;
		int64_t usecWakeDelay = usecAwaken - getBlockUsec(blockIndex - 1);
		usecWakeDelayMax = (usecWakeDelayMax < usecWakeDelay ? usecWakeDelay : usecWakeDelayMax);
		usecWakeDelaySum += usecWakeDelay;
		int64_t usecLoad = usec - usecAwaken;
		usecLoadMax = (usecLoadMax < usecLoad ? usecLoad : usecLoadMax);

		FOR_CLIENTS(client) {
			if (usec - client->lastPacketUsec > 1000000) {
				client->connected = false;
				client->connectedMain = false;
				msg("Client %d '%s' timeout, disconnected...", client->id, client->name);
			}
		}

		if (blockIndex % BLOCKS_PER_SRV_STAT == 0) {
			int64_t usecTot   = getBlockUsec(BLOCKS_PER_SRV_STAT);
			int64_t usecBlock = getBlockUsec(1);
			msg("\n"
					"  DELAY %6.0f us (%6.2f %%) avg,%6.0f us (%6.2f %%) max\n"
					"  LOAD  %6.0f us (%6.2f %%) avg,%6.0f us (%6.2f %%) max\n"
					"  FREE  %6.0f us (%6.2f %%) avg,%6.0f us (%6.2f %%) min\n",
					(float)(usecWakeDelaySum) / BLOCKS_PER_SRV_STAT,
					(float)(usecWakeDelaySum) / usecTot * 100,
					(float)(usecWakeDelayMax),
					(float)(usecWakeDelayMax) / usecBlock * 100,
					(float)(usecTot - usecWakeDelaySum - usecFreeSum) / BLOCKS_PER_SRV_STAT,
					(float)(usecTot - usecWakeDelaySum - usecFreeSum) / usecTot * 100,
					(float)(usecLoadMax),
					(float)(usecLoadMax) / usecBlock * 100,
					(float)(usecFreeSum) / BLOCKS_PER_SRV_STAT,
					(float)(usecFreeSum) / usecTot * 100,
					(float)(usecFreeMin > 0 ? usecFreeMin : 0),
					(float)(usecFreeMin > 0 ? usecFreeMin : 0) / usecBlock * 100);

			usecFreeSum = 0;
			usecFreeMin = INT64_MAX;
			usecWakeDelaySum = 0;
			usecWakeDelayMax = 0;
			usecLoadMax = 0;
		}

		__sync_synchronize();
		int64_t usecWait = getBlockUsec(blockIndex) - getUsec(usecZero);
		if (usecWait < 0) {
			msg("Sound mixer was late by %ld us...", -usecWait);
		}
		//while (usecWait > 0) {
		if (usecWait > 0) {
			if (schedPolicy == SCHED_DEADLINE) {
				sched_yield();
			} else {
				usleep(usecWait);
			}
			//usecWait = getBlockUsec(blockIndex) - getUsec(usecZero);
		}
		usecAwaken = getUsec(usecZero);

		// ] end of timing
	}

	pthread_join(udpThread, NULL);
	pthread_join(statusThread, NULL);
	netCleanup();
	msg("Exitting...");

	return 0;
 }
