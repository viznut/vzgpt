#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common.h"

/*** load the token dictionary (for normal text-based gpt-2 models) ***/

int loadtokens_from_tokendata(char*tokendata,int numtoks)
{
  char*s=tokendata;
  tokenstrings=malloc((numtoks+1+MAXUSERTOKENS)*sizeof(char*));
  int i=0;
  while(i<numtoks)
  {
    tokenstrings[i]=s;
    s+=strlen(s)+1;
    i++;
  }
  tokenstrings[i]=NULL;
  fprintf(stderr,"%d tokenstrings retrieved\n",i);
  numtokens=i;
  return 0;
}

int loadtokens(char*path)
{
  int tokendatalgt;
  tokendata=(char*)readfile("tokens.dat",&tokendatalgt,path);
  if(!tokendata)
  {
    fprintf(stderr,"couldn't load tokens.dat...\n");
    return 1;
  }
  char*s=tokendata;
  int numtoks=0;
  while(s<tokendata+tokendatalgt)
  {
    s+=strlen(s)+1;
    numtoks++;
  }
  return loadtokens_from_tokendata(tokendata,numtoks);
}

/*** load the color palette (for imagegpt models) ***/

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

/*** helper functions ***/

// TODO fix for PKD
int allocusertoken(float*wv,char*name)
{
  int tok;
  if(numtokens>=nummodeltokens+MAXUSERTOKENS) return -1;
  tok=numtokens;
  numtokens++;
  userwte[tok-nummodeltokens]=malloc(WVSIZE*sizeof(pkdflt));
  tokenstrings[tok]=strdup(name?name:"UNNAMED");
  tokenstrings[tok+1]=NULL;
  memcpy(&userwte[tok-nummodeltokens],wv,WVSIZE*sizeof(float));
  return tok;
}

wte_t*getwv(int token)
{
  if(token<0 || token>=numtokens) return sos;
  if(token<nummodeltokens) return wte+WVSIZE*token;
  return userwte+WVSIZE*(token-nummodeltokens);
}

wte_t*getwv_final(int token)
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

#if (0)
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
#endif

int (matchToTokens_cmp)(const void*a,const void*b)
{
  return (((match_t*)b)->prob < ((match_t*)a)->prob) ? -1:1;
}

/*** matches a word vector against the token dictionary ***/

// optimization todo: wte_min (uses vec16) jolla top-80 tms sortattavaksi
void matchToTokens(float*wv,match_t*o,int num,float temp) // outputs tuples of (dist,token)
{
  int i,j;
//  fprintf(stderr,"numtokens=%d\n",numtokens);
  match_t*t=malloc(sizeof(match_t)*numtokens);

#ifdef USE_PKD_WTE
  int32_t wv32[WVSIZE];
  for(i=0;i<WVSIZE;i++)
  {
    int64_t a=wv[i]*quanter_wte; // safe
    wv32[i]=a;
    //if(a<-128)a=-128;
    //if(a>127)a=127;
    //wv8[i]=a;
  }
#endif

  // "top_s" phase
  for(i=0,j=0;j<numtokens;j++)
  {
    if(tokenflags[j]>=0)
    {
      wte_t*compwv=getwv_final(j);
#ifdef Q8MODE_OUTWTE
      int cossim=conv1dline_ii(0,wv8,wte8+j*WVSIZE,WVSIZE);
#else
#ifdef USE_PKD_WTE
      int64_t cossim=conv1dline_pkdwte(0,wv32,compwv,WVSIZE);
#else
      float cossim=conv1dline(0,wv,compwv,WVSIZE);
#endif
#endif
      t[i].prob=cossim/(temp*quanter_wte*quanter_wte);
      t[i].tok=j;
      i++;
    }
    //t[j*2+1]=j; // *(((int*)&t[j*2+1]))=
  }
//  for(i=0;i<512;i++) fprintf(stderr,"%f ",t[i*2]);
//  fprintf(stderr,"\n");

  qsort(t,i,sizeof(match_t),matchToTokens_cmp);

#if (0)
  if(targetwv)
  {
    for(i=0;i<num;i++)
    {
      float cossim=conv1dline(0,targetwv,wte+((int)t[i].tok)*WVSIZE,WVSIZE);
      t[i].prob+=cossim/(temp*quanter_wte*quanter_wte);
    }
    qsort(t,i,sizeof(match_t),matchToTokens_cmp);
  }
#endif
  
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

/* picks a random match from the match list.
 * tokenflags[] allows for some token-specific options.
 */

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
      //fprintf(stderr,"<>");
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

int replacetoken(int t)
{
  if(tokenflags[t]==4 || tokenflags[t]==5)
  {
    if(tokenflags[t]==4) tokenflags[t]=0;
    t=tokenrepls[t];
    fprintf(stderr,"<R>");
  }
  return t;
}

int pickmatch(match_t*list,int sz,float minp)
{
  int i=pickmatch_(list,sz,minp);
  int t=list[i].tok;
  while(tokenflags[t]==2)
  {
    //fprintf(stderr,"<!%s>",tokenstrings[t]);
    tokenflags[t]=0;
    i=pickmatch_(list,sz,minp);
    t=list[i].tok;
  }
  return i;
}
