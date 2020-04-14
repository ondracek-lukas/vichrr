// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <float.h>

#include "main.h"
#include "net.h"
#include "tty.h"
#include "audioBuffer.h"
#include "audioIO.h"

struct audioBuffer outputBuffer;
PaStream *paInputStream = NULL, *paOutputStream = NULL;
int udpSocket = -1;
pthread_t inputThread, outputThread, udpThread;
uint8_t clientID;
float aioLat = 0;

volatile enum inputMode {
	INPUT_DISCARD,
	INPUT_MEASURE_LATENCY,
	INPUT_TO_OUTPUT,
	INPUT_SEND,
	INPUT_END
} inputMode = INPUT_DISCARD;

volatile enum outputMode {
	OUTPUT_PASS,
	OUTPUT_PASS_STAT,
	OUTPUT_END
} outputMode = OUTPUT_PASS;

volatile enum udpState {
	UDP_OPEN,
	UDP_CLOSED
} udpState;

static void *outputWorker(void *none) {
	while (outputMode != OUTPUT_END) {
		__sync_synchronize();
		sample_t *blockMono = bufferReadNext(&outputBuffer);
		sample_t blockStereo[BLOCK_SIZE * 2];
		for (size_t i = 0; i < BLOCK_SIZE; i++) {
			blockStereo[2 * i] = blockStereo[2 * i + 1] = blockMono[i];
		}
		PaError err = Pa_WriteStream(paOutputStream, blockStereo, BLOCK_SIZE);
		if ((outputMode == OUTPUT_PASS_STAT) && (outputBuffer.readPos % 50 == 0)) {
			float dBAvg, dBPeak;
			bufferOutputStats(&outputBuffer, &dBAvg, &dBPeak);
			char str[100];
			char *s = str;
			for (int i=0; i < 21; i++) *s++ = ' ';
			ttyFormatSndLevel(&s, dBAvg, dBPeak);
			ttyResetStatus();
			ttyUpdateStatus(str, 0);
			ttyPrintStatus();
		}
	}
}

static void *inputWorker(void *none) {
	bindex_t blockIndex = 0;
	enum inputMode lastMode = INPUT_END;
	struct packetClientData packet;
	packet.type = PACKET_DATA;
	sample_t *blockMono = packet.block;
	sample_t blockStereo[BLOCK_SIZE * 2];

	while (inputMode != INPUT_END) {
		PaError err = Pa_ReadStream(paInputStream, blockStereo, BLOCK_SIZE);
		for (size_t i = 0; i < BLOCK_SIZE; i++) {
			blockMono[i] = blockStereo[2 * i];
		}
		if (inputMode != lastMode) {
			__sync_synchronize();
			blockIndex = 0;
			switch (inputMode) {
				case INPUT_TO_OUTPUT:
					bufferClear(&outputBuffer, 0);
					/*
					memset(blockMono, 0, sizeof(blockMono)); // XXX
					for (int i = 0; i < 30; i++) {
						bufferWrite(&outputBuffer, blockIndex++, blockMono);
					}
					*/
					break;
				case INPUT_MEASURE_LATENCY:
					bufferClear(&outputBuffer, 0);
					aioLatReset();
					break;
				case INPUT_SEND:
					packet.clientID = clientID;
					break;
			}
			lastMode = inputMode;
		}
		switch (inputMode) {
			case INPUT_SEND:
				packet.blockIndex = blockIndex++;
				packet.recBlockIndex = outputBuffer.readPos; // XXX check latency calc
				send(udpSocket, (void *)&packet, sizeof(packet), 0);
				break;
			case INPUT_TO_OUTPUT:
				bufferWrite(&outputBuffer, blockIndex++, blockMono);
				break;
			case INPUT_MEASURE_LATENCY:
				aioLatBlock(blockMono, blockIndex - outputBuffer.readPos);
				bufferWrite(&outputBuffer, blockIndex++, blockMono);
				break;
			case INPUT_DISCARD:
			case INPUT_END:
				break;
		}
		__sync_synchronize();
		/*
		if (blockIndex % 1000 == 999) {
			bufferPrintStats(&outputBuffer);
		}
		*/
	}

}

static void *udpReceiver(void *none) {
	char packetRaw[sizeof(union packet) + 1];
	union packet *packet = (union packet *) &packetRaw;
	ssize_t size;
	int statusIndex = -1;
	char packetsCnt = 0;
	bool packetsReceived[256];

	while ((size = recv(udpSocket, packetRaw, sizeof(union packet), 0)) >= 0) {
		switch (packetRaw[0]) {
			case PACKET_HELO:
				if (size != sizeof(struct packetServerHelo)) break;
				clientID = packet->sHelo.clientID;
				bufferClear(&outputBuffer, packet->sHelo.initBlockIndex);
				printf("Connected.\n");
				fflush(stdout);
				__sync_synchronize();
				inputMode = INPUT_SEND;
				break;
			case PACKET_DATA:
				if ((inputMode != INPUT_SEND) || (size != sizeof(struct packetServerData))) break;
				bufferWrite(&outputBuffer, packet->sData.blockIndex, packet->sData.block);
				break;
			case PACKET_STATUS:
				packetRaw[size] = '\0';
				if ((int)packet->sStat.statusIndex > statusIndex) {
					statusIndex = packet->sStat.statusIndex;
					packetsCnt = packet->sStat.packetsCnt;
					for (int i = 0; i < packetsCnt; i++) {
						packetsReceived[i] = false;
					}
					ttyResetStatus();
				} else if (packet->sStat.statusIndex < statusIndex) {
					break;
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
					ttyPrintStatus(); // XXX different lines count
				}
				break;
		}
	}
	ttyClearStatus();
	udpState = UDP_CLOSED;
	inputMode = INPUT_DISCARD;
	printf("Connection lost, connect again? (y/n): ");
	fflush(stdout);
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
	aioConnectAudio(&outputBuffer, &paInputStream, &paOutputStream);

	pthread_create(&inputThread, NULL, &inputWorker, NULL);
	pthread_create(&outputThread, NULL, &outputWorker, NULL);

	printf("\n== 2/4 == MEASURE DELAY OF SOUND SYSTEM =======================================\n\n");

	printf(
			"Now, take your headphones OFF your ears,\n"
			"place microphone closer to audio source if possible,\n"
			"and press space to continue...\n"
			"\n"
			"Several loud beeps will be played and recorded back to measure the delay\n"
			"between producing sound by the application a getting it back there.\n"
			"\n"
			"In case of failure, you can use loudspeaker temporarily.\n"
			"\n"
			"Later you will see this delay + all the remaining delay in both directions\n"
			"(in application, network, etc.).\n");


	ttyReadKey();
	do {
		printf("\nMeasuring...:  "); fflush(stdout);
		double min[3] = {DBL_MAX, DBL_MAX, DBL_MAX};
		int succCnt = 0;
		double value = 0;
		int i = 0;
		while ((succCnt < 5) && ((i++ < 10) || (value > 0))) {
			inputMode = INPUT_MEASURE_LATENCY;
			Pa_Sleep(450);
			inputMode = INPUT_DISCARD;
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
			"\n"
			"You may hear the sound being recorded by your microphone\n"
			"and see its average (#) and peak (+) intensity levels on the scale below.\n"
			"Set your microphone volume in system settings so that\n"
			"it's peak intensity never exceeds -30 dB; --------------------------------+\n"
			"test it by loud singing.                                                  |\n"
			"Also set your microphone position                                         |\n"
			"to hear your voice but not your breathing.                                |\n"
			"                                                                          |\n"
			"Press space when done...                                      average   peak\n"
			"                                                                 |        |\n"
			"                     | -78 dB         -30 dB |         0 dB |    V        V\n");
			//                    [########++++++------------------------]  -xx dB ( -xx dB)

	bufferOutputStatsReset(&outputBuffer, true);
	inputMode = INPUT_TO_OUTPUT;
	outputMode = OUTPUT_PASS_STAT;
	ttyReadKey();
	outputMode = OUTPUT_PASS;
	Pa_Sleep(50);
	bufferOutputStatsReset(&outputBuffer, false);
	ttyClearStatus();


	printf("\n== 4/4 == SERVER SETTINGS =====================================================\n\n");
	inputMode = INPUT_DISCARD;

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
		udpSocket = netOpenConn(addr, STR(UDP_PORT));

		{
			struct packetClientHelo packet = {
				.type = PACKET_HELO,
				.version = PROT_VERSION,
				.aioLatency = aioLat,
			};
			strcpy(packet.name, name);
			ssize_t err = send(udpSocket, (void *)&packet, (void *)strchr(packet.name, '\0') - (void *)&packet, 0);
		}

		udpState = UDP_OPEN;
		pthread_create(&udpThread, NULL, &udpReceiver, NULL);


		int c;
		while ((c = ttyReadKey()) != EOF) {
			switch (udpState) {
				case UDP_OPEN:
					switch (c) {
						case 'q': goto EXIT;
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
	pthread_join(udpThread, NULL);
	Pa_StopStream(paInputStream);
	Pa_StopStream(paOutputStream);
	Pa_Terminate();
	netCleanup();

	printf("n\n");
}
