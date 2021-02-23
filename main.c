#include "common.h"

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
  /*if(!(sz&1023))*/ s=realloc(s,sz+1);
  s[sz]='\0';
  return s;
}

void*readtextfile(char*fn,char*path)
{
  int sz;
  char*s=readfile(fn,&sz,path);
  return zeroterminate(s,sz);
}

void*readfile_mmap(char*fn,int*lgt_ret)
{
  // TODO impl
  // PROT_READ
}

float testquestion(char*prompt,char*answer)
{
  int i;
  int answtok=tokenize(answer);
  int here=tokenize_to_context(prompt,0);
  for(i=0;i<here;i++) runModel(currwv,i);
  matchToTokens(currwv,matchlist,40,1.0);
  fprintf(stderr,"%s [%s] : ",prompt,answer);
  for(i=0;i<40;i++)
    if(matchlist[i].tok==answtok) {
      fprintf(stderr,"%f%% (%d)\n",matchlist[i].prob*100.0,i);
      return matchlist[i].prob/(i+1.0);
    }
  fprintf(stderr,"FAIL\n");
  return 0;
}

int branches[256*16];

int (branches_cmp)(const void*a,const void*b)
{
  return ((int*)b)[0] < ((int*)a)[0] ? -1:1;
}

#if (0)
// experimental
void findbranches(char*prompt,int num,int maxlgt)
{
  int i,j,k;
  int*branches=malloc(sizeof(int)*num*2*maxlgt);
  int emptytoken=tokenize("<|endoftext|>");
  for(i=0;i<maxlgt*num*2;i++) branches[i]=emptytoken;
  for(i=0;i<CTXSIZE;i++) context[i]=emptytoken;
  printf("Prompt: %s\n",prompt);
  int here=tokenize_to_context(prompt,0);
  for(i=0;i<here;i++) runModel(currwv,i);

  matchToTokens(currwv,matchlist,num,2.0);
  for(i=0;i<num;i++)
  {
    branches[i*maxlgt+0] = matchlist[i].prob*1000000000;
    branches[i*maxlgt+1] = matchlist[i].tok;
  }
  fprintf(stderr,"Results:\n");
  for(i=0;i<num;i++)
  {
    printf("%d. %d: ",i,branches[i*maxlgt+0]);
    for(j=0;j<maxlgt-1;j++)
    {
      if(branches[i*maxlgt+j+1]==emptytoken) break;
      printf("%s ",tokenstrings[branches[i*maxlgt+j+1]]);
    }
    printf("\n");
  }
  for(;;){
  for(i=0;i<maxlgt-1;i++)
  {
    if(i==maxlgt-2) return;
    int t=branches[i+1];
    context[here+i]=t;
    if(t==emptytoken) break;
    runModel(currwv,here+i);
  }
  printf(">");
  for(j=here;j<=here+i-1;j++) printf("%s",tokenstrings[context[j]]);
  printf("\n");
  runModel(currwv,here+i-1);
  matchToTokens(currwv,matchlist,num,1.2);
  for(j=0;j<num;j++)
  {
    branches[(num+j)*maxlgt+0] = branches[0]*matchlist[j].prob;
    for(k=0;k<i;k++)
    {
      branches[(num+j)*maxlgt+k+1] = context[here+k];
    }
    branches[(num+j)*maxlgt+i+1] = matchlist[j].tok;
    branches[(num+j)*maxlgt+i+2] = emptytoken;
  }
  branches[0]=0;
  qsort(branches,num*2,sizeof(int)*maxlgt,branches_cmp);
  /*
  printf("\033[HResults:\n");
  for(i=0;i<50;i++)
  {
    printf("\033[K%d. %d: ",i,branches[i*16+0]);
    for(j=0;j<15;j++)
    {
      if(branches[i*16+j+1]==emptytoken) break;
      printf("%s",tokens[branches[i*16+j+1]]);
    }
    printf("\n");
  }
  */
  }
}

void iqtest()
{
  float score=0;
  score+=testquestion(" The capital of Finland is"," Helsinki");
  score+=testquestion(" The capital of France is"," Paris");
  score+=testquestion(" The capital of Sweden is"," Stockholm");
  score+=testquestion(" Paris is located in"," France");
  score+=testquestion(" Paris is located in the country of"," France");
  score+=testquestion(" Stockholm is located in the country of"," Sweden");
  score+=testquestion(" Christmas is celebrated in the month of"," December");
  score+=testquestion(" The color of the sky is"," blue");
  score+=testquestion(" The color of grass is"," green");
  score+=testquestion(" The opposite of infrared is"," ultraviolet");
  score+=testquestion(" The opposite of large is"," small");
  score+=testquestion(" 1 + 1 ="," 2");
  score+=testquestion(" 2 + 3 ="," 5");
  fprintf(stderr,"Total score: %f\n",score);
}
#endif

void benchmark(char*prompt,int lgt)
{
  int i,here,match,tok;
  here=tokenize_to_context(prompt,0);
  fprintf(stderr,"%s",prompt);
  for(i=0;i<here-1;i++) runModel(currwv,i);
  for(i=here-1;i<lgt;i++)
  {
    runModel(currwv,i);
    matchToTokens(currwv,matchlist,80,1.2);
    match=pickmatch(matchlist,80,0.001);
    tok=matchlist[match].tok;
    context[i+1]=tok;
    if(tok>=0) fprintf(stderr,"%s",tokenstrings[tok]);
  }
  fprintf(stderr,"\n");
}

void generate(int start,int genstart_,int genend_)
{
  currslot=start;
  genstart=genstart_;
  genend=genend_;
  char reloading=0;
  while(currslot<genend)
  {
    int tok;
    runModel(currwv,currslot);
    tok=context[currslot];
    if(tok>=0 && currslot<genstart)
    {
      if(!reloading)
      {
        printf("%s",tokenstrings[tok]);
        fflush(stdout);
      }
      else
        if(verbose>=1) fprintf(stderr,"%s",tokenstrings[tok]);
    }
    currslot++;
    if(currslot>=genstart)
    {
      int match;
      reloading=0;
      matchToTokens(currwv,matchlist,nummatches,temperature);
      match=pickmatch(matchlist,nummatches,minp);
      tok=matchlist[match].tok;
      context[currslot]=tok;
      if(tok>=0)
      {
        printf("%s",tokenstrings[tok]);
        fflush(stdout);
      }
    }
    if(currslot>=CTXSIZE-1 && genend>CTXSIZE)
    {
      purgeoldcontext(CTXSIZE/2);
      currslot=0;
      genstart=CTXSIZE/2-2;
      reloading=1;
    }
  }
}

void init(char*modelpath)
{
  int rc;
  context=malloc(CTXSIZE*sizeof(int));
  currwv=malloc(WVSIZE*sizeof(float));
  matchlist=malloc(MAXNUMMATCHES*sizeof(match_t));

  fprintf(stderr,"load tokens...\n");
  rc=loadtokens(modelpath);
  if(rc)
  {
    fprintf(stderr,"load palette...\n");
    rc=loadpalette(modelpath);
    emptytoken=-1;
  } else
  {
    emptytoken=tokenize("<|endoftext|>");
  }
  tokenflags=malloc((numtokens+MAXUSERTOKENS)*sizeof(char));
  memset(tokenflags,0,(numtokens+MAXUSERTOKENS)*sizeof(char));
  nummodeltokens=numtokens;

  fprintf(stderr,"load model...\n");
  loadmodel(modelpath);
#ifdef QUANTIZE
  quantize();
#endif
}

float*buildcustomwv(char*fn)
{
  float*wv;
  char*s0=readtextfile(fn,NULL);
  char*s;
  int i;
  if(!s0) return NULL;
  wv=malloc(WVSIZE*sizeof(float));
  for(i=0;i<WVSIZE;i++) wv[i]=0;
  int numpos=0,numneg=0;
  s=s0;
  while(*s)
  {
    int t=tokenize(s+1);
    //fprintf(stderr,"%c %d\n",s[0],t);
    if(s[0]=='+')
    {
      for(i=0;i<WVSIZE;i++) wv[i]-=wte[t*WVSIZE+i];
      numpos++;
      tokenflags[t]=1;

    } else
    if(s[0]=='-')
    {
      for(i=0;i<WVSIZE;i++) wv[i]+=wte[t*WVSIZE+i];
      numneg++;
      tokenflags[t]=2;
    } else
    if(s[0]=='0')
    {
      tokenflags[t]=-1;
    }
    while(*s!='\n' && *s!='\0') s++;
    if(*s=='\n') s++;
  }
  fprintf(stderr,"pos %d neg %d\n",numpos,numneg);
  float sum=0.0;
  for(i=0;i<WVSIZE;i++) sum+=fabs(wv[i]);
  sum/=WVSIZE;
  sum=quanter_wte/(sum*10);

  for(i=0;i<WVSIZE;i++) wv[i]*=sum;
  //for(i=0;i<WVSIZE;i++) wv[i]/=(numpos>numneg?numpos:numneg);
  free(s0);
  return wv;
}

int matchcolortopalette(int r,int g,int b)
{
  int i;
  float min;
  int where;
  for(i=0;i<numtokens;i++)
  {
    float*p=palette+i*3;
    float d2=(p[0]-r)*(p[0]-r)+
             (p[1]-g)*(p[1]-g)+
             (p[2]-b)*(p[2]-b);
    if(i==0 || d2<min) { min=d2; where=i; }
  }
  return where;
}

int tokenize_image(char*fn)
{ 
  int ctxdim=sqrt(CTXSIZE);
  int i,j,w,h;
  SDL_Surface*img=IMG_Load(fn);
  if(!img) return;
  fprintf(stderr,"loading image %s: %d x %d",fn,img->w,img->h);
  w=img->w; if(w>ctxdim) w=ctxdim;
  h=img->h; if(h>ctxdim) h=ctxdim;
  fprintf(stderr," -> %d x %d\n",w,h);
  for(j=0;j<h;j++)
  for(i=0;i<w;i++)
  {
    Uint8 r,g,b;
    int c=*((int*)(img->pixels+img->pitch*j+img->format->BytesPerPixel*i));
    SDL_GetRGB(c,img->format,&r,&g,&b);
    context[j*ctxdim+i]=matchcolortopalette(r,g,b);
  }
  fprintf(stderr,"loaded\n");
  return (h-1)*ctxdim+w;
  //SDL_FreeSurface(img);
}

#ifdef ENABLE_TTYUI
int handlesignal(int s)
{
//  signal(SIGINT,SIG_DFL);
  ttyui();
//  signal(SIGINT,handlesignal);
  return 0;
}
#endif

int main(int argc,char**argv)
{
  // settings & defaults
  temperature=1.0;
  modelpath="net";
  nummatches=80;
  minp=0.0001;
  numthreads=1;
  int seed=0;
  verbose=0;

  // other runtime options
  char*prompt=NULL;
  char*promptfile=NULL;
  char*configfile=NULL;
  int lengthtogen=0;
  char wannastartui=0;
  char wannabenchmark=0;

  int i,here=0;

  for(i=1;i<argc;i++)
  {
    if(argv[i][0]=='-')
    {
      char*s=argv[i]+1;
      while(*s=='-') s++;
      for(;*s;s++)
      {
        if(*s=='h')
        {
          fprintf(stderr,
            "Usage: %s <options> [modelpath]\n"
            "-h          show this help\n"
            "-p \"<text>\" generate text starting with the given prompt\n"
            "-f file.txt read prompt from file\n"
            "-l 512      set maximum length of text to output (in tokens)\n"
            "-t 1.0      set noise temperature for match randomization\n"
            "-T 4        set number of threads\n"
            "-s 123456   set random number seed (0 = use timer)\n"
            "-c conf.txt read config commands from file\n"
            "-u          start ui even with -p and -f\n"
            "-b          run benchmark\n"
            "-v          verbose/debug output\n"
            );
          exit(1);
        }
        if(i<argc-1)
        {
          if(*s=='p')
            prompt=argv[++i];
          if(*s=='f')
            promptfile=argv[++i];
          if(*s=='t')
            temperature=atof(argv[++i]);
          if(*s=='T')
            numthreads=atoi(argv[++i]);
          if(*s=='s')
            seed=atoi(argv[++i]);
          if(*s=='c')
            configfile=argv[++i];
          if(*s=='l')
            lengthtogen=atoi(argv[++i]);
        }
        if(*s=='u')
          wannastartui=1;
        if(*s=='v')
          verbose++;
        if(*s=='b')
        {
          wannabenchmark=1;
          temperature=1.2;
          seed=70177;
          lengthtogen=256;
          nummatches=40;
          prompt=" Suddenly, a magical floppy disk";
        }
      }
    } else modelpath=argv[i];
  }

  init(modelpath);
  if(!seed)
  {
    seed=time(NULL);
    fprintf(stderr,"seed from time(): %d\n",seed);
  }
  srand(seed);
/*
  TODO proper configfile
*/
  if(configfile) targetwv=buildcustomwv(configfile);
  targetwv=NULL;

  if(promptfile && !palette)
    prompt=readtextfile(promptfile,NULL);

  if(wannastartui || !prompt) ui_init();

  context[0]=emptytoken;
  int promptlgt=0;
  if(prompt) promptlgt=tokenize_to_context(prompt,0);
  if(palette && promptfile) promptlgt=tokenize_image(promptfile);
  fprintf(stderr,"Prompt length: %d tokens\n",promptlgt);

  if(wannastartui || !prompt)
  {
    ui_run();
    return 0;
  }

#ifdef ENABLE_TTYUI
  signal(SIGINT,handlesignal);
#endif
  if(lengthtogen<=0) lengthtogen=CTXSIZE+1;
  generate(0,promptlgt,lengthtogen);

  return 0;
}
