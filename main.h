// Virtual Choir Rehearsal Room  Copyright (C) 2020  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3

#define _GNU_SOURCE

#define PROT_VERSION              4
#define APP_VERSION             1.3
#define UDP_PORT              64199
#define NAME_LEN                 10
#define MAX_CLIENTS             100
#define BLOCKS_PER_STAT          50
#define BLOCKS_PER_SRV_STAT    2000  // should be divisible by BLOCKS_PER_STAT
#define CONN_TIMEOUT_MSEC      2000  // ms

#define CLIENT_SOCK_BUF_SIZE 100000 // B
// #define SERVER_SCHED_DEADLINE

#define SAMPLE_RATE           48000
#define MONO_BLOCK_SIZE         128  // 2.667 ms
#define BUFFER_BLOCKS          4096  // 10.92 s, ~1 MB mono, ~2 MB stereo
#define STEREO_BLOCK_SIZE (2 * MONO_BLOCK_SIZE)
#define BUFFER_DES_JUMP_PERIOD 1500  // blocks, 5s; desired minimal period between jumps in stream


#define STAT_HALFLIFE_MSEC      100
#define STAT_MULTIPLIER           0.9817
	// multiplies avg square stats every block, it's halved after ~100 ms
#define STAT_LATENCY_MULTIPLIER   0.9637
	// multiplies avg latency every block, it's halved after ~50 ms

#define STATUS_WIDTH             79
#define STATUS_HEIGHT           200
#define STATUS_LINES_PER_PACKET   4
#define SHELO_STR_LEN           500

// metronome
#define METR_DELAY_MSEC         500
#define METR_MAX_BPM            300
#define METR_MIN_BPM             10
#define METR_DEFAULT_BPM         80
#define METR_MAX_BPB             64
#define METR_DEFAULT_BPB          4
#define METR_CACHE_BEATS        (METR_DELAY_MSEC * METR_MAX_BPM / 60000 + 1)

#define STR(arg) STR2(arg)
#define STR2(arg) #arg

#include <stdint.h>
typedef uint32_t bindex_t;
typedef int16_t  sample_t;

// #define DEBUG_AUTORECONNECT
// #define DEBUG_BUFFER_VERBOSE
