// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <portaudio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "tty.h"
#include <pthread.h>

#ifdef __WIN32__
#include <pa_win_wasapi.h>
#endif

#define SAMPLE_RATE      48000
#define BLOCK_SIZE         128

#define BUFFER_SIZE    (2<<20)

typedef uint32_t bindex_t;
typedef int16_t  sample_t;

sample_t buffer[BUFFER_SIZE];
volatile size_t readPos = 0;
volatile size_t writePos = 2<<13;

PaStream *paInputStream = NULL, *paOutputStream = NULL;
int problems = 0;

static int paStreamCallback(
		const void *input, void *output, unsigned long count,
		const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
		void *none) {

	if (statusFlags & (paOutputOverflow | paOutputUnderflow | paPrimingOutput)) {
		problems++;
	}

	for (size_t i = 0; i < count * 2; i++) {
		((sample_t *)output)[i] = buffer[readPos++ % BUFFER_SIZE];
	}

	if (count != BLOCK_SIZE) {
		static int stat = 0;
		if (stat++ % 100 == 0) {
			printf("count: %d, BLOCK_SIZE: %d\n", count, BLOCK_SIZE);
		}
	}

	return paContinue;
	// return paAbort;
}

bool aioConnectAudio(PaStream **paInputStream, PaStream **paOutputStream) {
	PaError err;


	PaDeviceIndex inputIndex, outputIndex;
	float inputSuggLat, outputSuggLat;
	bool useWasapiExclusive = 0;

#ifdef __WIN32__
		struct PaWasapiStreamInfo wasapiInfo = {
			.size = sizeof(PaWasapiStreamInfo),
			.hostApiType = paWASAPI,
			.version = 1,
			.flags = paWinWasapiExclusive | paWinWasapiThreadPriority,
			.channelMask = 0,
			.hostProcessorOutput = NULL,
			.hostProcessorInput = NULL,
			.threadPriority = eThreadPriorityProAudio };
		PaHostApiIndex wasapiIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
#else
		struct {} wasapiInfo;
#endif

	if (ttyPromptKey("Use default sound settings? (y/n)", "yn") != 'n') {
		inputIndex = Pa_GetDefaultInputDevice();
		outputIndex = Pa_GetDefaultOutputDevice();
		inputSuggLat = 0;
		outputSuggLat = 0;
#ifdef __WIN32__
	if (wasapiIndex >= 0) {
		const PaHostApiInfo *wasapiInfo = Pa_GetHostApiInfo(wasapiIndex);
		inputIndex = wasapiInfo->defaultInputDevice;
		outputIndex = wasapiInfo->defaultOutputDevice;
		useWasapiExclusive = true;
	}
#endif
	} else {

		printf("List of available interfaces and their devices:\n");
		{
			PaHostApiIndex apiCnt = Pa_GetHostApiCount();
			PaHostApiIndex apiDefault = Pa_GetDefaultHostApi();

			for (PaHostApiIndex apiIndex = 0; apiIndex < apiCnt; apiIndex++) {
				const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(apiIndex);
				printf("  %s API (type %d)%s\n", apiInfo->name, apiInfo->type, (apiIndex == apiDefault ? ", system default" : ""));
				// apiInfo -> defaultInputDevice, deviceCount
				
				for (int apiDeviceIndex = 0; apiDeviceIndex < apiInfo->deviceCount; apiDeviceIndex++) {
					PaDeviceIndex deviceIndex = Pa_HostApiDeviceIndexToDeviceIndex(apiIndex, apiDeviceIndex);
					const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(deviceIndex);
					printf("  %3d: %s, %.0f Hz\n", deviceIndex, deviceInfo->name, deviceInfo->defaultSampleRate);
					if (deviceInfo->maxInputChannels > 0) {
						printf("         IN:  %3d channels, latency %.2f--%.2f ms%s\n",
								deviceInfo->maxInputChannels,
								deviceInfo->defaultLowInputLatency * 1000,
								deviceInfo->defaultHighInputLatency * 1000,
								(deviceIndex == apiInfo->defaultInputDevice ? ", default input" : ""));
					}

					if (deviceInfo->maxOutputChannels > 0) {
						printf("         OUT: %3d channels, latency %.2f--%.2f ms%s\n",
								deviceInfo->maxOutputChannels,
								deviceInfo->defaultLowOutputLatency * 1000,
								deviceInfo->defaultHighOutputLatency * 1000,
								(deviceIndex == apiInfo->defaultOutputDevice ? ", default output" : ""));
					}

				}
			}
		}
		printf("\n");


		/*
		PaError err = Pa_OpenDefaultStream( &paStream,
				1, 1, paInt16, SAMPLE_RATE, SAMPLE_RATE * BUFFER_SIZE_MSEC / 1000, //paFramesPerBufferUnspecified,
				paOutputStreamCallback, NULL);
				*/

		/*
		printf("Default input device id: %d\nDefault output device id: %d\nDefault minimal latency: 0\n\n",
				Pa_GetDefaultInputDevice(),Pa_GetDefaultOutputDevice());
				*/

		inputIndex = ttyPromptInt("Choose input device id");
		outputIndex = ttyPromptInt("Choose output device id");
		inputSuggLat = ttyPromptFloat("Set minimal input latency");
		inputSuggLat = ttyPromptFloat("Set minimal output latency");

#ifdef __WIN32__
		const PaDeviceInfo *inputInfo  = Pa_GetDeviceInfo(inputIndex);
		const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(outputIndex);
		if ((inputInfo->hostApi == wasapiIndex) && (outputInfo->hostApi == wasapiIndex)) {
			useWasapiExclusive = (ttyPromptKey("Use WASAPI exclusive mode (y/n)", "yn") == 'y');
		}
#endif

	}



	const PaStreamParameters inputParameters = {
		.device = inputIndex,
		.channelCount = 2,
		.sampleFormat = paInt16,
		.suggestedLatency =  inputSuggLat/1000,
		.hostApiSpecificStreamInfo = (useWasapiExclusive ? &wasapiInfo : NULL)};

	err = Pa_OpenStream( paInputStream,
			&inputParameters, NULL,
			SAMPLE_RATE, BLOCK_SIZE, paNoFlag,
			NULL, NULL);

	if (err != paNoError) {
		printf("%s\n", Pa_GetErrorText(err));
		return false;
	}

	const PaStreamParameters outputParameters = {
		.device = outputIndex,
		.channelCount = 2,
		.sampleFormat = paInt16,
		.suggestedLatency =  outputSuggLat/1000,
		.hostApiSpecificStreamInfo = (useWasapiExclusive ? &wasapiInfo : NULL)};

	err = Pa_OpenStream( paOutputStream,
			NULL, &outputParameters,
			SAMPLE_RATE, BLOCK_SIZE, paNoFlag, // XXX
			NULL, NULL);

	if (err != paNoError) {
		printf("%s\n", Pa_GetErrorText(err));
		return false;
	}


	Pa_StartStream(*paOutputStream);
	Pa_StartStream(*paInputStream);


	{
		const PaStreamInfo *inputStreamInfo = Pa_GetStreamInfo(*paInputStream);
		const PaStreamInfo *outputStreamInfo = Pa_GetStreamInfo(*paOutputStream);

		printf("\ninput latency: %f ms\noutput latency: %f ms\ninput sample rate: %f\noutput sample rate: %f\n\n",
			inputStreamInfo->inputLatency * 1000,
			outputStreamInfo->outputLatency * 1000,
			inputStreamInfo->sampleRate,
			outputStreamInfo->sampleRate);
	}
	return true;
}

void *outputWorker(void *none) {
	while (true) {
		__sync_synchronize();
		sample_t block[BLOCK_SIZE * 2];
		for (int i = 0; i < BLOCK_SIZE; i++) {
			block[2*i] = block[2*i+1] = buffer[readPos++ % BUFFER_SIZE];
		}
		PaError err = Pa_WriteStream(paOutputStream, block, BLOCK_SIZE);
		if (err != paNoError) {
			printf("output err: %d\n", err);
		}
	}
}

int main() {
	if (Pa_Initialize() != paNoError) return 1;
	if (!aioConnectAudio(&paInputStream, &paOutputStream)) return 2;

	pthread_t outputThread;
	pthread_create(&outputThread, NULL, &outputWorker, NULL);

	while (true) {
		sample_t block[BLOCK_SIZE * 2];
		PaError err = Pa_ReadStream(paInputStream, block, BLOCK_SIZE);
		for (int i = 0; i < BLOCK_SIZE * 2; i+=2) {
			buffer[writePos++ % BUFFER_SIZE] = block[i];
		}
		__sync_synchronize();
		if (err != paNoError) {
			printf("err: %d\n", err);
		}
		if (writePos % (2<<16) == 0) {
			static size_t readPosLast = 0, writePosLast = 0;
			printf("readPos: %d (+%d), writePos: %d (+%d), outputProblems: %d\n", readPos, readPos - readPosLast, writePos, writePos - writePosLast, problems);
			readPosLast = readPos;
			writePosLast = writePos;
		}
	}

}
