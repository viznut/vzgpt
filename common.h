#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include "config.h"

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

typedef struct{
  /* constants (network parameters) */
  float*ln1_b,*ln1_g,*ln2_b,*ln2_g,
       *mlp_cfc_b,*mlp_cfc_w,
       *mlp_cproj_b,*mlp_cproj_w,
       *attn_cattn_b,*attn_cattn_w,
       *attn_cproj_b,*attn_cproj_w;
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
  int tok;
} match_t;

/* vocabulary & word vector handling */
global int numtokens;
global int nummodeltokens;
global char**tokenstrings; /* alloc in loadtokens() */
global float*currwv;       /* alloc in init() */
global match_t*matchlist;  /* alloc in init() */
global char*tokenflags;    /* alloc in loadtokens() */
global float**userwte;     /* alloc in ui_init() */
global float*targetwv;
global int emptytoken;     /* set in init() */

/* context buffer */
global int*context;        /* alloc in init() */
global volatile int currslot;
global volatile int genstart;
global volatile int genend;

/* model */
global char*modelpath;
global float*wte;
global int8_t*wte8;
global float*wpe;
global int8_t*wpe8;
global hlayer*layers;
global float*lnf_b;
global float*lnf_g;
/* igpt extras */
global float*wtet;
global float*sos;
global float*palette;

/* quantization extras */
#ifdef QUANTIZE
global float quanter_wte;
global float quanter_wpe;
#else
#define quanter_wte 1.0
#define quanter_wpe 1.0
#endif

/* settings */
global float temperature;
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
  { "minp", &minp, 0 },
  { "nummatches", &nummatches, 1 },
  { "genend", &genend, 1 },
  { "numthreads", &numthreads, 1 }
}
#endif
;

/* visualization for ui */
global float*attentions;
global float*outputcache;

/* ui */
global SDL_Surface*fb;
global int scrw,scrh;

/* functions */
void*readfile(char*fn,int*lgt_ret,char*path);
void runModel(float*x,int slot);
void renderwordvec(float*wv0,int x0,int y0,int dim);
void renderlayernode(float*wv,float*att,int numheads,int x0,int y0);
void matchToTokens(float*wv,match_t*o,int num,float temp);
int pickmatch(match_t*list,int sz,float minp);
float*getwv(int token);
void clearcontext(int i);
void purgeoldcontext(int p);

/*inline float conv1dline(float a,float*v,float*m,int wdt);*/

#define frand() ((rand()&65535)/65536.0)
