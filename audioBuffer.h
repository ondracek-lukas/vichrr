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
	bindex_t readTime;      // number of bufferReadNext calls
	bindex_t lastJumpTime;  // readTime of last discontinuity
	double statAvgSq;
	double statMaxSq;
	bool statClear;
	bool statEnabled;
	bool fade;
	size_t srvStatWait;
	size_t srvStatSkip;
	size_t srvStatLost;
	size_t srvStatPlay;
	int nullReads;
	sample_t tmpBlock[BLOCK_SIZE];
	bindex_t blockTime[BUFFER_BLOCKS];    // readTime when written; block empty iff 0
	sample_t data[BUFFER_BLOCKS * BLOCK_SIZE];
};

// fading within one block in case of discontinuity slightly reduces crackling
float bufferFade(int index) {
	return ((float)index)/BLOCK_SIZE;
}

void bufferClear(struct audioBuffer *buf, bindex_t readPos) {
	buf->readPos = readPos;
	buf->readTime = 1;
	buf->lastJumpTime = 0;
	buf->writeLastPos = 0;
	buf->fade = true;
	buf->nullReads = 0;
	buf->statAvgSq = 0;
	buf->statMaxSq = 0;
	buf->statClear = false;
	for (int i = 0; i < BUFFER_BLOCKS; i++) {
		buf->blockTime[i] = 0;
	}
	buf->srvStatWait = 0;
	buf->srvStatSkip = 0;
	buf->srvStatLost = 0;
	buf->srvStatPlay = 0;
}

sample_t *bufferRead(struct audioBuffer *buf, bindex_t pos, bool fadeIn, bool fadeOut) {
	sample_t *retData;

	if ((fadeIn && fadeOut) ||
			(pos + BUFFER_BLOCKS <= buf->writeLastPos) || (pos > buf->writeLastPos) ||
			(!buf->blockTime[pos % BUFFER_BLOCKS])) {
		retData = buf->tmpBlock;
		memset(retData, 0, BLOCK_SIZE * sizeof(sample_t));
		return retData;
	}

	retData = buf->data + (pos % BUFFER_BLOCKS) * BLOCK_SIZE;
	if (fadeIn || fadeOut) {
		// fade-in/fade-out should be performed within the block because of some discontinuity
		sample_t *tmpData = buf->tmpBlock;
		if (fadeOut) {
			for (int i = 0; i < BLOCK_SIZE; i++) {
				tmpData[i] = retData[i] * bufferFade(BLOCK_SIZE - i - 1);
			}
		} else { // fadeIn
			for (int i = 0; i < BLOCK_SIZE; i++) {
				tmpData[i] = retData[i] * bufferFade(i);
			}
		}
		retData = tmpData;
	}
	return retData;
}

// low-latency reading
sample_t *bufferReadNext(struct audioBuffer *buf) {
	__sync_synchronize();
	bindex_t readTime = ++buf->readTime;
	bindex_t writeLastPos = buf->writeLastPos;
	bindex_t readPos = buf->readPos;
	__sync_synchronize();

	if (readPos + BUFFER_BLOCKS <= writeLastPos) {
		// writing occurred too far apart reading, this shouldn't happen
		buf->readPos = readPos = writeLastPos - BUFFER_BLOCKS/2;
		buf->fade = true; // no fade out before it
#ifdef DEBUG_BUFFER_VERBOSE
		printf("Long jump\n");
#endif
	}

	// check whether to skip some data (to lower delay)
	int skip = 0;
	if (buf->lastJumpTime + BUFFER_SKIP_PERIOD <= buf->readTime) {
		skip = writeLastPos - readPos - 1;
		if (skip > BUFFER_SKIP_PERIOD) skip = BUFFER_SKIP_PERIOD;
		int seenBlocks = 0;
		for (bindex_t i = writeLastPos; (skip > 0) && (i + BUFFER_DES_JUMP_PERIOD > readPos) && (i > 0); i--) {
			if (buf->blockTime[i % BUFFER_BLOCKS]) {
				int val = i - readPos + readTime - buf->blockTime[i % BUFFER_BLOCKS] - 2; // skip allowed by i-th block
				if (skip > val) {
					skip = val;
				}
				seenBlocks++;
			}
			if ((i + BUFFER_DES_JUMP_PERIOD <= readPos + skip) && (seenBlocks >= BUFFER_DES_JUMP_PERIOD)) break;
		}
		if ((skip <= 0) || !buf->blockTime[(readPos + skip) % BUFFER_BLOCKS] || !buf->blockTime[(readPos + skip + 1) % BUFFER_BLOCKS]) skip = 0;
	}

	bool curUsed = (readPos <= buf->writeLastPos) && (buf->blockTime[readPos % BUFFER_BLOCKS]);

	// insert one empty block if this one arrived just on time and so previous was faded
	if (buf->nullReads == -1) {
		if (curUsed && !skip) {
			curUsed = false;
		} else {
			buf->nullReads = 0;
		}
	}

	// check whether we are waiting too long for a packet, so it can be missed and should be skiped
	if (!curUsed && !skip) {
		bool used = false;
		for (int i = 0; i <= buf->nullReads; i++) {
			if (readPos + i + 1 > buf->writeLastPos) break;
			bool nextUsed = (buf->blockTime[(readPos + i + 1) % BUFFER_BLOCKS]);
			if (used && nextUsed) {
				readPos += i;
				buf->readPos = readPos;
				curUsed = true;
				buf->srvStatLost += i;
				buf->nullReads -= i;
				if (buf->nullReads) {
					buf->lastJumpTime = buf->readTime;
				}
#ifdef DEBUG_BUFFER_VERBOSE
				printf("lost %d, readPos %d, writePos %d\n", i, readPos, buf->writeLastPos);
#endif
				break;
			}
			used = nextUsed;
		}
	}

	bool fadeOut = skip || (!buf->blockTime[(readPos + 1) % BUFFER_BLOCKS]);

	sample_t *retData = NULL;
	if (curUsed && (!fadeOut || !buf->fade)) {

		// a block is available and should be returned
		buf->readPos = readPos + 1;
		__sync_synchronize();
		retData = bufferRead(buf, readPos, buf->fade, fadeOut);

		buf->fade = fadeOut;

		buf->srvStatPlay++;
		buf->srvStatWait += buf->nullReads;
#ifdef DEBUG_BUFFER_VERBOSE
		if (buf->nullReads > 0) {
			printf("wait %d\n", buf->nullReads);
		}
#endif
		buf->nullReads = 0;

		if (fadeOut && !skip) buf->nullReads = -1; // special value; insert one empty block if the following will arrive on time

	} else {

		// we are waiting for some data, nothing is returned
		memset(buf->tmpBlock, 0, BLOCK_SIZE * sizeof(sample_t));
		retData = buf->tmpBlock;
		buf->nullReads++;

#ifdef DEBUG_BUFFER_VERBOSE
		if ((buf->nullReads % 100 == 0) && (buf->nullReads != 0)) {
			size_t readPos = buf->readPos;
			size_t writePos = buf->writeLastPos;
			printf("read %d, write %d\n", readPos, writePos);
			if (readPos > 0) {
				for (size_t i = writePos; i >= readPos; i--) {
					printf("%d", buf->blockTime[i % BUFFER_BLOCKS]);
				}
				printf("\n");
			}
		}
#endif
	}

	if (skip) {

		// after block being returned, part of the stream will be skipped
		readPos = buf->readPos;
		buf->readPos = readPos + skip;
		buf->lastJumpTime = readTime;
		buf->srvStatWait += buf->nullReads;
#ifdef DEBUG_BUFFER_VERBOSE
		printf("skip %d + wait %d, readPos %d, writePos %d\n", skip, buf->nullReads, buf->readPos, buf->writeLastPos);
#endif
		buf->nullReads = 0;
		buf->srvStatSkip += skip;

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
		}
		buf->statAvgSq = buf->statAvgSq * STAT_MULTIPLIER + (double)sum/cnt * (1 - STAT_MULTIPLIER);
		buf->statMaxSq = (buf->statMaxSq * STAT_MULTIPLIER < max ? max : buf->statMaxSq * STAT_MULTIPLIER);
		__sync_synchronize();
	}

	return retData;
}

				// tmpData[i] = retData[i] * bufferFade(i); XXX
bool bufferWrite(struct audioBuffer *buf, bindex_t pos, const sample_t *data, bool add) { // TODO fadeIn, fadeOut
	if (buf->writeLastPos < pos) {
		do {
			buf->blockTime[++buf->writeLastPos % BUFFER_BLOCKS] = 0;
		} while (buf->writeLastPos < pos);
		__sync_synchronize();
	}

	if (pos < buf->readPos) {
		return false;
	}

	if (!add || (!buf->blockTime[pos % BUFFER_BLOCKS])) {
		memcpy(buf->data + (pos % BUFFER_BLOCKS) * BLOCK_SIZE, data, BLOCK_SIZE * sizeof(sample_t));
	} else {
		sample_t *block = buf->data + (pos % BUFFER_BLOCKS) * BLOCK_SIZE;
		for (size_t i = 0; i < BLOCK_SIZE; i++) {
			block[i] += data[i];
		}
	}

	__sync_synchronize();
	buf->blockTime[pos % BUFFER_BLOCKS] = buf->readTime;

	return true;
}

bool bufferWriteNext(struct audioBuffer *buf, const sample_t *data, bool add) {
	return bufferWrite(buf, buf->writeLastPos + 1, data, add);
}

// --- stats (when readNext is used) ---

void bufferOutputStats(struct audioBuffer *buf, float *dBAvg, float *dBPeak) {
	*dBAvg = 10 * log10f(buf->statAvgSq / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
	*dBPeak = 10 * log10f(buf->statMaxSq / (1ll << (2 * sizeof(sample_t) * 8 - 2)));
}
void bufferOutputStatsReset(struct audioBuffer *buf, bool enable) {
	buf->statClear = true;
	buf->statEnabled = enable;
	__sync_synchronize();
}

void bufferSrvStatsReset(struct audioBuffer *buf, size_t *play, size_t *lost, size_t *wait, size_t *skip, ssize_t *delay) {
	__sync_synchronize();
	*play = buf->srvStatPlay; buf->srvStatPlay = 0;
	*lost = buf->srvStatLost; buf->srvStatLost = 0;
	*wait = buf->srvStatWait; buf->srvStatWait = 0;
	*skip = buf->srvStatSkip; buf->srvStatSkip = 0;
	*delay = buf->writeLastPos - buf->readPos + 1;
}

#undef BLOCK_SIZE
