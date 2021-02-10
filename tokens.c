#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common.h"

char*tokendata;

int loadtokens(char*path)
{
  int tokendatalgt;
  tokendata=(char*)readfile("tokens.dat",&tokendatalgt,path);
  if(!tokendata)
  {
    fprintf(stderr,"couldn't find tokens.dat...\n");
    return 1;
  }

  numtokens=0;
  char*s=tokendata;
  while(s<tokendata+tokendatalgt)
  {
    s+=strlen(s)+1;
    numtokens++;
  }

  tokenstrings=malloc((numtokens+1+MAXUSERTOKENS)*sizeof(char*));
  s=tokendata;
  int i=0;
  while(s<tokendata+tokendatalgt)
  {
    tokenstrings[i]=s;
    s+=strlen(s)+1;
    i++;
  }
  tokenstrings[i]=NULL;
  fprintf(stderr,"%d tokens retrieved\n",i);
  return 0;
}

int loadpalette(char*path)
{
  int sz,i;
  palette=(float*)readfile("kmeans_centers.npy.raw",&sz,path);
  if(!sz)
  {
    fprintf(stderr,"couldn't find color clusters\n");
    return 1;
  }
  numtokens=sz/(3*sizeof(float));

  // normalize to 0..255
  float min=0,max=0;
  for(i=0;i<numtokens*3;i++)
  {
    if(palette[i]<min) min=palette[i];
    if(palette[i]>max) max=palette[i];
  }
  for(i=0;i<numtokens*3;i++)
  {
    int a=round(255*(palette[i]-min)/(max-min));
    palette[i]=a;
  }

  // create pseudotokens
  tokenstrings=malloc((numtokens+1+MAXUSERTOKENS)*sizeof(char*));
  for(i=0;i<numtokens;i++)
  {
    char buf[8];
    sprintf(buf," %02x%02x%02x",
      (int)(palette[i*3+0]),
      (int)(palette[i*3+1]),
      (int)(palette[i*3+2]));
    tokenstrings[i]=strdup(buf);
  }
  tokenstrings[numtokens]=NULL;
  fprintf(stderr,"%d colors imported\n",numtokens);
  return 0;
}

int allocusertoken(float*wv,char*name)
{
  int tok;
  if(numtokens>=nummodeltokens+MAXUSERTOKENS) return -1;
  tok=numtokens;
  numtokens++;
  userwte[tok-nummodeltokens]=malloc(WVSIZE*sizeof(float));
  tokenstrings[tok]=strdup(name?name:"UNNAMED");
  tokenstrings[tok+1]=NULL;
  memcpy(&userwte[tok-nummodeltokens],wv,WVSIZE*sizeof(float));
  return tok;
}

float*getwv(int token)
{
  if(token<0 || token>=numtokens) return sos; // ?sos:NULL;
  if(token<nummodeltokens) return wte+WVSIZE*token;
  return userwte+WVSIZE*(token-nummodeltokens);
}

float*getwv_final(int token)
{
  if(!wtet) return getwv(token);
  if(token<0 || token>=numtokens) return sos;
  if(token<nummodeltokens) return wtet+WVSIZE*token;
  return userwte+WVSIZE*(token-nummodeltokens);
}

void nametoken(int tok,char*name)
{
  if(tok>=nummodeltokens) free(tokenstrings[tok]);
    // ^ slight memory leak here if renaming pre-renamed model token.
    // should rather disable the modeltoken and replace it with usertoken
  tokenstrings[tok]=strdup(name);
}

int strmatchlgt(char*s0,char*s1)
{
  int i=0;
  while(*s0 && *s1)
  {
    if(!*s0) return i;
    if(*s0!=*s1) return 0;
    s0++;
    s1++;
    i++;
  }
  if(!*s0) return i;
  return 0;
}

int tokenize(char*src) // slow! (but not too slow)
{
  int i;
  int best=0,where=-1;
  for(i=0;;i++)
  {
    if(!tokenstrings[i]) break;
    int matchlgt=strmatchlgt(tokenstrings[i],src);
    if(matchlgt>best)
    {
      best=matchlgt;
      where=i;
    }
  }
  //fprintf(stderr,"%s bestmatch %d lgt %d\n",src,where,best);
  return where;
}

int tokenize_to_context(char*src,int idx)
{
  while(*src && idx<CTXSIZE)
  {
    int token=tokenize(src);
    if(token<0) return idx;
    context[idx]=token;
    src+=strlen(tokenstrings[token]);
    idx++;
  }
  return idx;
}

void dumpwvstats(float*wv)
{
  float mean=0;
  float max=0;
  float min=0;
  int i;
  for(i=0;i<WVSIZE;i++)
  {
    float a=wv[i];
    mean+=a;
    if(a>max) max=a;
    if(a<min) min=a;
    fprintf(stderr,"%f ",a);
  }
  mean/=WVSIZE;
  fprintf(stderr,"\nmean %f min %f max %f\n",mean,min,max);
}

inline float conv1dline(float a,float*v,float*m,int wdt)
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

#define CRUNCHSZ 256
void crunchvector(float*o,float*v,int lgt)
{
  int i,j;
  for(i=0;i<CRUNCHSZ;i++)
  {
    float a=0;
    for(j=i;j<lgt;j+=CRUNCHSZ) a+=v[j];
    o[i]=a;
  }
}

int (matchToTokens_cmp)(const void*a,const void*b)
{
  return (((match_t*)b)->prob < ((match_t*)a)->prob) ? -1:1;
}

// optimization todo: wte_min (uses vec16) jolla top-80 tms sortattavaksi
void matchToTokens(float*wv,match_t*o,int num,float temp) // outputs tuples of (dist,token)
{
  int i,j;
  match_t*t=malloc(sizeof(match_t)*numtokens);
/*
  float wvww[CRUNCHSZ];
  float*ww=malloc(CRUNCHSZ*sizeof(float)*numtokens);
  for(i=0;i<numtokens;i++)
  {
    crunchvector(&ww[CRUNCHSZ*i],getwv(i),WVSIZE);
  }
*/
  int8_t wv8[WVSIZE];
  float wvv[WVSIZE];
  for(i=0;i<WVSIZE;i++)
  {
    float a=wv[i]*quanter_wte; // safe
    wvv[i]=a;
    if(a<-128)a=-128;
    if(a>127)a=127;
    wv8[i]=a;
  }

/*
  crunchvector(wvww,wvv,WVSIZE);

  // "top_k" phase
  for(j=0;j<numtokens;j++)
  {
    float*compwv=ww+j*CRUNCHSZ;
#ifdef Q8MODE_OUTWTE
    int cossim=conv1dline_ii(0,wv8,wte8+j*WVSIZE,8);
#else
    float cossim=conv1dline(0,compwv,wvww,CRUNCHSZ);
#endif
    t[j*2]=cossim;
    t[j*2+1]=j; // *(((int*)&t[j*2+1]))=
  }
  qsort(t,numtokens,sizeof(float)*2,matchToTokens_cmp);
*/

  // "top_s" phase
  for(i=0,j=0;j<numtokens;j++)
  {
    if(tokenflags[j]>=0)
    {
      float*compwv=getwv_final(j);
#ifdef Q8MODE_OUTWTE
      int cossim=conv1dline_ii(0,wv8,wte8+j*WVSIZE,WVSIZE);
#else
      float cossim=conv1dline(0,wv,compwv,WVSIZE);
#endif
      t[i].prob=cossim/(temp*quanter_wte*quanter_wte);
      t[i].tok=j;
      i++; 
    }
    //t[j*2+1]=j; // *(((int*)&t[j*2+1]))=
  }
//  for(i=0;i<512;i++) fprintf(stderr,"%f ",t[i*2]);
//  fprintf(stderr,"\n");

  qsort(t,i,sizeof(float)*2,matchToTokens_cmp);
 
  if(targetwv)
  {
    for(i=0;i<num;i++)
    {
      float cossim=conv1dline(0,targetwv,wte+((int)t[i].tok)*WVSIZE,WVSIZE);
      t[i].prob+=cossim/(temp*quanter_wte*quanter_wte);
    }
    qsort(t,i,sizeof(match_t),matchToTokens_cmp);
  }
  
//  for(i=0;i<num;i++)
//  {
//    if(tokenflags[(int)(t[i*2+1])]>0) t[i*2]=(t[i*2]*3+t[0])/4.0;
//  }

  // softmax  
  float max=t[0].prob;
  for(i=1;i<num;i++) if(t[i].prob>max) max=t[i].prob;
  float sum=0;
  for(i=0;i<num;i++)
  {
    float a=exp(t[i].prob-max);
    t[i].prob=a;
    sum+=a;
  }
  float sumr=1.0/sum;
  for(i=0;i<num;i++)
  {
    o[i].prob = t[i].prob*sumr;
    o[i].tok  = t[i].tok;
  }
  free(t);
}

int pickmatch_(match_t*list,int sz,float minp)
{
  int i;
  float a=frand();
  if(list[0].prob<minp || list[0].prob>0.98) return 0;

  if(list[0].prob<0.75 && (rand()&1))
  for(i=0;i<sz;i++)
  {
    int t=list[i].tok;
    if(tokenflags[t]==1 && list[i].prob>0.002)
    {
      tokenflags[t]=0;
      fprintf(stderr,"<>");
      return i;
    }
  }

  for(i=0;i<sz;i++)
  {
    float p=list[i].prob;
    if(p<minp) { i=0; p=list[i].prob; }
    a-=p;
    if(a<=0) return i;
  }
  return 0;
}

int pickmatch(match_t*list,int sz,float minp)
{
  int i=pickmatch_(list,sz,minp);
  int t=list[i].tok;
  while(tokenflags[t]==2)
  {
    fprintf(stderr,"<!%s>",tokenstrings[t]);
    tokenflags[t]=0;
    i=pickmatch_(list,sz,minp);
    t=list[i].tok;
  }
  return i;
}
