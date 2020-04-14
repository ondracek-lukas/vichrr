// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#define PROT_VERSION         1
#define APP_VERSION        0.1
#define UDP_PORT          1234
#define NAME_LEN            10
#define MAX_CLIENTS        100
#define BLOCKS_PER_STAT    100

#define SAMPLE_RATE      48000
#define BLOCK_SIZE         128  // 2.667 ms
#define BUFFER_BLOCKS      512

#define STAT_HALFLIFE_MSEC 100
#define STAT_MULTIPLIER    0.9817
	// multiplies avg square stats every block, it's halved after ~100 ms

#define STATUS_WIDTH        79
#define STATUS_HEIGHT      100
#define STATUS_LINES_PER_PACKET 4

#define STR(arg) STR2(arg)
#define STR2(arg) #arg

#include <stdint.h>
typedef uint32_t bindex_t;
typedef int16_t  sample_t;
