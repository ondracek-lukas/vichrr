// Copyright (C) 2020  Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3

#include <math.h>

struct surroundCtx {
	signed char phaseShift;
	int32_t multL;
	int32_t multR;
	sample_t prevMonoBlock[MONO_BLOCK_SIZE];
};

void surroundInitCtx(struct surroundCtx *ctx, float dBAdj, float horizAngle /* rad */, float distance /* m */) {
	const double earsDist = 0.2; // m
	const double soundSpeed = 343; // m/s

	double distL, distR;
	{
		double a = (double)distance * distance + earsDist * earsDist / 4;
		double b = earsDist * distance * sin(horizAngle);
		distL = sqrt(a + b);
		distR = sqrt(a - b);
	}
	
	ctx->multL = (1<<16) * (exp10f(dBAdj / 20) / distL);
	ctx->multR = (1<<16) * (exp10f(dBAdj / 20) / distR);

	ctx->phaseShift = (distR - distL) / soundSpeed * SAMPLE_RATE; // at most 28 samples
}

void surroundFilter(struct surroundCtx *ctx, sample_t *monoBlock, sample_t *stereoBlockOut) {
	ssize_t iL = ctx->phaseShift < 0 ? ctx->phaseShift : 0;
	ssize_t iR = iL - ctx->phaseShift;

	for (ssize_t i = 0; i < MONO_BLOCK_SIZE; i++, iL++, iR++) {
		stereoBlockOut[2 * i]     = ((iL < 0 ? ctx->prevMonoBlock[MONO_BLOCK_SIZE + iL] : monoBlock[iL]) * ctx->multL) >> 16;
		stereoBlockOut[2 * i + 1] = ((iR < 0 ? ctx->prevMonoBlock[MONO_BLOCK_SIZE + iR] : monoBlock[iR]) * ctx->multR) >> 16;
	}
	memcpy(ctx->prevMonoBlock, monoBlock, MONO_BLOCK_SIZE * sizeof(sample_t));

}
