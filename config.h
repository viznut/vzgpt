#define HAVE_THREADS
#define HAVE_SDL
#define HAVE_MMAP
#define HAVE_LUA
#define ENABLE_SDLUI
#define ENABLE_TTYUI
//#define USE_LIBEDIT
/* Still slightly buggy
//#define CONSTS_AS_VARS
*/

/* more or less constant values */
#define MAXTOKENS 53000
#define MAXUSERTOKENS 256
#define MAXNUMMATCHES 256
#define MAXNUMLAYERS 48

/* default window size for sdl ui */
#define DEFAULT_SCRW 640
#define DEFAULT_SCRH 480

/* these are constant in all gpt-2 networks i've seen so far */
#define CTXSIZE 1024
#define HEADSIZE 64
#define RSQRT_HEADSIZE (1/8.0)

/* these may vary from network to network
 * GPT2-S (124M): WVSIZE=768, NUMLAYERS=12, NUMHEADS=12
 * IGPT-S (76M):  WVSIZE=512, NUMLAYERS=24, NUMHEADS=8
 * GPT2-L (774M): WVSIZE=1280, NUMLAYERS=36, NUMHEADS=20
 */
#ifndef CONSTS_AS_VARS
#define WVSIZE 768
#define NUMLAYERS 12
#define NUMHEADS 12
#endif

/* Use packed floats for those matrices where it doesn't cause regression: */
#define USE_PKDFLT
/* Quantize WTE matrix into int16 (~no regression): */
#define USE_PKD_WTE

/* 8-bit quantization is work-in-progress
//#define QUANTIZE
//#define Q8MODE_INWTE
//#define Q8MODE_OUTWTE
//#define Q8MODE_MLP
*/

/* maximum number of threads to support */
#define MAXNUMTHR 8
