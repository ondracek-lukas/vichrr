// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include "main.h"

#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <float.h>
#include <errno.h>

#include "stereoBuffer.h"
#include "net.h"
#include "tty.h"
#include "audioIO.h"

struct stereoBuffer outputBuffer;
PaStream *paInputStream = NULL, *paOutputStream = NULL;
int udpSocket = -1;
pthread_t inputThread, outputThread, udpThread;
uint8_t clientID;
float aioLat = 0;
float dBAdj = 20;
char sHeloStr[SHELO_STR_LEN+1];
char *serverKeys = "";
char *serverKeysDesc = "";
char *clientKeysDesc = "";


volatile enum inputMode {
	INPUT_DISCARD,
	INPUT_MEASURE_LATENCY,
	INPUT_TO_OUTPUT,
	INPUT_NULL_TO_OUTPUT,
	INPUT_SEND,
	INPUT_END
} inputMode = INPUT_DISCARD;

volatile enum outputMode {
	OUTPUT_PASS,
	OUTPUT_PASS_STAT,
	OUTPUT_NULL,
	OUTPUT_END
} outputMode = OUTPUT_PASS;

volatile enum udpState {
	UDP_OPEN,
	UDP_CLOSED
} udpState;

static void *outputWorker(void *none) {
	while (outputMode != OUTPUT_END) {
		__sync_synchronize();
		sample_t *blockStereo = NULL;
		if (outputMode == OUTPUT_NULL) {
			blockStereo = sbufferRead(&outputBuffer, 0, true, true);
		} else {
			blockStereo = sbufferReadNext(&outputBuffer);
		}
		PaError err = Pa_WriteStream(paOutputStream, blockStereo, MONO_BLOCK_SIZE);
		if ((outputMode == OUTPUT_PASS_STAT) && (outputBuffer.readPos % 50 == 0)) {
			float dBAvg, dBPeak;
			sbufferOutputStats(&outputBuffer, &dBAvg, &dBPeak);
			if (dBAvg + dBAdj > -20) {
				dBAdj = -20 - dBAvg;
			}

			char str[200];
			char *s = str;

			s += sprintf(s, "%-21s ", "system level:");
			ttyFormatSndLevel(&s, dBAvg, dBPeak);
			*s++ = '\n';
			s += sprintf(s, "%-21s ", "adjusted level:");
			ttyFormatSndLevel(&s, dBAvg + dBAdj, dBPeak + dBAdj);

			ttyResetStatus();
			ttyUpdateStatus(str, 0);
			ttyPrintStatus();
		}
	}
	return NULL;
}

static void *inputWorker(void *none) {
	bindex_t blockIndex = 0;
	enum inputMode lastMode = INPUT_END;
	struct packetClientData packet = {};
	packet.type = PACKET_DATA;
	sample_t *blockMono = packet.block;
	sample_t blockStereo[STEREO_BLOCK_SIZE * 2];

	while (inputMode != INPUT_END) {
		PaError err = Pa_ReadStream(paInputStream, blockStereo, MONO_BLOCK_SIZE);
		for (size_t i = 0; i < MONO_BLOCK_SIZE; i++) {
			blockMono[i] = blockStereo[2 * i];
		}
		if (inputMode != lastMode) {
			__sync_synchronize();
			blockIndex = 0;
			switch (inputMode) {
				case INPUT_TO_OUTPUT:
					//sbufferClear(&outputBuffer, 0);
					break;
				case INPUT_MEASURE_LATENCY:
					//sbufferClear(&outputBuffer, 0);
					aioLatReset();
					break;
				case INPUT_SEND:
					packet.clientID = clientID;
					break;
				default: break;
			}
			lastMode = inputMode;
		}
		switch (inputMode) {
			case INPUT_SEND:
				packet.blockIndex = blockIndex++;
				packet.playBlockIndex = outputBuffer.readPos;
				send(udpSocket, (void *)&packet, sizeof(packet), 0);
				break;
			case INPUT_TO_OUTPUT:
				sbufferWriteNext(&outputBuffer, blockStereo, false);
				break;
			case INPUT_NULL_TO_OUTPUT:
				memset(blockStereo, 0, sizeof(sample_t) * STEREO_BLOCK_SIZE);
				sbufferWriteNext(&outputBuffer, blockStereo, false);
				break;
			case INPUT_MEASURE_LATENCY:
				aioLatBlock(blockMono, outputBuffer.writeLastPos + 1 - outputBuffer.readPos);
				for (size_t i = 0; i < MONO_BLOCK_SIZE; i++) {
					blockStereo[2 * i] = blockStereo[2 * i + 1] = blockMono[i];
				}
				sbufferWriteNext(&outputBuffer, blockStereo, false);
				break;
			case INPUT_DISCARD:
			case INPUT_END:
				break;
		}
		__sync_synchronize();
		/*
		if (blockIndex % 1000 == 999) {
			sbufferPrintStats(&outputBuffer);
		}
		*/
	}
	return NULL;
}

static void *udpReceiver(void *none) {
	char packetRaw[sizeof(union packet) + 1];
	union packet *packet = (union packet *) &packetRaw;
	ssize_t size;
	int statusIndex = -1;
	uint8_t packetsCnt = 0;
	bool packetsReceived[256];

#ifdef DEBUG_AUTORECONNECT
reconnected:
#endif

	printf("Waiting for server response...\n");
	while ((size = recv(udpSocket, packetRaw, sizeof(union packet), 0)) > 0) {
		switch (packetRaw[0]) {
			case PACKET_HELO:
				packetRaw[size] = '\0';
				clientID = packet->sHelo.clientID;
				sbufferClear(&outputBuffer, packet->sHelo.initBlockIndex);
				strncpy(sHeloStr, packet->sHelo.str, SHELO_STR_LEN+1);
				sHeloStr[SHELO_STR_LEN]='\0';
				serverKeys = sHeloStr;
				serverKeysDesc = strchr(sHeloStr, '\n');
				if (serverKeysDesc && (*serverKeysDesc == '\n')) {
					*serverKeysDesc++ = '\0';
				} else {
					serverKeysDesc = "";
				}
				clientKeysDesc = "[^C]  exit";
				printf("Connected.\n");
				fflush(stdout);
				__sync_synchronize();
				inputMode = INPUT_SEND;
				break;
			case PACKET_DATA:
				if ((inputMode != INPUT_SEND) || (size != sizeof(struct packetServerData))) break;
				sbufferWrite(&outputBuffer, packet->sData.blockIndex, packet->sData.block, false);
				break;
			case PACKET_STATUS:
				if (inputMode != INPUT_SEND) break;
				packetRaw[size] = '\0';
				if ((int)packet->sStat.statusIndex > statusIndex) {
					statusIndex = packet->sStat.statusIndex;
					packetsCnt = packet->sStat.packetsCnt;
					for (int i = 0; i < 256; i++) {
						packetsReceived[i] = false;
					}
					ttyResetStatus();
				} else if (packet->sStat.statusIndex < statusIndex) {
					break;
				} else if (packetsCnt > packet->sStat.packetsCnt) {
					packetsCnt = packet->sStat.packetsCnt;
				}
				packetsReceived[packet->sStat.packetIndex] = true;
				ttyUpdateStatus(packet->sStat.str, STATUS_LINES_PER_PACKET * packet->sStat.packetIndex + 1);
				bool complete = true;
				for (int i = 0; i < packetsCnt; i++) {
					if (!packetsReceived[i]) {
						complete = false; break;
					}
				}
				if (complete) {
					ttyUpdateStatus(serverKeysDesc, ttyStatusLines);
					ttyUpdateStatus(clientKeysDesc, ttyStatusLines);
					ttyPrintStatus();
				}
				break;
		}
	}
	printf("\n");
	if (size < 0) {
		printf("Error while waiting for data: %s (%d)\n", strerror(errno), errno); // XXX
	}
	ttyClearStatus();
	udpState = UDP_CLOSED;
	inputMode = INPUT_DISCARD;

#ifdef DEBUG_AUTORECONNECT
	{
		printf("\nConnection lost or cannot be established, reconnecting...\n");
		Pa_Sleep(5000);
		struct packetClientHelo packet = {
			.type = PACKET_HELO,
			.version = PROT_VERSION,
			.aioLatency = aioLat,
			.dBAdj = dBAdj
		};
		strcpy(packet.name, "auto");
		send(udpSocket, (void *)&packet, (void *)strchr(packet.name, '\0') - (void *)&packet, 0);
		goto reconnected;
	}
#endif


	printf("Connection lost or cannot be established, connect again? (y/n): ");
	fflush(stdout);
	return NULL;
}

int main() {
#ifndef __WIN32__
	close(2);
#endif
	ttyInit();
	netInit();

	printf("\n"
			"    +----------------------------------------------------------------------+\n"
			"    | Virtual Choir Rehearsal Room v" STR(APP_VERSION) ",                                   |\n"
			"    | created by Lukas Ondracek, use under GNU GPLv3.                      |\n"
			"    +----------------------------------------------------------------------+\n");
			

	if (Pa_Initialize() != paNoError) {
		printf("Cannot initialize PortAudio library.\n");
		exit(1);
	}


	printf("\n== 1/4 == SELECT SOUND INTERFACE AND DEVICES ==================================\n\n");

	printf(
			"Usually, there are several interfaces for communicating with your sound devices\n"
			"and possibly even multiple devices; different interfaces have different delays.\n\n");

#ifdef __WIN32__
	printf(
			"On Windows, WASAPI in exclusive mode is chosen as default interface.\n"
			"You may need to allow exclusive mode in your system settings\n"
			"and set sampling rate of both microphone and headphones to " STR(SAMPLE_RATE) " Hz.\n\n");
#endif
	sbufferClear(&outputBuffer, 0);
	aioConnectAudio(&paInputStream, &paOutputStream);

	pthread_create(&inputThread, NULL, &inputWorker, NULL);
	pthread_create(&outputThread, NULL, &outputWorker, NULL);

	printf("\n== 2/4 == MEASURE DELAY OF SOUND SYSTEM =======================================\n\n");

	printf(
			"Take your headphones OFF your ears,\n"
			"place microphone closer to audio source if possible,\n"
			"and press space to continue...\n"
			"\n"
			"Several loud beeps will be played and recorded back to measure the delay\n"
			"between producing sound by the application a getting it back there.\n"
			"\n"
			"In case of failure, you can use loudspeaker temporarily.\n"
			"\n"
			"Later you will see delay in form `A+B ms`, where A is this delay\n"
			"while B is all the remaining round-trip delay (in application, network, etc.).\n");

	ttyReadKey();
	do {
		printf("\nMeasuring...:  "); fflush(stdout);
		double min[3] = {DBL_MAX, DBL_MAX, DBL_MAX};
		int succCnt = 0;
		double value = 0;
		int i = 0;
		while ((succCnt < 5) && ((i++ < 10) || (value > 0))) {
			inputMode = INPUT_MEASURE_LATENCY;
			Pa_Sleep(200);
			inputMode = INPUT_NULL_TO_OUTPUT;
			Pa_Sleep(50);
			value = aioLatReset();
			if (value > 0) {
				printf("%.2f ms  ", value); fflush(stdout);
				succCnt++;
				int j = 0;
				while ((min[j] < value) && (j < 3)) j++;
				for (; j < 3; j++) {
					double tmp = min[j];
					min[j] = value;
					value = tmp;
				}
			} else {
				printf("none  "); fflush(stdout);
			}
		}
		printf("\n\n");
		if (succCnt >= 5) {
			aioLat = min[2];
			printf("Measured latency: %.2f ms.\n", aioLat);
			printf("Continue with this value?");
		} else {
			aioLat = 0;
			printf("Measuring failed, skip this step?");
		}
	} while (ttyPromptKey(" (y/n)", "yn") == 'n');


	printf("\n== 3/4 == SET MICROPHONE AND HEADPHONES' VOLUME ===============================\n\n");
	printf(
			"Connect your headphones and press space...\n");
	ttyReadKey();
	printf(
			"---\n\n"
			"Sing loudly for a while, then press space to continue...\n"
			"\n"
			"You may hear the sound being recorded by your microphone\n"
			"and see its average (#) and peak (+) intensity levels on the scales below.\n"
			"Set your microphone volume in system settings so that\n"
			"it's peak intensity never exceeds -10 dB on the first scale; --------------+\n"
			"test it by loud singing.                                                   |\n"
			"At the same time further intensity adjustments will be performed           |\n"
			"and displayed on the second scale;                                         |\n"
			"the average level of laud singing on this scale may be just below -20 dB.  |\n"
			"Also set your microphone position                                          |\n"
			"to hear your voice but not your breathing.                                 |\n"
			"                                                                           |\n"
			"Press r to reset adjustments (and space when done)...          average   peak\n"
			"                    -78                          -20  -10    0    |        |\n"
			"                      |                            |    |    |    V        V\n");
			//                     [########++++++------------------------]  -xx dB ( -xx dB)

	sbufferOutputStatsReset(&outputBuffer, true);
	inputMode = INPUT_TO_OUTPUT;
	outputMode = OUTPUT_PASS_STAT;

	{
		bool retry = true;
		while (retry) {
			switch (ttyReadKey()) {
				case 'r':
					dBAdj = 20;
					break;
				case ' ':
					retry = false;
					break;
			}
		}
	}

	outputMode = OUTPUT_PASS;
	Pa_Sleep(50);
	sbufferOutputStatsReset(&outputBuffer, false);
	ttyClearStatus();

	printf("\n== 4/4 == SERVER SETTINGS =====================================================\n\n");
	printf(
			"Disclaimer:\n"
			"  After entering server address,\n"
			"  sound being recorded as well as all key presses to this application\n"
			"  may be send unencrypted to the server.\n\n");

	inputMode = INPUT_DISCARD;
	//outputMode = OUTPUT_NULL;

	char name[NAME_LEN+1];

	strncpy(name, ttyPromptStr("Your name (without diacritics, at most " STR(NAME_LEN) " letters)"), NAME_LEN);
	name[NAME_LEN]='\0';
	char *addr = NULL;

	while (true) {
		if (!addr || (ttyPromptKey("Use the same server address? (y/n)", "yn") == 'n')) {
			free(addr);
			char *newAddr = ttyPromptStr("Server address");
			addr = strdup(newAddr);
		}
		printf("\nContacting server...\n");
		udpSocket = netOpenConn(addr, STR(UDP_PORT));
		if (udpSocket < 0) continue;

		{
			struct packetClientHelo packet = {
				.type = PACKET_HELO,
				.version = PROT_VERSION,
				.aioLatency = aioLat,
				.dBAdj = dBAdj
			};
			strcpy(packet.name, name);
			if (send(udpSocket, (void *)&packet, (void *)strchr(packet.name, '\0') - (void *)&packet, 0) == -1) {
				printf("Error while sending initial packet: %s (%d)\n", strerror(errno), errno);
			};
		}

		udpState = UDP_OPEN;
		pthread_create(&udpThread, NULL, &udpReceiver, NULL);


		int c;
		while ((c = ttyReadKey()) != EOF) {
			switch (udpState) {
				case UDP_OPEN:
					switch (c) {
						default:
							if (strchr(serverKeys, c)) {
								struct packetKeyPress packet = {
									.type = PACKET_KEY_PRESS,
									.clientID = clientID,
									.playBlockIndex = outputBuffer.readPos,
									.key = c
								};
								send(udpSocket, (void *)&packet, sizeof(struct packetKeyPress), 0);
							}
					}; break;
				case UDP_CLOSED:
					switch (c) {
						case 'y':
							printf("y\n");
							goto RECONNECT;
						case 'n':
							printf("n\n");
							goto EXIT;
					}; break;
			}
		}
		RECONNECT:
		pthread_join(udpThread, NULL);
	}

	EXIT:


	// terminate
	inputMode = INPUT_END;
	outputMode = OUTPUT_END;
	pthread_join(inputThread, NULL);
	pthread_join(outputThread, NULL);
	pthread_join(udpThread, NULL);
	Pa_StopStream(paInputStream);
	Pa_StopStream(paOutputStream);
	Pa_Terminate();
	netCleanup();

	printf("\n");
}
