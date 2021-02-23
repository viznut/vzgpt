#include "common.h"

/* option: we can use 16-bit "packed floats" for the big matrices */

#ifdef USE_PKDFLT
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

float*packtensor(float*s,int lgt)
{
  int i;
  pkdflt*o=malloc(lgt*sizeof(pkdflt));
  for(i=0;i<lgt;i++)
    o[i]=PKFLT(s[i]);
  free(s);
  return(float*)o;
}
#else
typedef float pkdflt;
#define UNPKFLT(s) (s)
#define PKFLT(s) (s)
#endif

/* some matrices are transposed after loading */

float*transpose(float*m,int w,int h)
{
  int i,j;
  float*o=malloc(sizeof(float)*w*h);
  for(i=0;i<h;i++)
  for(j=0;j<w;j++)
    o[j*h+i]=m[i*w+j];
  free(m);
  return o;
}

/* here we load the model from separate raw files */

void loadmodel(char*path)
{
  int i;
  int sz;
  char is_igpt=0;
#ifdef QUANTIZE
  quanter_wte=1.0;
  quanter_wpe=1.0;
#endif
  lnf_g=(float*)readfile("lnf_g.raw",&sz,path);
  if(!lnf_g)
  {
    fprintf(stderr,"check if the directory is valid!\n");
    exit(1);
  }
#ifdef CONSTS_AS_VARS
  WVSIZE=sz/sizeof(float);
  fprintf(stderr,"wordvector length: %d\n",WVSIZE);
#else
  if(sz!=WVSIZE*sizeof(float))
  {
    fprintf(stderr,"lnf_g doesn't match hardcoded WVSIZE=%d!\n",WVSIZE);
    exit(1);
  }
#endif  
  lnf_b=(float*)readfile("lnf_b.raw",&sz,path);

  wte=(float*)readfile("wte.raw",&sz,path);
  numtokens=sz/(WVSIZE*sizeof(float));
  fprintf(stderr,"vocabulary size: %d wordvecs\n",numtokens);
  wpe=(float*)readfile("wpe.raw",&sz,path);
  if(sz!=CTXSIZE*WVSIZE*sizeof(float))
  {
    fprintf(stderr,"wpe size mismatch!\n");
    exit(1);
  }
  wtet=(float*)readfile("wtet.raw",&sz,path); // igpt-only
  sos=(float*)readfile("sos.raw",&sz,path); // igpt-only

  for(i=0;i<MAXNUMLAYERS;i++)
  {
    char fn[80];
    
    layers=realloc(layers,sizeof(hlayer)*(i+1));
    if(!layers)
    {
      fprintf(stderr,"memory allocation error at layer %d!\n",i);
      exit(1);
    }

    sprintf(fn,"h%d_ln1_g.raw",i);
          layers[i].ln1_g=(float*)readfile(fn,&sz,path);
    if(!layers[i].ln1_g) break;
    sprintf(fn,"h%d_ln1_b.raw",i); // not in igpt
          layers[i].ln1_b=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_ln2_g.raw",i);
          layers[i].ln2_g=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_ln2_b.raw",i); // not in igpt
          layers[i].ln2_b=(float*)readfile(fn,&sz,path);

    sprintf(fn,"h%d_mlp_cfc_w.raw",i);
          layers[i].mlp_cfc_w=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_mlp_cfc_b.raw",i); // not in igpt
          layers[i].mlp_cfc_b=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_mlp_cproj_w.raw",i);
          layers[i].mlp_cproj_w=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_mlp_cproj_b.raw",i); // not in igpt
          layers[i].mlp_cproj_b=(float*)readfile(fn,&sz,path);
#ifdef QUANTIZE
    layers[i].mlp_cfc_w_q=1.0;
    layers[i].mlp_cproj_w_q=1.0;
#endif
    sprintf(fn,"h%d_attn_cproj_w.raw",i);
          layers[i].attn_cproj_w=(float*)readfile(fn,&sz,path);
    if(!layers[i].attn_cproj_w)
    {
      // igpt uses different name. size 8x64x512
      sprintf(fn,"h%d_attn_cproj.raw",i);
            layers[i].attn_cproj_w=(float*)readfile(fn,&sz,path);
    }
    if(i==0)
    {
      int numheads=sz/(WVSIZE*HEADSIZE*sizeof(float));
#ifdef CONSTS_AS_VARS
      NUMHEADS=numheads;
      fprintf(stderr,"numheads=%d\n",numheads);
#else
      if(NUMHEADS!=numheads)
      {
        fprintf(stderr,"number of heads (%d) doesn't match hardcoded %d!\n",
          numheads,NUMHEADS);
      }
#endif
    }
    sprintf(fn,"h%d_attn_cproj_b.raw",i); // not in igpt
          layers[i].attn_cproj_b=(float*)readfile(fn,&sz,path);

    sprintf(fn,"h%d_attn_cattn_w.raw",i);
          layers[i].attn_cattn_w=(float*)readfile(fn,&sz,path);
    sprintf(fn,"h%d_attn_cattn_b.raw",i);
          layers[i].attn_cattn_b=(float*)readfile(fn,&sz,path);
    if(!layers[i].attn_cattn_w)
    {
      layers[i].attn_cattn_w=
        malloc(sizeof(float)*3*NUMHEADS*HEADSIZE*WVSIZE);
      float*tmp;
      /* igpt separates cattn_w into 3 files */
      int j;
      for(j=0;j<3;j++)
      {
        sprintf(fn,"h%d_attn_%cproj.raw",i,"qkv"[j]);
        tmp=(float*)readfile(fn,&sz,path);
        memcpy(layers[i].attn_cattn_w+j*NUMHEADS*HEADSIZE*WVSIZE,tmp,
          sizeof(float)*NUMHEADS*HEADSIZE*WVSIZE);
        free(tmp);
      }
      is_igpt=1;
    }

    /* transpose some of the bigger matrices for speedup. */

    if(!is_igpt) /* igpt has already transposed this one */
    {
      layers[i].attn_cattn_w =
        transpose(layers[i].attn_cattn_w,WVSIZE*3,WVSIZE);
    }
      layers[i].attn_cproj_w =
        transpose(layers[i].attn_cproj_w,WVSIZE,WVSIZE);

    layers[i].mlp_cfc_w =
      transpose(layers[i].mlp_cfc_w,WVSIZE*4,WVSIZE);
    layers[i].mlp_cproj_w =
      transpose(layers[i].mlp_cproj_w,WVSIZE,WVSIZE*4);

#ifdef USE_PKDFLT
    layers[i].attn_cattn_w=packtensor(layers[i].attn_cattn_w,WVSIZE*3*WVSIZE);
    layers[i].attn_cproj_w=packtensor(layers[i].attn_cproj_w,WVSIZE*WVSIZE);
    layers[i].mlp_cfc_w=packtensor(layers[i].mlp_cfc_w,WVSIZE*WVSIZE*4);
    layers[i].mlp_cproj_w=packtensor(layers[i].mlp_cproj_w,WVSIZE*WVSIZE*4);
#endif

    layers[i].k=malloc(CTXSIZE*WVSIZE*sizeof(float));
    layers[i].v=malloc(CTXSIZE*WVSIZE*sizeof(float));
  }
#ifdef CONSTS_AS_VARS
  NUMLAYERS=i;
  fprintf(stderr,"number of layers = %d\n",NUMLAYERS);
#else
  if(i!=NUMLAYERS)
  {
    fprintf(stderr,"number of layers (%d) doesn't match hardcoded NUMLAYERS=%d!\n",
      i,NUMLAYERS);
    exit(1);
  }
#endif

}

float statistics(float*m,int sz)
{
  int i;
  float sum=0,sumabs=0,maxabs=0;
  for(i=0;i<sz;i++)
  {
    float a=m[i];
    sum+=a;
    sumabs+=fabs(a);
    if(fabs(a)>maxabs)maxabs=fabs(a);
  }
  sum/=sz;
  sumabs/=sz;
  float muller=128.0/maxabs;
  fprintf(stderr,"avg %f avg(abs) %f max(abs) %f. mul by %f -> %f & %f\n\n",
    sum,sumabs,maxabs, muller, sumabs*muller,maxabs*muller);
  return muller;
}

/* experimental: quantize some matrices into 8-bit integer format */

#ifdef QUANTIZE
void quantize_matrix_fake(int8_t*m8,float*m,float muller,int sz)
{
  int i;
  for(i=0;i<sz;i++) m8[i]=m[i]=m[i]*muller;
}

void quantize_matrix(int8_t*m8,float*m,float muller,int sz)
{
  int i;
  for(i=0;i<sz;i++) m8[i]=m[i]=floor(m[i]*muller);
}

void quantize()
{
  // safe
  float muller,muller1;
  fprintf(stderr,"wte: ");
  quanter_wte=muller=statistics(wte,numtokens*WVSIZE);
  fprintf(stderr,"wpe: ");
  quanter_wpe=muller1=statistics(wpe,CTXSIZE*WVSIZE);
  if(muller1<muller) muller=muller1;

  wte8=malloc(numtokens*WVSIZE*sizeof(int8_t));
  wpe8=malloc(CTXSIZE*WVSIZE*sizeof(int8_t));
  
  // safe
  int i;
  for(i=0;i<numtokens*WVSIZE;i++) wte8[i]=wte[i]=wte[i]*quanter_wte;
  for(i=0;i<CTXSIZE*WVSIZE;i++) wpe8[i]=wpe[i]=wpe[i]*quanter_wpe;

  for(i=0;i<NUMLAYERS;i++)
  {
    layers[i].mlp_cfc8w=malloc(WVSIZE*WVSIZE*4);
    layers[i].mlp_cproj8w=malloc(WVSIZE*WVSIZE*4);
    layers[i].mlp_cfc8b=malloc(WVSIZE*4);
    layers[i].mlp_cproj8b=malloc(WVSIZE);

    // safe
    fprintf(stderr,"%d cfc: ",i);
    muller=statistics(layers[i].mlp_cfc_w,WVSIZE*WVSIZE*4);
    //fprintf(stderr,"%d cfc_b: ",i);
    //muller1=statistics(layers[i].mlp_cfc8b,WVSIZE*4);
    layers[i].mlp_cfc_w_q=muller; //>muller1?muller:muller1;

    // safe
    fprintf(stderr,"%d cproj: ",i);
    muller=statistics(layers[i].mlp_cproj_w,WVSIZE*WVSIZE*4);
    //fprintf(stderr,"%d cproj_b: ",i);
    //muller1=statistics(layers[i].mlp_cproj8b,WVSIZE);
    layers[i].mlp_cproj_w_q=muller; //>muller1?muller:muller1;

    // safe
#ifdef Q8MODE_MLP
    quantize_matrix(layers[i].mlp_cfc8w,layers[i].mlp_cfc_w,
      layers[i].mlp_cfc_w_q,WVSIZE*WVSIZE*4);
    quantize_matrix(layers[i].mlp_cproj8w,layers[i].mlp_cproj_w,
      layers[i].mlp_cproj_w_q,WVSIZE*WVSIZE*4);
#else
    quantize_matrix_fake(layers[i].mlp_cfc8w,layers[i].mlp_cfc_w,
      layers[i].mlp_cfc_w_q,WVSIZE*WVSIZE*4);
    quantize_matrix_fake(layers[i].mlp_cproj8w,layers[i].mlp_cproj_w,
      layers[i].mlp_cproj_w_q,WVSIZE*WVSIZE*4);
#endif

// UNSAFE
//    quantize_matrix(layers[i].mlp_cfc8b,layers[i].mlp_cfc_b,
//      layers[i].mlp_cfc_w_q,WVSIZE*4);
//    quantize_matrix(layers[i].mlp_cproj8b,layers[i].mlp_cproj_b,
//      layers[i].mlp_cproj_w_q,WVSIZE);
  }
  
  //quanter_wte=1.0/muller;
}
#endif

#define EPSILON 0.0000001
void normalize(float*o,float*x,float*b,float*g)
{
  int i;
  float mean=0,smean=0,muller;
  for(i=0;i<WVSIZE;i++)
  {
    mean+=x[i];
  }
  mean/=WVSIZE;
  for(i=0;i<WVSIZE;i++)
  {
    float a=x[i]-mean;
    smean+=a*a;
  }
  smean/=WVSIZE;
  if(smean<EPSILON) smean=EPSILON;
  muller=sqrt(1.0/(smean));
  if(b)
    for(i=0;i<WVSIZE;i++)
      o[i] = (x[i]-mean)*muller*g[i]+b[i];
  else
    for(i=0;i<WVSIZE;i++)
      o[i] = (x[i]-mean)*muller*g[i];
}

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

#define MAXNUMTHR 8
struct
{
  volatile pthread_t t[MAXNUMTHR];
  volatile pthread_barrier_t barrier;
  float*x;
  int slot;
  int numthr;
  float*q;
  float*tmp;
  float*xn;
  float*mlp;
}thrglob;

#ifdef HAVE_THREADS
void syncthreads()
{
  if(thrglob.numthr<=1) return;
  pthread_barrier_wait(&thrglob.barrier);
}
#else
#define syncthreads()
#endif

void runLayer(float*x,int layeridx,int here,int thr,int numthr)
{
#ifdef HAVE_THREADS
  float*q=thrglob.q;
  float*tmp=thrglob.tmp;
  float*xn=thrglob.xn;
  float*mlp=thrglob.mlp;
#else
  float q[WVSIZE]; /* q vectors are only needed locally */
  float tmp[WVSIZE]; /* tmp space for operations */
  float xn[WVSIZE];
#endif
  int i,j,h;
  hlayer*l=&layers[layeridx];

  if(verbose>=2) fprintf(stderr,"layer %d...\n",layeridx);

  if(!thr) normalize(xn,x,l->ln1_b,l->ln1_g);

  syncthreads();

  /* produce query/key/value vectors for this slot */
  {float*b=l->attn_cattn_b;
   pkdflt*w=(pkdflt*)l->attn_cattn_w;
  for(i=thr;i<WVSIZE*3;i+=numthr)
  {
    float a=conv1dline_pkd(b?b[i]:0,xn,w+WVSIZE*i,WVSIZE);
    if(i<WVSIZE)
      q[i]=a;
    else if(i<WVSIZE*2)
      l->k[here*WVSIZE+(i-WVSIZE)]=a;
    else
      l->v[(i-WVSIZE*2)*CTXSIZE+here]=a;
  }
  }

  syncthreads();

  if(verbose>=3) fprintf(stderr,"heads...\n",layeridx);

  /* run for each attention head */
  for(h=thr;h<NUMHEADS;h+=numthr)
  {
    float att[here+1];

    /* query * keys = attentions */
    for(i=0;i<=here;i++)
    {
      float a=conv1dline(0,q+h*HEADSIZE,l->k+i*WVSIZE+h*HEADSIZE,HEADSIZE);
      att[i] = a * RSQRT_HEADSIZE;
    }

    /* softmax attentions to make them sum up to 1.0 */
    float max=att[0];
    for(i=1;i<=here;i++) if(att[i]>max) max=att[i];
    float sum=0;
    for(i=0;i<=here;i++)
    {
      float a=exp(att[i]-max);
      att[i]=a;
      sum+=a;
    }
    float sumr=1.0/sum;
    for(i=0;i<=here;i++) att[i]*=sumr;

    /* store attention data for visualization */
    if(attentions)
    for(i=0;i<=here;i++)
      attentions[i*NUMHEADS*NUMLAYERS+layeridx*NUMHEADS+h]=att[i];

    /* apply attentions to values */
    for(j=0;j<HEADSIZE;j++)
      tmp[h*HEADSIZE+j]=conv1dline(0,att,l->v+(j+h*HEADSIZE)*CTXSIZE,here+1);
  }

  syncthreads();

  if(verbose>=3) fprintf(stderr,"project...\n",layeridx);

  /* projection (WVSIZExWVSIZE) */
  {pkdflt*w=(pkdflt*)l->attn_cproj_w;
   float*b=l->attn_cproj_b;
   for(i=thr;i<WVSIZE;i+=numthr)
      x[i]+=conv1dline_pkd(b?b[i]:0,tmp,w+WVSIZE*i,WVSIZE);
  }

  syncthreads();

  /* normalize again */
  if(!thr) { normalize(xn,x,l->ln2_b,l->ln2_g);
#ifdef QUANTIZE
  for(i=0;i<WVSIZE;i++) xn[i]/=l->mlp_cfc_w_q; // jakolasku ei muuta tulosta
#endif
  }

  syncthreads();

  if(verbose>=3) fprintf(stderr,"mlp...\n",layeridx);

  /* multilayer perceptron (WVSIZE -> WVSIZE*4 -> WVSIZE) */
  {pkdflt*w=(pkdflt*)l->mlp_cfc_w;
   float*b=l->mlp_cfc_b;
#ifndef HAVE_THREADS
   float mlp[WVSIZE*4];
#endif
   for(i=thr;i<WVSIZE*4;i+=numthr)
   {
     float a=conv1dline_pkd(b?b[i]:0,xn,w+WVSIZE*i,WVSIZE);
     if(!palette) a=0.5*a*(1+tanh(0.7978845676080871*(a+0.044715*a*a*a)));
             else a=a*(1/(1+exp(-a*1.702))); // gelu2 (igpt)
#ifdef QUANTIZE
     mlp[i]=a/l->mlp_cproj_w_q; // safe
#else
     mlp[i]=a;
#endif
   }

   syncthreads();
   w=(pkdflt*)l->mlp_cproj_w;
   b=l->mlp_cproj_b;
   for(i=thr;i<WVSIZE;i+=numthr)
     x[i]+=conv1dline_pkd(b?b[i]:0,mlp,w+WVSIZE*4*i,WVSIZE*4);
  }
}

void*perthread(void*args);

void runModel(float*x,int slot)
{
  int i,j;
  thrglob.numthr=numthreads;

  /* get the token's wordvector (wte) + positional salt (wpe) */
  int tok=slot<0?emptytoken:context[slot];
  float*wv=getwv(tok);
  if(slot<0) slot=0;

  for(i=0;i<WVSIZE;i++)
  {
#ifdef Q8MODE_INWTE
    x[i]=wpe8[i+WVSIZE*(slot+0)]/quanter_wpe+wte8[i+WVSIZE*tok]/quanter_wte;
#else
    x[i]=wpe[i+WVSIZE*(slot+0)]/quanter_wpe+wv[i]/quanter_wte;
#endif
  }

#ifdef HAVE_THREADS
  /* alloc memory for some variables we can't keep local when threaded */
  if(!thrglob.q) thrglob.q=malloc(WVSIZE*sizeof(float));
  if(!thrglob.tmp) thrglob.tmp=malloc(WVSIZE*sizeof(float));
  if(!thrglob.xn) thrglob.xn=malloc(WVSIZE*sizeof(float));
  if(!thrglob.mlp) thrglob.mlp=malloc(WVSIZE*4*sizeof(float));
#endif

#ifdef HAVE_THREADS
  if(numthreads<=1)
  {
#endif
    for(i=0;i<NUMLAYERS;i++)
    {
      if(verbose>=2) fprintf(stderr,"layer %d\n",i);
      runLayer(x,i,slot,0,1);
    }
#ifdef HAVE_THREADS
  } else
  {
    thrglob.x=x;
    thrglob.slot=slot;
    thrglob.numthr=numthreads;
    int thread_args[numthreads];
    pthread_barrier_init(&thrglob.barrier,NULL,numthreads);
    for(i=0;i<numthreads;i++)
    {
      thread_args[i]=i;
      pthread_create(&thrglob.t[i],NULL,perthread,&thread_args[i]);
    }
    for(i=0;i<numthreads;i++) pthread_join(thrglob.t[i],NULL);
    pthread_barrier_destroy(&thrglob.barrier);
  }
#endif

  /* normalize the final result */
  normalize(x,x,lnf_b,lnf_g);

  /* cache it if cache is present */
  if(outputcache && slot)
    memcpy(outputcache+WVSIZE*slot,x,WVSIZE*sizeof(float));
}

#ifdef HAVE_THREADS
void*perthread(void*args)
{
  int i;
  int thr=*((int*)args);
  if(verbose>=2) fprintf(stderr,"thread %d started\n",thr);
  for(i=0;i<NUMLAYERS;i++)
  {
    runLayer(thrglob.x,i,thrglob.slot,thr,thrglob.numthr);
    syncthreads();
  }
  if(verbose>=2) fprintf(stderr,"thread %d finished\n",thr);
}
#endif
