#include "common.h"

/* math helper functions */

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

/* globals for multithreading */

#ifdef HAVE_THREADS
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

void syncthreads()
{
  if(thrglob.numthr<=1) return;
  pthread_barrier_wait(&thrglob.barrier);
}
#else
#define syncthreads()
#endif

/* from here on: the code that actually runs the model */

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
#ifdef HAVE_THREADS
  thrglob.numthr=numthreads;
#endif

  /* get the token's wordvector (wte) + positional salt (wpe) */
  int tok=slot<0?emptytoken:context[slot];
  wte_t*wv=getwv(tok);
  if(slot<0) slot=0;

  for(i=0;i<WVSIZE;i++)
  {
#ifdef Q8MODE_INWTE
    x[i]=wpe8[i+WVSIZE*slot]/quanter_wpe+wte8[i+WVSIZE*tok]/quanter_wte;
#else
#ifdef USE_PKDFLT
    x[i]=UNPKFLT(wpe[i+WVSIZE*slot])/quanter_wpe;
#else
    x[i]=wpe[i+WVSIZE*slot]/quanter_wpe;
#endif
#ifdef USE_PKD_WTE
    x[i]+=((float)wv[i])/quanter_wte; // UNPKFLT(wv[i])/quanter_wte;
#else
    x[i]+=wv[i]/quanter_wte;
#endif
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
    if(numthreads>MAXNUMTHR) numthreads=MAXNUMTHR;
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
