#include "common.h"

/* file management functions */

void*readfile(char*fn,int*lgt_ret,char*path)
{
  void*buf=NULL;
  int i=0;
  FILE*f;
  char fnb[120];
  char*pathfn;

  if(path)
  {
    //path=alloca(strlen(fn)+strlen(path)+2);
    snprintf(fnb,119,"%s/%s",path,fn);
    pathfn=fnb;
  } else pathfn=fn;
  
  if(lgt_ret) *lgt_ret=0;

  f=fopen(pathfn,"rb");
  if(!f)
  {
    fprintf(stderr,"file not found: %s\n",pathfn);
    return NULL;
  }
  while(!feof(f))
  {
    buf=realloc(buf,i+1024);
    if(!buf)
    {
      fprintf(stderr,"memory allocation failed when fetching file %s!\n",pathfn);
      exit(1);
    }
    i+=fread(buf+i,1,1024,f);
  }
  fclose(f);
  fprintf(stderr,"fetched file %s, lgt=%d\n",pathfn,i);
  if(lgt_ret) *lgt_ret=i;
  return buf;
}

char*zeroterminate(char*s,int sz)
{
  s=realloc(s,sz+1);
  s[sz]='\0';
  return s;
}

char*readtextfile(char*fn,char*path)
{
  int sz;
  char*s=readfile(fn,&sz,path);
  return zeroterminate(s,sz);
}

void*readfile_mmap(char*fn,int*lgt_ret)                                   
{
#ifdef HAVE_MMAP
  int sz;                                                              
  struct stat st;
  int fd=open(fn,O_RDONLY,0);
  if(fd<0) return NULL;
  stat(fn, &st);
  sz=st.st_size;
  if(lgt_ret)*lgt_ret=sz;
  fprintf(stderr,"mmap()\n");
  return mmap(NULL,sz,PROT_READ,MAP_PRIVATE|MAP_POPULATE,fd,0);
#else
  return readfile(fn,lgt_ret,NULL);
#endif
}

/* for loading and saving single-file models (.vzgpt) */

int loadpackedmodel(char*path)
{
  int sz,i;
  int paramsz,wteparamsz;
  int paramsz2=sizeof(float);
  void*m=readfile_mmap(path,&sz); //readfile(path,&sz,NULL); // TODO use mmap!
  if(!m) return 1;
  fprintf(stderr,"file opened, size=%d\n",sz);

  h=(header_t*)m; m+=256;
  if(!(h->fileformat[0]=='V' && h->fileformat[1]=='Z' &&
       h->fileformat[2]=='G'))
  {
    fprintf(stderr,"not a vzgpt file!\n");
    return 1;
  }
  if(WVSIZE!=h->wvsize)
  {
    fprintf(stderr,"WVSIZE mismatch! file %d, exe %d\n",h->wvsize,WVSIZE);
    exit(1);
  }
  if(NUMLAYERS!=h->numlayers)
  {
    fprintf(stderr,"NUMLAYERS mismatch! file %d, exe %d\n",h->numlayers,NUMLAYERS);
    exit(1);
  }
  if(NUMHEADS!=h->numheads)
  {
    fprintf(stderr,"NUMHEADS mismatch! file %d, exe %d\n",h->numlayers,NUMLAYERS);
    exit(1);
  }
  numtokens=h->numtokens;
  fprintf(stderr,"numtokens=%d\n",numtokens);
  if(CTXSIZE!=h->ctxsize)
  {
    fprintf(stderr,"CTXSIZE mismatch! file %d, exe %d\n",h->ctxsize,CTXSIZE);
    exit(1);
  }
  if(HEADSIZE!=h->headsize)
  {
    fprintf(stderr,"HEADSIZE mismatch! file %d, exe %d\n",h->headsize,HEADSIZE);
    exit(1);
  }
  if(h->paramformat!=PFMT_BF16)
  {
    fprintf(stderr,"paramformat must be PFMT_BF16!\n");
    exit(1);
  }
  paramsz=2;
  if(h->wteformat!=PFMT_INT16)
  {
    fprintf(stderr,"wteformat must be PFMT_INT16!\n");
    exit(1);
  }
  wteparamsz=2;
  quanter_wte=h->quanter_wte;
  fprintf(stderr,"quanter_wte=%f\n",quanter_wte);

  wte=m; m+=wteparamsz*numtokens*WVSIZE;
  wpe=m; m+=paramsz*CTXSIZE*WVSIZE;
  lnf_g=(pkdflt*)m; m+=paramsz2*WVSIZE;
  if(h->flags&FLAG_HAVE_BASES)
  {
    fprintf(stderr,"we have bases\n");
    lnf_b=m; m+=paramsz2*WVSIZE;
  }
  if(h->flags&FLAG_HAVE_WTET)
  {
    fprintf(stderr,"todo implement wtet\n");
    exit(1);
  }
  if(h->flags&FLAG_HAVE_SOS)
  {
    fprintf(stderr,"todo implement sos\n");
    exit(1);
  }
  layers=malloc(sizeof(hlayer)*NUMLAYERS);
  for(i=0;i<NUMLAYERS;i++)
  {
    fprintf(stderr,"layer %d starts at %d\n",i,m-(void*)h);
    layers[i].ln1_g=m; m+=paramsz2*WVSIZE;
    layers[i].ln2_g=m; m+=paramsz2*WVSIZE;
    layers[i].mlp_cfc_w=m; m+=paramsz*WVSIZE*WVSIZE*4;
    layers[i].mlp_cproj_w=m; m+=paramsz*WVSIZE*WVSIZE*4;
    layers[i].attn_cattn_w=m; m+=paramsz*WVSIZE*3*WVSIZE;
    layers[i].attn_cproj_w=m; m+=paramsz*WVSIZE*WVSIZE;
    if(h->flags&FLAG_HAVE_BASES)
    {
      layers[i].ln1_b=m; m+=paramsz2*WVSIZE;
      layers[i].ln2_b=m; m+=paramsz2*WVSIZE;
      layers[i].mlp_cfc_b=m; m+=paramsz2*WVSIZE*4;
      layers[i].mlp_cproj_b=m; m+=paramsz2*WVSIZE;
      layers[i].attn_cattn_b=m; m+=paramsz2*WVSIZE*3;
      layers[i].attn_cproj_b=m; m+=paramsz2*WVSIZE;
    }
    layers[i].k=malloc(CTXSIZE*WVSIZE*sizeof(float));
    layers[i].v=malloc(CTXSIZE*WVSIZE*sizeof(float));
  }
  if(h->flags&FLAG_HAVE_PALETTE)
  {
    fprintf(stderr,"todo implement palette\n");
    exit(1);
  }
  if(h->flags&FLAG_HAVE_TOKENSTRINGS)
    loadtokens_from_tokendata(m,numtokens);
  fprintf(stderr,"packed model loaded (size %d + tokendata)\n",m-(void*)h);
  fprintf(stderr,"first tokens: %s %s %s\n",
    tokenstrings[0],tokenstrings[1],tokenstrings[2]); // oikein
  return 0;
}

int savepackedmodel(char*fn)
{
  int i;
  header_t h;
  int flags=0;
  int paramsz,wteparamsz;
  int paramsz2=sizeof(float);
  FILE*f=fopen(fn,"wb");
  if(!f) return 1;
  fprintf(stderr,"savepackedmodel: %s\n",fn);
  if(wtet) flags|=FLAG_HAVE_WTET;
  if(sos)  flags|=FLAG_HAVE_SOS;
  if(!(flags&(FLAG_HAVE_WTET|FLAG_HAVE_SOS)))
    flags|=FLAG_HAVE_BASES|FLAG_HAVE_TOKENSTRINGS; else
  {
    fprintf(stderr,"looks like igpt\n");
    flags|=FLAG_HAVE_PALETTE;
  }
  h.fileformat[0]='V';
  h.fileformat[1]='Z';
  h.fileformat[2]='G';
  h.fileformat[3]='0';
  h.wvsize=WVSIZE;
  h.numlayers=NUMLAYERS;
  h.numheads=NUMHEADS;
  h.numtokens=numtokens;
  h.ctxsize=CTXSIZE;
  h.headsize=HEADSIZE;
  h.flags=flags;
  h.paramformat=PFMT_BF16; paramsz=sizeof(pkdflt);
  h.wteformat=PFMT_INT16;  wteparamsz=sizeof(wte_t);
  h.reserved0=0;
  h.quanter_wte=quanter_wte;
  fprintf(stderr,"quanter_wte=%f\n",quanter_wte);
  fwrite(&h,sizeof(header_t),1,f);
  for(i=sizeof(header_t);i<256;i++) fputc(0,f);
  fwrite(wte,wteparamsz*numtokens*WVSIZE,1,f);
  fwrite(wpe,paramsz*CTXSIZE*WVSIZE,1,f);
  fwrite(lnf_g,paramsz2*WVSIZE,1,f);
  if(flags&FLAG_HAVE_BASES)
    fwrite(lnf_b,paramsz2*WVSIZE,1,f);
  if(flags&FLAG_HAVE_WTET)
    fwrite(wtet,wteparamsz*numtokens*WVSIZE,1,f);
  if(flags&FLAG_HAVE_SOS)
    fwrite(sos,paramsz2*WVSIZE,1,f);
  for(i=0;i<NUMLAYERS;i++)
  {
    fprintf(stderr,"layer %d starts at %d\n",i,ftell(f));
    fwrite(layers[i].ln1_g,paramsz2*WVSIZE,1,f);
    fwrite(layers[i].ln2_g,paramsz2*WVSIZE,1,f);
    fwrite(layers[i].mlp_cfc_w,paramsz*WVSIZE*WVSIZE*4,1,f);
    fwrite(layers[i].mlp_cproj_w,paramsz*WVSIZE*WVSIZE*4,1,f);
    fwrite(layers[i].attn_cattn_w,paramsz*WVSIZE*3*WVSIZE,1,f);
    fwrite(layers[i].attn_cproj_w,paramsz*WVSIZE*WVSIZE,1,f);
    if(flags&FLAG_HAVE_BASES)
    {
      fwrite(layers[i].ln1_b,paramsz2*WVSIZE,1,f);
      fwrite(layers[i].ln2_b,paramsz2*WVSIZE,1,f);
      fwrite(layers[i].mlp_cfc_b,paramsz2*WVSIZE*4,1,f);
      fwrite(layers[i].mlp_cproj_b,paramsz2*WVSIZE,1,f);
      fwrite(layers[i].attn_cattn_b,paramsz2*WVSIZE*3,1,f); //!
      fwrite(layers[i].attn_cproj_b,paramsz2*WVSIZE,1,f);
    }
  }
  if(flags&FLAG_HAVE_PALETTE)
    fwrite(palette,numtokens*3*sizeof(float),1,f);
  fprintf(stderr,"tokens start at %d\n",ftell(f));
  if(flags&FLAG_HAVE_TOKENSTRINGS)
    for(i=0;i<numtokens;i++)
      fwrite(tokenstrings[i],sizeof(char)*(strlen(tokenstrings[i])+1),1,f);
  fclose(f);
  fprintf(stderr,"packed file written to %s!\n",fn);
  return 0;
}

/* we may need to transpose some matrices when loading from raw dumps */

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

/* we also pack some matrices into more compact float formats */

pkdflt*packtensor(float*s,int lgt)
{
  int i;
  pkdflt*o=malloc(lgt*sizeof(pkdflt));
  for(i=0;i<lgt;i++)
    o[i]=PKFLT(s[i]);
  free(s);
  return o;
}

/* here we load the model from separate raw files */

void importlayerdata(char*path)
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

  wte=(wte_t*)readfile("wte.raw",&sz,path);
  int numwtetokens=sz/(WVSIZE*sizeof(float));
  fprintf(stderr,"wte size: %d wordvecs\n",numwtetokens);
  if(numtokens!=numwtetokens)
  {
    fprintf(stderr,"mismatch with vocabulary size %d!\n",numtokens);
    if(numtokens==0)
    {
      numtokens=numwtetokens;
      fprintf(stderr,"using %d\n",numwtetokens);
    }
  }
  wpe=(pkdflt*)readfile("wpe.raw",&sz,path);
  if(sz!=CTXSIZE*WVSIZE*sizeof(float))
  {
    fprintf(stderr,"wpe size mismatch!\n");
    exit(1);
  }
  wtet=(pkdflt*)readfile("wtet.raw",&sz,path); // igpt-only
  sos=(pkdflt*)readfile("sos.raw",&sz,path); // igpt-only
#ifdef USE_PKDFLT
  wpe=packtensor((float*)wpe,CTXSIZE*WVSIZE);
#endif

  /* bfloat16 causes regression in wte, so we use 16-bit ints there */
#ifdef USE_PKD_WTE
  {
  float max=0;
  float avg=0;
  for(i=0;i<numtokens*WVSIZE;i++)
  {
    float a=fabs(((float*)wte)[i]);
    //avg+=a;
    if(a>max) max=a;
    //uint32_t a=((uint32_t*)wte)[i];
    //a&=0xfffffe00;
    //((uint32_t*)wte)[i]=a;
  }
  //avg/=(numtokens*WVSIZE);
  //fprintf(stderr,"wte max %f avg %f\n",max,avg);
  quanter_wte=32767.5/max;
  fprintf(stderr,"quanter_wte=%f\n",quanter_wte);
  wte_t*qwte=malloc(numtokens*WVSIZE*sizeof(wte_t));
  for(i=0;i<numtokens*WVSIZE;i++)
  {
    int a=floor(((float*)wte)[i]*quanter_wte);
    qwte[i]=a;
  }
  free(wte);
  wte=qwte;
  }
#endif

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
    layers[i].attn_cattn_w=packtensor((float*)layers[i].attn_cattn_w,WVSIZE*3*WVSIZE);
    layers[i].attn_cproj_w=packtensor((float*)layers[i].attn_cproj_w,WVSIZE*WVSIZE);
    layers[i].mlp_cfc_w=packtensor((float*)layers[i].mlp_cfc_w,WVSIZE*WVSIZE*4);
    layers[i].mlp_cproj_w=packtensor((float*)layers[i].mlp_cproj_w,WVSIZE*WVSIZE*4);
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

#if (0)
int isvalidutf8(char*s0)
{
  unsigned char*s=(unsigned char*)s0;
  char pt=0;
  while(*s)
  {
    char t=0;
    if(*s<0x80) t=0; else
    if(*s<0xc0) t=1; else t=2;
//    printf("- %02x %d %d\n",*s,pt,t);
    if(*s<0x20 || *s==0x7f) return 0;
    if(pt==0 && t==1) return 0;
    if(pt==2 && t!=1) return 0;
    pt=t;
    s++;
  }
  if(pt==2) return 0;
//  printf("OK!\n");
  return 1;
}

void printtoken(char*s)
{
  if(isvalidutf8(s))
  {
    printf("%s",s);
  } else
  {
    for(;*s;s++)
    {
      if(*s<0x20 || *s>=0x7F) printf("<%02X>",(unsigned char)*s);
      else putchar(*s);
    }
  }
}
#endif

int isregularfile(char*fn)
{
  int rc;
  struct stat sb;
  rc=lstat(fn,&sb);
  if(rc<0) return 0;
  if(S_ISREG(sb.st_mode)) return 1; else return 0;
}

void loadmodel(char*modelpath)
{
  if(isregularfile(modelpath))
  {
    fprintf(stderr,"loading packed model...\n");
    int rc=loadpackedmodel(modelpath);
    if(rc)
    {
      fprintf(stderr,"failed to load packed model\n");
      exit(1);
    }
    emptytoken=tokenize("<|endoftext|>");
    return;
  }

  fprintf(stderr,"load tokens...\n");
  int rc=loadtokens(modelpath);
  if(rc)
  {
    fprintf(stderr,"load palette...\n");
    rc=loadpalette(modelpath);
    emptytoken=-1;
  } else
  {
    emptytoken=tokenize("<|endoftext|>");
  }
  /*
  if(rc)
  {
    rc=loadpackedmodel(modelpath);
    if(rc)
    {
      fprintf(stderr,"check if the model path (`%s') is valid!\n",modelpath);
      exit(1);
    }
    return;
  }
  */

  importlayerdata(modelpath);
/*
  int i;
  for(i=0;i<numtokens;i++)
  {
    printf("%d. \"",i); printtoken(tokenstrings[i]);
    printf("\"\n");
  }
  exit(0);
*/
}

/* experimental: quantize some matrices into 8-bit integer format */

#ifdef QUANTIZE
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
