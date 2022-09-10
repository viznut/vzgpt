#include "config.h"
#ifdef HAVE_SDL
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_MMAP
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#endif

#ifdef __MAIN__
#define global
#else
#define global extern
#endif

#ifdef CONSTS_AS_VARS
global int WVSIZE;
global int NUMLAYERS;
global int NUMHEADS;
#endif

typedef uint16_t token_t;

#ifndef USE_PKDFLT
typedef float pkdflt;
#else
#ifdef USE_NATIVE_FP16
typedef __fp16 pkdflt;
#else
typedef uint16_t pkdflt;
#endif
#endif

#ifdef USE_PKD_WTE
typedef int16_t wte_t;
#else
typedef float wte_t;
#endif

typedef struct{
  /* constants (network parameters) */
  float*ln1_b,*ln1_g,*ln2_b,*ln2_g;
  float*mlp_cfc_b;    pkdflt*mlp_cfc_w;
  float*mlp_cproj_b;  pkdflt*mlp_cproj_w;
  float*attn_cattn_b; pkdflt*attn_cattn_w;
  float*attn_cproj_b; pkdflt*attn_cproj_w;
#ifdef QUANTIZE
  /* 8-bit versions of the network parameters */
  int8_t*mlp_cproj8w,*mlp_cfc8w,
        *mlp_cproj8b,*mlp_cfc8b;
  /* quantizers (something to divide/multiply with) */
  float mlp_cfc_w_q;
  float mlp_cproj_w_q;
#endif
  /* variables */
  float*k,*v;
} hlayer;

typedef struct
{
  float prob;
  token_t tok;
} match_t;

/* not used yet! we'll support several simultaneous contexts in the future */
typedef struct
{
  token_t*in;
  float*k;
  float*v;
  int lgt;
  int validlgt;
} context_t;

/* for packed format */

#define FLAG_HAVE_BASES 1
#define FLAG_HAVE_WTET 2
#define FLAG_HAVE_SOS 4
#define FLAG_HAVE_PALETTE 8
#define FLAG_HAVE_TOKENSTRINGS 16
#define MTYPE_GPT2 0
#define MTYPE_IGPT 1
#define PFMT_FLOAT32 0
#define PFMT_BF16 1
#define PFMT_IEEE16 2
#define PFMT_INT16 3
#define PFMT_INT8 4

typedef struct
{
  char fileformat[4];
  uint32_t wvsize; 
  uint32_t numlayers;
  uint32_t numheads;
  uint32_t numtokens;
  uint32_t ctxsize;
  uint32_t headsize;
  char flags;
  char paramformat;
  char wteformat;
  char reserved0;
  float quanter_wte;
} header_t;

/* vocabulary & word vector handling */
global int numtokens;
global int nummodeltokens;
global char**tokenstrings; /* alloc in loadtokens() */
global float*currwv;       /* alloc in init() */
global match_t*matchlist;  /* alloc in init() */
global char*tokenflags;    /* alloc in loadtokens() */
global token_t*tokenrepls; /* alloc in flagTokenForReplace() */
global float*targetwv;
global token_t emptytoken; /* set in init() */
global char*tokendata;     /* alloc in loadtokens() */

/* context buffer */
global token_t*context;    /* alloc in init() */
// ^ todo global context_t*context;
global volatile int currslot;
global volatile int genstart;
global volatile int genend;

/* model */
global header_t*h;
global char*modelpath;
global wte_t*wte;
global pkdflt*wpe;
global wte_t**userwte;    /* alloc in ui_init() */
global wte_t*wtet;
global wte_t*sos;
global hlayer*layers;
global float*lnf_b;
global float*lnf_g;
#ifdef QUANTIZE
global int8_t*wte8;
global int8_t*wpe8;
#endif
/* igpt extras */
global float*palette;

/* quantization extras */
#ifdef QUANTIZE
global float quanter_wpe;
#else
#define quanter_wpe 1.0
#endif

#ifdef USE_PKD_WTE
global float quanter_wte;
#else
#define quanter_wte 1.0
#endif

/* settings */
global float temperature;
global float temperature_alt;
global float minp;
global float minp_for_tagged; /* not used (yet?) */
global int nummatches;
global int numthreads;
global int verbose;

global struct
{
  char*name;
  volatile void*ptr;
  char type;
} settingvars[]
#ifdef __MAIN__
={
  { "temperature", &temperature, 0 },
  { "temperature_alt", &temperature_alt, 0 },
  { "minp", &minp, 0 },
  { "nummatches", &nummatches, 1 },
  { "genend", &genend, 1 },
  { "numthreads", &numthreads, 1 }
}
#endif
;
#define NUMVARS 5

/* visualization for ui */
global float*attentions;
global float*outputcache;

/* ui */
#ifdef HAVE_SDL
#ifdef ENABLE_SDLUI
global SDL_Surface*fb;
global int scrw,scrh;
#endif
#endif

/* functions */
void*readfile(char*fn,int*lgt_ret,char*path);
char*readtextfile(char*fn,char*path);
void runModel(float*x,int slot);
void renderwordvec(float*wv0,int x0,int y0,int dim);
void renderlayernode(float*wv,float*att,int numheads,int x0,int y0);
void matchToTokens(float*wv,match_t*o,int num,float temp);
int pickmatch(match_t*list,int sz,float minp);
wte_t*getwv(int token);
void clearcontext(int i);
void purgeoldcontext(int p);
int loadtokens_from_tokendata(char*tokendata,int numtokens);
void vzlua(char*scriptfile);

/* markov chain */

#if (0)
typedef struct _prob_t
{
  token_t t;
  int freq;
  _prob_t*lt;
  _prob_t*gt;
} prob_t;

typedef struct _markovnode_t
{
  token_t t;
  _markovnode_t*lt;
  _markovnode_t*gt;
  _markovnode_t*next;
  prob_t*probs;
} markovnode_t;

markovnode_t*markovchain;

prob_t*markov_getprobs(markovnode_t*tree,token_t*ctx);
markovnode_t*markov_import(markovnode_t*tree,char*s,int ctxlgt);
int pickMatchWithMarkov(match_t*list,int sz,int slot);
#endif
// void markov_compress(markovnode_t*tree);

/*inline float conv1dline(float a,float*v,float*m,int wdt);*/

#define frand() ((rand()&65535)/65536.0)

/*** types and conversion macros for packed floats ***/

#ifndef USE_PKDFLT
#define UNPKFLT(s) (s)
#define PKFLT(s) (s)

#else
#ifdef USE_NATIVE_FP16
#define UNPKFLT(s) (s)
#define PKFLT(s) (s)
#define packtensor(s,lgt) (s)

#else
typedef uint16_t pkdflt;

inline pkdflt PKFLT(float s)
{
  uint32_t a=*((uint32_t*)&s);
  if(!(a&0x8000)) return a>>16;
  a>>=16;
  if((a&0x7f)==0x7f) return a; // don't overflow mantissa
  return a+1;
}

inline float UNPKFLT(pkdflt s)
{
  uint32_t a=s;
  // format: bfloat16
  a=(a<<16);//|0x8000;//a;
  return *((float*)&a);
}
#endif
#endif

#define UNPKWTE(a) (a/quanter_wte)

/* innerloop of matrix multiplication (or "1d convolution").
 * this is where most of the computation takes place.
 */

inline float conv1dline(float a,float*v,float*m,int wdt)
{
  int i;
  for(i=0;i<wdt;i++) a+=v[i]*m[i];
  return a;
}

#ifdef USE_PKDFLT
inline float conv1dline_pkd(float a,float*v,pkdflt*m,int wdt)
{
  int i;
  for(i=0;i<wdt;i++) a+=v[i]*UNPKFLT(m[i]);
  return a;
}
#else
#define conv1dline_pkd conv1dline
#endif

inline int64_t conv1dline_pkdwte(int64_t a,int32_t*v,wte_t*m,int wdt)
{
  int i;
  for(i=0;i<wdt;i++) a+=v[i]*m[i];
  return a;
}

inline int conv1dline_ii(int a,int8_t*v,int8_t*m,int wdt)
{
  int i;
  for(i=0;i<wdt;i++) a+=v[i]*m[i];
  return a;
}

inline int conv1dline_fi(float a,float*v,int8_t*m,int wdt)
{
  int i;
  for(i=0;i<wdt;i++) a+=v[i]*m[i];
  return a;
}
