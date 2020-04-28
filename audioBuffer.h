// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3


/* needed defs:
 *   BLOCK_SIZE
 *   BUFFER_BLOCKS
 *   bindex_t
 *   sample_t
 */


#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef BLOCK_SIZE
#define BLOCK_SIZE MONO_BLOCK_SIZE
#endif

struct audioBuffer {
	bindex_t readPos;       // to be read
	bindex_t writeLastPos;  // farest already written
	bindex_t skipCondDur;
	double statAvgSq;
	double statMaxSq;
	/*
	int64_t statSumSq;
	int64_t statMaxSq;
	int64_t statCnt;
	*/
	bool statClear;
	bool statEnabled;
	bool fade;
	bindex_t nullReads;
	sample_t tmpBlock[BLOCK_SIZE];
	enum {
		BLOCK_EMPTY, BLOCK_USED
	} blockState[BUFFER_BLOCKS];
	sample_t data[BUFFER_BLOCKS * BLOCK_SIZE];
};

// fading within one block in case of discontinuity slightly reduces crackling
float bufferFade(int index) { // XXX optimize
	/*
	static float cache[BLOCK_SIZE] = {-1};
	if (cache[0] < 0) {
		for (int i = 0; i < BLOCK_SIZE; i++) {
			cache[i] = 1;
			//cache[i] = ((float)i)/BLOCK_SIZE;
			//cache[i] = exp2(((float)i)/BLOCK_SIZE * 2 - 2);
			//cache[i] = cos(((float)i) / BLOCK_SIZE * M_PI_2);
			//cache[i] = 1 - sin(((float)i) / BLOCK_SIZE * M_PI_2);
			//cache[i] = (1 - cos(((float)i) / BLOCK_SIZE * M_PI)) / 2;
			//cache[i] = cos(((float)i) / BLOCK_SIZE * M_PI_2)/2 + ((float)i)/BLOCK_SIZE / 2;
			//cache[i] = exp2((1 - cos(((float)i) / BLOCK_SIZE * M_PI)) / 2 * 4 - 4);
			//cache[i] = log2(i)/log2(BLOCK_SIZE);
		}
	}
	return cache[index];
	*/
	return ((float)index)/BLOCK_SIZE;
}

void bufferClear(struct audioBuffer *buf, bindex_t readPos) {
	buf->readPos = readPos;
	buf->writeLastPos = 0;
	buf->skipCondDur = 0;
	buf->fade = true;
	buf->nullReads = 0;
	/*
	buf->statSumSq = 0;
	buf->statMaxSq = 0;
	buf->statCnt = 0;
	*/
	buf->statAvgSq = 0;
	buf->statMaxSq = 0;
	buf->statClear = false;
	for (int i = 0; i < BUFFER_BLOCKS; i++) {
		buf->blockState[i] = BLOCK_EMPTY;
	}
}

sample_t *bufferReadNext(struct audioBuffer *buf) {
	__sync_synchronize();
	bindex_t writeLastPos = buf->writeLastPos;
	bindex_t readPos = buf->readPos;

	// propose skipping part of the stream if latency is too high
	int propSkip = 0;
	if (writeLastPos >= readPos + 4) {
		propSkip = (writeLastPos - readPos) / 4;
		int misses = 0;
		int i = 0;
		for (; i < propSkip; i++) {
			misses += (buf->blockState[(readPos + i) % BUFFER_BLOCKS] == BLOCK_EMPTY);
			if (misses > propSkip/4) break;
		}
		if (i == propSkip) {
			for (; (i < propSkip * 3) && (misses >= 0); i++) {
				misses -= (buf->blockState[(readPos + i) % BUFFER_BLOCKS] == BLOCK_EMPTY);
			}
			if (misses < 0) {
				propSkip = 0;
			}
		} else {
			propSkip = 0;
		}
	}

	// performe proposed skipping only if the preconditions last longer time
	bool skip = false;
	if (propSkip && (++buf->skipCondDur >= 80)) {
		skip = true;
	} else if (!propSkip) {
		buf->skipCondDur = 0;
	}


	bool curUsed = (buf->blockState[readPos % BUFFER_BLOCKS] == BLOCK_USED);

	// check whether we are waiting too long for a packet, so it can be missed and should be skiped
	if (!curUsed && !skip) {
		bool prevUsed = false;
		for (int i = 0; i < buf->nullReads/2; i++) {
			bool curUsed = (buf->blockState[(readPos + i + 1) % BUFFER_BLOCKS] == BLOCK_USED);
			if (curUsed) {
				if (prevUsed) {
					skip = true;
					propSkip = i;
					break;
				}
			}
			prevUsed = curUsed;
		}
	}

	bool fadeOut = skip || (buf->blockState[(readPos + 1) % BUFFER_BLOCKS] == BLOCK_EMPTY);

	sample_t *retData = NULL;
	if (curUsed && (!fadeOut || !buf->fade)) {

		// a block is available and should be returned
		buf->readPos = readPos + 1;
		__sync_synchronize();
		buf->blockState[readPos % BUFFER_BLOCKS] = BLOCK_EMPTY;
		retData = buf->data + (readPos % BUFFER_BLOCKS) * BLOCK_SIZE;

		// fade-in/fade-out should be performed within the block because of some discontinuity
		if (buf->fade || fadeOut) {
			sample_t *tmpData = buf->tmpBlock;
			if (fadeOut) {
				for (int i = 0; i < BLOCK_SIZE; i++) {
					tmpData[i] = retData[i] * bufferFade(BLOCK_SIZE - i - 1);
				}
				buf->fade = true;
			} else { // fade-in
				for (int i = 0; i < BLOCK_SIZE; i++) {
					tmpData[i] = retData[i] * bufferFade(i);
				}
				buf->fade = false;
			}
			retData = tmpData;
		}
		buf->nullReads = 0;

	} else {

		// we are waiting for some data, nothing is returned
		memset(buf->tmpBlock, 0, BLOCK_SIZE * sizeof(sample_t));
		retData = buf->tmpBlock;
		buf->nullReads++;
	}

	if (skip) {

		// after block being returned, part of the stream will be skipped
		readPos = buf->readPos;
		buf->readPos = readPos + propSkip;
		for (; propSkip--; readPos++) {
			buf->blockState[readPos % BUFFER_BLOCKS] = BLOCK_EMPTY;
		}
		buf->skipCondDur = 0;
	}

	if (buf->statEnabled) {
		int64_t sum = 0, max = 0, cnt = 0;
		for (size_t i = 0; i < BLOCK_SIZE; i++) {
			int64_t sq = (int64_t)retData[i] * retData[i];
			sum += sq;
			max = (max < sq ? sq : max);
			cnt++;
		}
		__sync_synchronize();
		if (buf->statClear) {
			buf->statClear = false;
			buf->statAvgSq = 0;
			buf->statMaxSq = 0;
			/*
			buf->statSumSq = 0;
			buf->statMaxSq = 0;
			buf->statCnt = 0;
			*/
		}
		buf->statAvgSq = buf->statAvgSq * STAT_MULTIPLIER + (double)sum/cnt * (1 - STAT_MULTIPLIER);
		buf->statMaxSq = (buf->statMaxSq * STAT_MULTIPLIER < max ? max : buf->statMaxSq * STAT_MULTIPLIER);
		__sync_synchronize();
	}

	return retData;
}

bool bufferWrite(struct audioBuffer *buf, bindex_t pos, sample_t *data) {
	bindex_t readPos = buf->readPos;
	if ((pos < readPos) || (pos >= readPos + BUFFER_BLOCKS - 1) || (buf->blockState[pos % BUFFER_BLOCKS] != BLOCK_EMPTY)) {
		return false;
	}
	
	memcpy(buf->data + (pos % BUFFER_BLOCKS) * BLOCK_SIZE, data, BLOCK_SIZE * sizeof(sample_t));

	__sync_synchronize();
	buf->blockState[pos % BUFFER_BLOCKS] = BLOCK_USED;
	if (buf->writeLastPos < pos) {
		buf->writeLastPos = pos;
	}

	return true;
}

/*
void bufferPrintStats(struct audioBuffer *buf) { // tmp
	bindex_t readPos = buf->readPos;
	bindex_t writeLastPos = buf->writeLastPos;
	int readable = 0;
	while ((readPos + readable < writeLastPos) && (buf->blockState[(readPos + readable) % BUFFER_BLOCKS] == BLOCK_USED)) readable++;
	printf("%3d blocks delay, %3d blocks readable, %5d read calls, %5d write calls\n", writeLastPos - readPos, readable) //, buf->totReadStat, buf->totWriteStat);
}
*/

// --- stats ---

// bufferOutputRootMeanSquareReset
// bufferOutputPeakReset
void bufferOutputStats(struct audioBuffer *buf, float *dBAvg, float *dBPeak) {
	// *dBAvg = 10 * log10f((float)buf->statSumSq / buf->statCnt / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
	// *dBPeak = 10 * log10f((float)buf->statMaxSq / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
	*dBAvg = 10 * log10f(buf->statAvgSq / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
	*dBPeak = 10 * log10f(buf->statMaxSq / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
}
void bufferOutputStatsReset(struct audioBuffer *buf, bool enable) {
	buf->statClear = true;
	buf->statEnabled = enable;
	__sync_synchronize();
}

#undef BLOCK_SIZE
