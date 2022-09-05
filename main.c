#include "common.h"

#if (0)
/* we sometimes need to check if the network is as smart as it should */

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

/* standardized benchmark run */

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


void purgeoldcontext(int p)
{
  int i,j;

  for(i=0;i<CTXSIZE;i++)
    context[i]=i+p<CTXSIZE?context[i+p]:emptytoken;
#if (0)
  if(context_tags)
  for(i=0;i<CTXSIZE;i++)
    context_tags[i]=i+p<CTXSIZE?context_tags[i+p]:0;
#endif
  // TODO copy k&v, clear rest of k&v
}

int breaks_called=0;

int isBreaker(char*s)
{
  int o=0;
  while(*s)
  {
    if(*s=='\n') o=2;
    if(*s=='.') o=2;
    if(*s=='!') o=2;
    if(*s=='?') o=2;
    if(*s==',') o=1;
    if(*s==':') o=1;
    if(*s==';') o=1;
    if(*s=='"') o=1;
    if(*s=='(') o=1;
    if(*s==')') o=1;
    if(*s>='a' && *s<='z') o=0;
    s++;
  }
  return o;
}

float tuneTemperatureByContext(int i)
{
  float lowtemp=temperature;
  float hightemp=temperature_alt;
  if(hightemp<=0) return lowtemp;
  if(!(i&15)) return hightemp;
  if(isBreaker(tokenstrings[context[i]])) { /*fprintf(stderr,"<b>");*/ return hightemp; }
  int j=i-3;
  while(j>3)
  {
    if(context[i]==context[j])
    {
      if(context[i-1]==context[j-1] &&
         context[i-2]==context[j-2] &&
         context[i-3]==context[j-3]) { /*fprintf(stderr,"<L>");*/ return hightemp; }
    }
    j--;
  }
  return lowtemp;
}

void generate(int start,int genstart_,int genend_)
{
  currslot=start;
  genstart=genstart_;
  genend=genend_;
  char reloading=0;
  float prevwv[WVSIZE];
  while(currslot<genend)
  {
    int tok;
    runModel(currwv,currslot);
    tok=context[currslot];
    if(tok>=0 && currslot<genstart)
    {
      if(!reloading)
      {
        {
        int i;
        float prob=0;
        // alt: laske tok:in etäisyys currwv:stä
        matchToTokens(prevwv,matchlist,nummatches,1.0);
        for(i=0;i<nummatches;i++)
          if(matchlist[i].tok==tok) { prob=matchlist[i].prob; break; }
        if(prob==0)   printf("\033[0;31m"); else
        if(prob<0.01) printf("\033[0;35m"); else
        if(prob>0.95 || i==0) printf("\033[33;1m"); else
        if(prob>0.90 || (prob>0.25 && i<=2)) printf("\033[0;1m"); else
                      printf("\033[0m");
        memcpy(prevwv,currwv,WVSIZE*sizeof(float));
        //printf("\033[%dm%s",36+(currslot&1),tokenstrings[tok]);
        }
        printf("%s",tokenstrings[tok]);
        printf("\033[0m");
        fflush(stdout);
      }
      else
        if(verbose>=1) fprintf(stderr,"%s",tokenstrings[tok]);
    }
    currslot++;
    if(currslot==genstart) // !!! TESTING
    {
      int i;
//      for(i=1;i<6;i++) glitchkv(i);
      glitchkv(5);
//      glitchkv(1);
//      glitchkv(35);
      srand(36879);
    }
    if(currslot>=genstart)
    {
      int match;
      reloading=0;
      float tunedTemp=tuneTemperatureByContext(currslot-1);
      matchToTokens(currwv,matchlist,nummatches,tunedTemp);
      match=pickmatch(matchlist,nummatches,minp);
      tok=matchlist[match].tok;
      tok=replacetoken(tok);
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
    if(breaks_called)
    {
      breaks_called=0;
      ttyui();
    }
  }
}

void init(char*modelpath)
{
  int rc;
  context=malloc(CTXSIZE*sizeof(int));
  currwv=malloc(WVSIZE*sizeof(float));
  matchlist=malloc(MAXNUMMATCHES*sizeof(match_t));

  fprintf(stderr,"load model...\n");
  loadmodel(modelpath);
#ifdef QUANTIZE
  //quantize();
#endif

  for(int i=0;i<CTXSIZE;i++) context[i]=emptytoken;

  tokenflags=malloc((numtokens+MAXUSERTOKENS)*sizeof(char));
  memset(tokenflags,0,(numtokens+MAXUSERTOKENS)*sizeof(char));
  nummodeltokens=numtokens;
}

#if (0)
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
#endif

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

#ifdef HAVE_SDL
int tokenize_image(char*fn)
{ 
  int ctxdim=sqrt(CTXSIZE);
  int i,j,w,h;
  SDL_Surface*img=IMG_Load(fn);
  if(!img) return 0;
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
#endif

#ifdef ENABLE_TTYUI
int handlesignal(int s)
{
  breaks_called++;
  if(breaks_called>=3)
  {
    breaks_called=0;
    ttyui();
  }
//  signal(SIGINT,SIG_DFL);
//  signal(SIGINT,handlesignal);
  return 0;
}
#endif

void configcmd(char*cmd,char*param)
{
  char changed=0;
  if(!strncmp(cmd,"model",5))
  {
    modelpath=strndup(param,strchr(param,'\n')-param);
    return;
  }
  for(int i=0;i<NUMVARS;i++)
    if(!strncmp(cmd,settingvars[i].name,param-1-cmd))
    {
      if(settingvars[i].type)
        *((int*)settingvars[i].ptr)=strtol(param,NULL,10);
      else
        *((float*)settingvars[i].ptr)=strtof(param,NULL);
      changed=1;
    }
  if(!changed)
  {
    fprintf(stderr,"couldn't parse configline `%s'\n",cmd);
    exit(1);
  }
}

void flagTokenForReplace(int t1,int t2,int flag)
{
  if(!tokenrepls) tokenrepls=malloc(numtokens*sizeof(token_t));
  tokenflags[t1]=flag;
  tokenrepls[t1]=t2;
//  fprintf(stderr,"flagged %d->%d (%d)\n",t1,t2,flag);
}

void flagTokenSetForReplace(char*t1s,char*t2s,int flag)
{
  int t1t=tokenize(t1s);
  int t2t=tokenize(t2s);
  if(!strcmp(tokenstrings[t1t],t1s) &&
     !strcmp(tokenstrings[t2t],t2s))
  {
    flagTokenForReplace(t1t,t2t,flag);
  } else
  {
//    fprintf(stderr,"no tokens for `%s',`%s'\n",t1s,t2s);
  }
  
  if(*t1s>='a' && *t1s<='z')
  {
    char*t1b=strdup(t1s);
    char*t2b=strdup(t2s);
    t1b[0]+='A'-'a';
    if(*t2s>='a' && *t2s<='z') t2b[0]+='A'-'a';
    flagTokenSetForReplace(t1b,t2b,flag);
    free(t1b);
    free(t2b);
  }
  if(*t1s==' ' && *t2s==' ') flagTokenSetForReplace(t1s+1,t2s+1,flag);
}

void readconfig(char*s,int dotokens)
{
  while(*s)
  {
    if(*s!='#')
    {
      char*linestart;
      while(*s==' ' || *s=='\t') s++;
      linestart=s;
      if(*linestart=='*' && (linestart[1]>='0' && linestart[1]<='9'))
      {
        // massflag tokens by substring
        if(dotokens)
        {
          int flag=linestart[1]-'0'-1;
          int i;
          int linelgt=strchr(linestart,'\n')-linestart;
          char*lineend=strchr(s,'\n');
          if(!lineend) lineend=s+strlen(s);
          lineend--;
          char*needle=strndup(linestart+2,lineend-linestart-2+1);
          fprintf(stderr,"massflagging *%s* to %d...\n",needle,flag);
          for(i=0;i<numtokens;i++)
          {
            if(strstr(tokenstrings[i],needle))
            {
              //fprintf(stderr,"token %d (%s) flagged to %d\n",i,tokenstrings[i],flag);
              tokenflags[i]=flag;
            }
          }
          free(needle);
        }
      } else
      if(*linestart>='0' && *linestart<='9')
      {
        // flag single token
        if(dotokens)
        {
          int flag=*linestart-'0'-1;
          int token=tokenize(linestart+1);
          if(token>=0) tokenflags[token]=flag;
        }
      } else
      if(*linestart=='r' || *linestart=='R' || *linestart=='x' ||
         *linestart=='X')
      {
        if(dotokens)
        {
          char*ss;
          int flag=isupper(*linestart)?5:4;
          ss=linestart+1;
          int t1=tokenize(ss);
          ss+=strlen(tokenstrings[t1]);
          int t2=tokenize(ss);
          ss+=strlen(tokenstrings[t2]);
          if(*ss!='\n' && *ss!='\0')
          {
            fprintf(stderr,"error: need exactly two tokens for r/R/x/X!\n"
              "(%d %d %s ends with %d)\n",t1,t2,linestart,*ss);
          } else
          {
            flagTokenSetForReplace(tokenstrings[t1],tokenstrings[t2],flag);
            if(*s=='x' || *s=='X')
              flagTokenSetForReplace(tokenstrings[t2],tokenstrings[t1],flag);
          }
        }
      } else
      {
        // other commands (set parameters)
        while(*s && *s!='=' && *s!=' ' && *s!='\n') s++;
        if(*s==' ' || *s=='=')
          configcmd(linestart,s+1);
      }
    }
    while(*s && *s!='\n') s++;
    if(*s) s++;
  }
}

int (analyzetokens_cmp)(const void*a,const void*b)
{
  return (((int*)a)[0] > ((int*)b)[0]) ? -1:1;
}

void analyzetokens()
{
  int i,j;
  int tab[numtokens*2];
  for(j=0;j<768;j++)
  {
    int howmany=0;
    for(i=0;i<numtokens;i++)
    {
      if(tokenstrings[i][0]==' ')
      {
        tab[howmany*2+0]=wte[i*768+j];
        tab[howmany*2+1]=i;
        howmany++;
      }
    }
    qsort(tab,howmany,sizeof(int)*2,analyzetokens_cmp);
    printf("component %d:",j);
    for(i=0;i<5;i++) printf("%s[%d]",tokenstrings[tab[i*2+1]],
      tab[i*2]);
    printf(" (not:");
    for(i=0;i<5;i++) printf("%s[%d]",tokenstrings[tab[(howmany-1-i)*2+1]],
      tab[(howmany-1-i)*2]);
    printf(")\n");
    //fprintf(stderr,"component %d: '%s' (%d), not '%s' (%d)\n",
    //  j, tokenstrings[maxat],max, tokenstrings[minat],min);
  }
}

int main(int argc,char**argv)
{
  // settings & defaults
  temperature=1.0;
  temperature_alt=0.0;
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
  char*packedfiletosave=NULL;
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
            "-a 1.0      set alternative temperature used at sentence boundaries etc\n"
            "-T 4        set number of threads\n"
            "-s 123456   set random number seed (0 = use timer)\n"
            "-c conf.txt read config commands from file\n"
            "-u          start ui even with -p and -f\n"
            "-b          run benchmark\n"
            "-v          verbose/debug output\n"
            "-Z file.vzg write packed model to disk\n"
            ,argv[0]);
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
          if(*s=='a')
            temperature_alt=atof(argv[++i]);
          if(*s=='T')
            numthreads=atoi(argv[++i]);
          if(*s=='s')
            seed=atoi(argv[++i]);
          if(*s=='c')
            configfile=argv[++i];
          if(*s=='l')
            lengthtogen=atoi(argv[++i]);
          if(*s=='Z')
            packedfiletosave=argv[++i];
        }
        if(*s=='H')
          wannastartui=66;
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
          minp=0;
          prompt=" Suddenly, a magical floppy disk";
        }
      }
    } else modelpath=argv[i];
  }
  
  // todo read configfile also here (before tokenstuff)
  
  char*configs=NULL;
  if(configfile)
  {
    configs=readtextfile(configfile,NULL);
    if(!configs)
    {
      fprintf(stderr,"couldn't find configfile %s!\n",configfile);
      exit(1);
    }
  }
  if(configs) readconfig(configs,0);

  init(modelpath);
  if(packedfiletosave)
  {
    fprintf(stderr,"saving packed model to file %s...\n",packedfiletosave);
    savepackedmodel(packedfiletosave);
    exit(0);
  }
  if(!seed)
  {
    seed=time(NULL);
    fprintf(stderr,"seed from time(): %d\n",seed);
  }
  srand(seed);
  
//  maketextart();
//  fprintf(stderr,"\n\n");
//  exit(0);

//  analyzetokens();
//  exit(0);

  if(wannastartui==66)
  {
    http_server();
    return 0;
  }

  if(configs)
  {
    readconfig(configs,1);
  }
  //targetwv=buildcustomwv(configfile);
  //targetwv=NULL;

  if(promptfile && !palette)
    prompt=readtextfile(promptfile,NULL);
    
#ifdef HAVE_SDL
#ifdef ENABLE_SDLUI
  if(wannastartui || !prompt) ui_init();
#endif
#endif  
  //iqtest();

  context[0]=emptytoken;
  int promptlgt=0;
  if(prompt) promptlgt=tokenize_to_context(prompt,0);
#ifdef HAVE_SDL
  if(palette && promptfile) promptlgt=tokenize_image(promptfile);
#endif
  fprintf(stderr,"Prompt length: %d tokens\n",promptlgt);

#ifdef HAVE_SDL
#ifdef ENABLE_SDLUI
  if(wannastartui || !prompt)
  {
    ui_run();
    return 0;
  }
#endif
#endif

#ifdef ENABLE_TTYUI
  signal(SIGINT,handlesignal);
#endif
  if(lengthtogen<=0) lengthtogen=CTXSIZE+1;
  generate(0,promptlgt,lengthtogen);

  return 0;
}

/*

configfile:
param value

*/