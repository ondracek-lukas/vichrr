// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <time.h>
#include <portaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __WIN32__
#include <pa_win_wasapi.h>
#endif

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

	printf("[space] default settings    [c] custom settings\n");
	if (ttyReadKey() != 'c') {
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

		printf("List of available APIs and their devices:\n");
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
			SAMPLE_RATE, MONO_BLOCK_SIZE, paNoFlag,
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
			SAMPLE_RATE, MONO_BLOCK_SIZE, paNoFlag,
			NULL, NULL);

	if (err != paNoError) {
		printf("%s\n", Pa_GetErrorText(err));
		return false;
	}


	Pa_StartStream(*paOutputStream);
	Pa_StartStream(*paInputStream);


	/*
	{
		const PaStreamInfo *inputStreamInfo = Pa_GetStreamInfo(*paInputStream);
		const PaStreamInfo *outputStreamInfo = Pa_GetStreamInfo(*paOutputStream);

		printf("\ninput latency: %f ms\noutput latency: %f ms\ninput sample rate: %f\noutput sample rate: %f\n\n",
			inputStreamInfo->inputLatency * 1000,
			outputStreamInfo->outputLatency * 1000,
			inputStreamInfo->sampleRate,
			outputStreamInfo->sampleRate);
	}
	*/
	return true;
}


// --- measuring latency ---

#define WAIT_SAMPLES  (10 * MONO_BLOCK_SIZE)
#define BEEP_SAMPLES  (20 * MONO_BLOCK_SIZE)

uint64_t aioLatSqSums[BEEP_SAMPLES];
uint64_t aioLatSqSum = 0;
size_t   aioLatPos = 0;
uint64_t aioLatMaxDiff = 0;
uint64_t aioLatMaxDiffPos = 0;
int64_t  aioLatBufferLatBlocksSum = 0;

void aioLatBlock(sample_t *block, int bufferLatBlocks) {
	for (size_t i = 0; i < MONO_BLOCK_SIZE; i++) {
		aioLatSqSum += (uint64_t)block[i] * block[i];
		if (aioLatPos > BEEP_SAMPLES) {
			uint64_t diff = aioLatSqSum - aioLatSqSums[aioLatPos % BEEP_SAMPLES];
			if (diff > aioLatMaxDiff) {
				aioLatMaxDiff = diff;
				aioLatMaxDiffPos = aioLatPos - BEEP_SAMPLES;
			}
		}
		aioLatSqSums[aioLatPos++ % BEEP_SAMPLES] = aioLatSqSum;

		if (aioLatPos < WAIT_SAMPLES) {
			block[i] = 0;
		} else if (aioLatPos < WAIT_SAMPLES + BEEP_SAMPLES) {
			block[i] = fmod(aioLatPos * 440.0 / SAMPLE_RATE, 1) < 0.5 ? -30000 : 30000;
		} else {
			block[i] = 0;
		}
	}
	aioLatBufferLatBlocksSum += bufferLatBlocks;
}

double aioLatReset() {
	double ret = 0;
	if (aioLatPos > BEEP_SAMPLES + WAIT_SAMPLES) {
		ret = ((double)aioLatMaxDiffPos - WAIT_SAMPLES) / SAMPLE_RATE * 1000;

		double bufferLat = (double)aioLatBufferLatBlocksSum / (aioLatPos / MONO_BLOCK_SIZE) * MONO_BLOCK_SIZE / SAMPLE_RATE * 1000;
		ret -= bufferLat;

		double signalToNoise =
			((double)aioLatMaxDiff / BEEP_SAMPLES) /
			((double)(aioLatSqSum - aioLatMaxDiff) / (aioLatPos - BEEP_SAMPLES));
		if ((signalToNoise < 5) || (ret < 0)) ret = 0;
	}

	aioLatSqSum = 0;
	aioLatPos = 0;
	aioLatMaxDiff = 0;
	aioLatMaxDiffPos = 0;
	aioLatBufferLatBlocksSum = 0;
	return ret;
}

