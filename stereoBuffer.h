#define BLOCK_SIZE STEREO_BLOCK_SIZE

#define audioBuffer stereoBuffer
#define bufferFade sbufferFade
#define bufferClear sbufferClear
#define bufferReadNext sbufferReadNext
#define bufferRead sbufferRead
#define bufferWrite sbufferWrite
#define bufferWriteNext sbufferWriteNext
#define bufferOutputStats sbufferOutputStats
#define bufferOutputStatsReset sbufferOutputStatsReset
#define BLOCK_USED SBLOCK_USED
#define BLOCK_EMPTY SBLOCK_EMPTY


#include "audioBuffer.h"

#undef audioBuffer
#undef bufferFade
#undef bufferClear
#undef bufferReadNext
#undef bufferRead
#undef bufferWrite
#undef bufferWriteNext
#undef bufferOutputStats
#undef bufferOutputStatsReset
#undef BLOCK_USED
#undef BLOCK_EMPTY

#undef BLOCK_SIZE
