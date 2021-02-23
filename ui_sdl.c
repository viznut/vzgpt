#include "common.h"

int fixedseed=1612634278;

char*font; // alloc in init()
int fontsz;
char matchlist_visible;
int context_xscroll;
int matchlist_yscroll;
int cursor_slot;
int cursor_match;
char*context_tags;

char uirunning=0;
char smallchange=0;
char animating=0;
char wannaquit=0;

//float temp=1.0;
//float minp=0.0;
//int nummatches=40;
int autorun_delay=200;
int autorun_nextshot=0;

void ui_refresh();

int utf8inc(unsigned char*s)
{
  if(*s<=0x7f) return 1;
  if(!s[1]) return 1;
  if(*s<=0xdf) return 2;
  if(!s[2]) return 1;
  if(*s<=0xef) return 3;
  if(!s[3]) return 1;
  return 4;
}

int utf8get(unsigned char*s)
{
  if(*s<=0x7f) return *s;
  if(!s[1]) return *s;
  if(*s<=0xdf) return ((s[0]&0x1f)<<6)|(s[1]&0x3f);
  if(!s[2]) return *s;  
  if(*s<=0xef) return ((s[0]&0x0f)<<12)|((s[1]&0x3f)<<6)|(s[2]&0x3f);
  if(!s[3]) return *s;
  return ((s[0]&0x07)<<18)|((s[1]&0x3f)<<12)|
         ((s[2]&0x3f)<<6) |(s[3]&0x3f);
}

int utf8strlen(char*s)
{
  int i;
  int ctr=0;
  char*s1=s+strlen(s);
  while(s<s1)
  {
    s+=utf8inc(s);
    ctr++;
  }
  return ctr;
}

int gettokencolor(int t,float m)
{
  int r,g,b;
  if(t<0 || t>=numtokens) return 0xff00ff;
  r=palette[t*3+0]*m+(m-1.0)*100; if(r>255) r=255;
  g=palette[t*3+1]*m+(m-1.0)*100; if(g>255) g=255;
  b=palette[t*3+2]*m+(m-1.0)*100; if(b>255) b=255;
  return (r<<16)|(g<<8)|b;
}

// // //

void renderchar(char*data,int x,int y,int fg,int bg)
{
  if(x<0 || y<0 || x>=scrw-8 || y>=scrh-16) return;
  int j,i;
  for(j=0;j<16;j++)
    for(i=0;i<8;i++)
      ((int*)(fb->pixels))[(y+j)*scrw+(x+i)]=data[j]&(128>>i)?fg:bg;
}

void rendertext(char*s,int x,int y,int fg,int bg,int flags)
{
  int x0=x;
  while(*s)
  {
    int c=utf8get(s);
    int cc=c;
    if(c>=fontsz) cc=0x378;
//    if(c=='\n') cc=0x21b5; //(flags&2)?0x21b5:' ';
    renderchar(font+cc*16,x,y,fg,bg);    
    if(c=='\n') { x=x0;y+=16; }
    x+=8;
    s+=utf8inc(s);
  }
  if(flags&1)
  {
    renderchar(font+32*16,x,y,0x00000,0x9999ff);
    x+=8;
    renderchar(font+32*16,x,y,0x00000,bg);
  }
}

void fillrect(int x0,int y0,int x1,int y1,int c)
{
  SDL_Rect r={x0,y0,x1-x0+1,y1-y0+1};
  SDL_FillRect(fb,&r,c);
}

void outlinerect(int x0,int y0,int x1,int y1,int thickness,int c0,int c1)
{
  thickness--;
  fillrect(x0,y0,x0+thickness,y1,c0);
  fillrect(x0,y0,x1,y0+thickness,c0);
  fillrect(x1-thickness,y0,x1,y1,c1);
  fillrect(x0,y1-thickness,x1,y1,c1);
}

void drawwindowframe(int x0,int y0,int x1,int y1)
{
  outlinerect(x0,y0,x1,y1,2,0x999999,0x111111);
  fillrect(x0+2,y0+2,x1-2,y1-2,0x333333);
}

void scroll(int*var,int target)
{
  if(*var==target) return;
  smallchange=0;
  *var=target;
/*  
  animating=1;
  int diff=target-*var;
  if(diff/16) diff/=16; else diff=diff<0?-1:1;
  *var+=diff;
*/
}

char showlayernodes=1;

int getattentioncoloring(int i)
{
  int j;
  float r=0,g=0,b=0;
  for(j=0;j<12*12;j++)
  {
    float a=attentions[i*NUMHEADS*NUMLAYERS+j];
    b+=(NUMHEADS*NUMLAYERS-j)*a;
    g+=abs((NUMHEADS*NUMLAYERS)/2-j)*a*2;
    r+=j*a*0;
  }
  float max=r>g?r:g;
  max=b>max?b:max;
  max-=255;
  if(max>0)
  {
    r-=max;
    g-=max;
    b-=max;
  }
  if(r<0)r=0;
  if(g<0)g=0;
  if(b<0)b=0;
  r+=0x99;
  g+=0x99;
  b+=0x99;
  if(r>255)r=255;
  if(g>255)g=255;
  if(b>255)b=255;
  return floor(r)*65536+floor(g)*256+floor(b);
}

int firstslotofparagraphs=0;

void renderpixelmap(int x0,int y0)
{
  int pxdim=sqrt(CTXSIZE);
  int zoom=(scrh-y0)/pxdim;
  int j,i;
  for(j=0;j<pxdim;j++)
  for(i=0;i<pxdim;i++)
  {
    int tc=gettokencolor(context[j*pxdim+i],1.0);
    int oc=(j*pxdim+i==cursor_slot?0xffffff:0x000000);
    fillrect(x0+i*zoom+1,y0+j*zoom+1,x0+(i+1)*zoom,y0+(j+1)*zoom,tc);
    outlinerect(x0+i*zoom,y0+j*zoom,x0+(i+1)*zoom+1,y0+(j+1)*zoom+1,1,oc,oc);
  }
}

void renderparagraphs(int x0,int y0)
{
  int x=x0,y=y0;
  int firstnewlineat=-1;
  int i;
  if(cursor_slot<firstslotofparagraphs) firstslotofparagraphs=0;
  for(i=firstslotofparagraphs;i<CTXSIZE;i++)
  {
    int tok,fg=getattentioncoloring(i),bg=0x000000;
    if(y>=scrh-16) break;
    tok=context[i];
    char*s="<NONE>";
    if(tok>=0 && tok<numtokens) s=tokenstrings[tok];
    int wdt=utf8strlen(s)*8;
    if(x+wdt>=scrw || tok==emptytoken)
    {
      y+=16;
      x=x0;
      if(firstnewlineat<0) firstnewlineat=i;
    }
    if(i==cursor_slot) { bg=0x666666; fg=0xffffff; }
    while(*s)
    {
      int c=utf8get(s);
      if(c=='\n') {
        y+=16;
        x=x0;
        if(firstnewlineat<0) firstnewlineat=i+1;
      } else
      {
        if(c>=fontsz) c=0x378;
        renderchar(font+c*16,x,y,fg,bg);
        x+=8;
      }
      s+=utf8inc(s);
    }
//    if(i==cursor_slot) renderchar(font+' '*16,x,y,0x000000,0xffffff);
  }
  if(i<=cursor_slot && firstnewlineat>0) firstslotofparagraphs=firstnewlineat;
}

void rendercontext(int x0,int y0,int zoom)
{
  int i,y;
  int y1=matchlist_visible?scrw-256:scrw;
  
  int x=x0-context_xscroll;
  if(x+cursor_slot*zoom<0)
     scroll(&context_xscroll,cursor_slot*zoom);
  if(x+cursor_slot*zoom>y1-zoom)
     scroll(&context_xscroll,(cursor_slot-y1/zoom+1)*zoom);
  x=x0-context_xscroll;

  for(i=0;i<CTXSIZE;i++,x+=zoom)
  {
    //int x=i*zoom+x0;
    y=y0;
  
    if(x<0) continue;
    if(x>=y1) break;

    // returned vector
    if(outputcache && !smallchange && i>0)
    {
      renderwordvec(outputcache+(i-1)*WVSIZE,x+16,y,32);
    }
    y+=32;

    // token tagging
    {
    char t[20];
    int bg=0x000000,fg=0x999999;
    if(!context_tags[i])
      sprintf(t,"%d",i);
    else
    {
      char*d=t;
      if(context_tags[i]>0) { *d++='+'; fg=0x00ff00; }
      else { *d++='-'; fg=0xff0000; }
      int a=abs(context_tags[i]);
      if(a>1) d+=sprintf(d,"%d",abs(context_tags[i]));
      *d++='\0';
    }
    // TODO if tagged then tagmark
    rendertext(t, x+zoom/2-4*strlen(t), y, fg,bg,0);
    }
    y+=16;

    // token string
    {
    char*t=tokenstrings[context[i]];
    int bg=0x333333,fg=0xffffff;
    if(palette)
    {
      bg=gettokencolor(context[i],1.0);
      fg=gettokencolor(context[i],1.2);
    }
    if(context[i]==emptytoken || !t)
    {
      fg=0xffffff;
      bg=0x000000;
      t=" ";
    }
    if(i==cursor_slot) { bg=fg; fg=0; }
    rendertext(t, x+zoom/2-4*utf8strlen(t), y, fg,bg,2);
    }
    y+=16;

    // word vector for token
    if(!smallchange)
      renderwordvec(getwv(context[i]),x,y,zoom);
    y+=64;

    // layernodes
    if(showlayernodes && !smallchange)
    {
      int j;
      int nodehgt=(scrh-y)/NUMLAYERS;
      if(nodehgt>24) nodehgt=24;
      if(nodehgt>16) y+=(nodehgt-16)/2;
      for(j=0;j<NUMLAYERS;j++)
      {
        renderlayernode(layers[j].k+WVSIZE*i,
          attentions+NUMHEADS*NUMLAYERS*i+NUMHEADS*j,NUMHEADS,x,y);
        y+=nodehgt;
      }
    }
  }
  
  if(!showlayernodes && !smallchange)
  {
    if(!palette) renderparagraphs(0,y0+32+16+16+64+8);
           else renderpixelmap(0,y0+32+16+16+64+8);
  }
}

void matchlist_render(int x0,int y0,int w)
{
  int i;

  int y=y0+128+4-matchlist_yscroll;
  int cm=cursor_match;
  if(cm<0) cm=0;
  if(y+cm*32<y0+128+4)
    scroll(&matchlist_yscroll,cm*32);
  if(y+cm*32>scrh-32)
    scroll(&matchlist_yscroll,(cm-(scrh-128-4)/32+1)*32);
  y=y0+128+4-matchlist_yscroll;

  if(!smallchange)
  {
    drawwindowframe(x0,y0,scrw,scrh);
    renderwordvec(currwv,x0+64,y0+2,128); // 32x32
  }
  fillrect(x0+62,y0+2,x0+63,y0+130,cursor_match<0?0xffffff:0x333333);

  y0+=128+4;
  for(i=0;i<nummatches;i++,y+=32)
  {
    if(y<y0) continue;
    if(y>=scrh) return;

    char bf[80];
    int t=matchlist[i].tok;
    if(!smallchange) renderwordvec(getwv(t),x0+6,y,32);
    sprintf(bf,"%3.f%%",matchlist[i].prob*100.0);
    rendertext(bf,x0+40,y,0xfffff,0x333333,0);
    {int fg=0xffffff,bg=0x333333;
     if(palette) { bg=gettokencolor(t,1.0); fg=gettokencolor(t,1.2); }
     if(i==cursor_match) { bg=fg; fg=0x000000; }
     rendertext(tokenstrings[t],x0+40+6*8,y,fg,bg,0);
    }
    sprintf(bf,"#%d",t);
    rendertext(bf,x0+40+6*8,y+16,0x999999,0x333333,0);
  }
}

char userinput[1024];
void userinput_addchar(int c)
{ 
  int s=strlen(userinput);
  if(c<=0x7f) { userinput[s]=c; s++; } else
  if(c<=0x7ff) { userinput[s]=0xc0|(c>>6); userinput[s+1]=0x80|(c&63); s+=2; }
  userinput[s]='\0';
}

void userinput_backspace()
{
  int s=strlen(userinput)-1;
  while(s>0 && userinput[s]>=0x80 && userinput[s]<=0xbf) s--;
  if(s<0) s=0;
  userinput[s]='\0';
}

#define CONFIRM_QUIT 1
#define CONFIRM_CLEAR 2
#define CONFIRM_PURGE 3
char confirmer_visible=0;
char*confirmer_texts[]={
  "Quit?",
  "Clear the rest of the buffer?",
  "Purge buffer before this point?"
};

void confirmer_render()
{
  char*t=confirmer_texts[confirmer_visible-1];
  int wdt=strlen(t)*8+16;
  drawwindowframe(scrw/2-wdt/2,scrh/2-32,scrw/2+wdt/2,scrh/2+32);
  rendertext(t,scrw/2-wdt/2+8,scrh/2-32+8,0xffffff,0x333333,0);
  rendertext("Y/N",scrw/2-24/2,scrh/2+8,0x999999,0x333333,0);
}

#define USERINPUT_TOKENIZE 1
#define USERINPUT_NAMETOKEN 2
char userinput_visible=0;
void userinput_clear()
{
  userinput[0]='\0';
  userinput_visible=0;
}

void userinput_render()
{
  char*title="Tokenize";
  if(userinput_visible==USERINPUT_NAMETOKEN) title="Name this token";
  drawwindowframe(0,8,scrw,8+16+16+8+4);
  rendertext(title,0,8+2,0x999999,0x333333,0);
  rendertext(userinput,0,8+16+2,0xffffff,0x333333,3);
}

void statusbar_render()
{
  char s[40];
  int part0lgt;
  sprintf(s,"temp=%1.2f  ",temperature);
  rendertext(s,0,scrh-17,0x999999,0x000000,0);
  part0lgt=strlen(s);
  if(autorun_nextshot)
  {
    sprintf(s,"delay=%d",autorun_delay);
    rendertext(s,part0lgt*8,scrh-17,0xcccccc,0x000000,0);
  }
}

void renderscreen()
{
  if(!smallchange) SDL_FillRect(fb,NULL,0);
  rendercontext(0,24,64);
  
  if(matchlist_visible) matchlist_render(scrw-256,0,256);
  if(userinput_visible) userinput_render();
  if(confirmer_visible) confirmer_render();
  statusbar_render();
  smallchange=0;
}

void update_matchlist()
{
  matchToTokens(currwv,matchlist,nummatches,temperature);
}

void copy_to_currwv()
{
  int token=context[cursor_slot];
  float*src=getwv(token); //wte+WVSIZE*token;
  memcpy(currwv,src,WVSIZE*sizeof(float));
  update_matchlist();
  matchlist_visible=1;
  cursor_match=0;
}

void copy_to_currwv_mix()
{
  int token0=context[cursor_slot];
  int token1=context[cursor_slot+1];
  int token2=context[cursor_slot+2];
  float*src0=wte+WVSIZE*token0;
  float*src1=wte+WVSIZE*token1;
  float*src2=wte+WVSIZE*token2;
  int i;
//  float c=2*(((rand()&65535)/32768.0)-1.0);
  for(i=0;i<WVSIZE;i++) currwv[i]=src0[i]-src1[i]+src2[i];//*c+src1[i]*(1.0-c);
  update_matchlist();
  cursor_match=-1;
  matchlist_visible=1;
}

void paste_currwv()
{
}

void dump_context()
{
  int i=0;
  fprintf(stderr,"\n\n");
  for(i=0;i<=cursor_slot;i++)
  {
    fprintf(stderr,tokenstrings[context[i]]);
  }
}

int lasttokenslot()
{
  int i=CTXSIZE-1;
  for(;i;i--)
    if(context[i]!=emptytoken) break;
  return i;
}

void mix_tagged()
{
  int n=0,i,j;
  float sum[768];
  for(i=0;i<768;i++) sum[i]=0;
  for(i=0;i<CTXSIZE;i++)
  if(context_tags[i])
  {
    int t=context[i];
    float mul=context_tags[i];
    n+=context_tags[i];
    for(j=0;j<768;j++) sum[j]+=mul*wte[t*768+j];
  }
  n=abs(n);
  if(!n) n=1;
  for(j=0;j<768;j++) currwv[j]=sum[j]/(float)n;
  update_matchlist();
  matchlist_visible=1;
  cursor_match=-1;
}

void randomize_wv()
{
  float min[768],max[768];
  int i,j;
  for(i=0;i<768;i++) min[i]=max[i]=0;
  int howmany=0;
  for(i=0;i<CTXSIZE;i++)
  if(context_tags[i])
  {
    int t=context[i];
    for(j=0;j<768;j++)
    {
      float a=wte[t*768+j];
      if(a<min[j]) min[j]=a;
      if(a>max[j]) max[j]=a;
    }
    howmany++;
  }
  if(!howmany)
  {
    for(i=0;i<768;i++)
      currwv[i]=((rand()&65535)-(rand()&65535))/65536.0;
  } else
  {
    for(i=0;i<768;i++)
      currwv[i]=min[i]+((max[i]-min[i])*(rand()&65535))/65536.0;
  }
  update_matchlist();
  matchlist_visible=1;
  cursor_match=-1;
}

// //

void purgeoldcontext(int p)
{
  int i,j;

  for(i=0;i<CTXSIZE;i++)
    context[i]=i+p<CTXSIZE?context[i+p]:emptytoken;

  if(context_tags)
  for(i=0;i<CTXSIZE;i++)
    context_tags[i]=i+p<CTXSIZE?context_tags[i+p]:0;

  // TODO copy k&v, clear rest of k&v
}

void autorun_shot()
{
  runModel(currwv,cursor_slot);
  cursor_slot++;
  if(cursor_slot<CTXSIZE && context[cursor_slot]==emptytoken)
  {
    update_matchlist();
    cursor_match=pickmatch(matchlist,nummatches,minp);
    context[cursor_slot]=matchlist[cursor_match].tok;
  }
  if(cursor_slot>=CTXSIZE-1)
  {
    autorun_nextshot=0;
    cursor_slot=CTXSIZE-1;
  }
  ui_refresh();
}

void clearcontext(int i)
{
  for(;i<CTXSIZE;i++)
  {
    context[i]=emptytoken;
    context_tags[i]=0;
  }
}

void ui_init()
{
  if(verbose>=1) fprintf(stderr,"load font...\n");
  font=readfile("font.dat",&fontsz,NULL);
  fontsz/=16;

  attentions=malloc(CTXSIZE*NUMLAYERS*NUMHEADS*sizeof(float));
  outputcache=malloc(CTXSIZE*WVSIZE*sizeof(float));
  context_tags=malloc(CTXSIZE*sizeof(char));
  userwte=malloc(WVSIZE*MAXUSERTOKENS*sizeof(float));
  memset(outputcache,0,CTXSIZE*WVSIZE*sizeof(float));
  memset(attentions,0,CTXSIZE*NUMLAYERS*NUMHEADS*sizeof(float));
  //fprintf(stderr,"emptytoken=%d\n",emptytoken);

  if(verbose>=1) fprintf(stderr,"start ui\n");
  SDL_Init(SDL_INIT_VIDEO);
  scrw=DEFAULT_SCRW;
  scrh=DEFAULT_SCRH;
  fb=SDL_SetVideoMode(scrw,scrh,32,SDL_RESIZABLE);
  SDL_EnableUNICODE(1);
  SDL_EnableKeyRepeat(500,3);

  clearcontext(0);
}

void handlekey(SDL_keysym*ks)
{
  int k=ks->sym;
  int u=ks->unicode;
  int m=ks->mod;

  if(k==SDLK_LEFT && cursor_slot>0) { cursor_slot--; smallchange=1; }
  if(k==SDLK_RIGHT && cursor_slot<1024) { cursor_slot++; smallchange=1; }
  
  if(confirmer_visible)
  {
    if(u=='y')
    { 
      int c=confirmer_visible;
      if(c==CONFIRM_QUIT) wannaquit=1;
      if(c==CONFIRM_CLEAR) clearcontext(cursor_slot);
      if(c==CONFIRM_PURGE) purgeoldcontext(cursor_slot);
      confirmer_visible=0;
    }
    if(k==SDLK_ESCAPE || u=='n') confirmer_visible=0;
    return;
  }
  
  if(userinput_visible)
  {
    if(k==SDLK_BACKSPACE) { userinput_backspace(); smallchange=1; }
    else
    if(k==SDLK_ESCAPE)
    {
      userinput_clear();
    }
    else
    if(k==SDLK_RETURN)
    {
      if(userinput_visible==USERINPUT_TOKENIZE)
        tokenize_to_context(userinput,cursor_slot);
      else
        nametoken(context[cursor_slot],userinput);
      userinput_clear();
      return;
    }
    else
    {
      if((u>=32 && u<=126) || u>=160)
      {
        userinput_addchar(u);
        smallchange=1;
        return;
      }
    }
  } else
  {
    if(u==' ')
    {
      userinput_addchar(u);
      userinput_visible=1;
      return;
    }
  }
  
  if(autorun_nextshot)
  {
    if(k==SDLK_ESCAPE)
    {
      autorun_nextshot=0;
      return;
    }
    if(u=='+')
    {
      autorun_delay+=100;
      return;
    }
    if(u=='-')
    {
      autorun_delay-=100;
      return;
    }
  }

  if(matchlist_visible)
  {
    if(k==SDLK_UP) { cursor_match--; smallchange=1; }
    if(k==SDLK_DOWN) { cursor_match++; smallchange=1; }
    if(k==SDLK_PAGEUP) { cursor_match-=(scrh-128-4)/32; smallchange=1; }
    if(k==SDLK_PAGEDOWN) { cursor_match+=(scrh-128-4)/32; smallchange=1; }
    if(k==SDLK_HOME) { cursor_match=0; smallchange=1; }
    if(k==SDLK_END) { cursor_match=nummatches-1; smallchange=1; }
    if(cursor_match<-1) cursor_match=-1;
    if(cursor_match>nummatches-1) cursor_match=nummatches-1;
    
    if(k=='s')
    {
      int i;
      for(i=0;i<WVSIZE;i++)
      {
        float a=currwv[i];
        if(a<-0.9) a=-0.9;
        if(a> 0.9) a= 0.9;
        currwv[i]=a;
      }
    }
    if(k==SDLK_ESCAPE)
    {
      matchlist_visible=0;
      return;
    }
    if(k==SDLK_RETURN)
    {
      int tok;
      if(cursor_match>=0) tok=matchlist[cursor_match].tok;
        else tok=allocusertoken(currwv,NULL);
      context[cursor_slot]=tok;
      matchlist_visible=0;
      return;
    }
    if(u=='r')
    {
      cursor_match=pickmatch(matchlist,nummatches,0.0);
      return;
    }
  } else
  {
    if(k==SDLK_HOME) { cursor_slot=0; smallchange=1; }
    if(k==SDLK_END) { cursor_slot=lasttokenslot(); smallchange=1; }
  }
  
  if(k==SDLK_ESCAPE)
  {
    confirmer_visible=CONFIRM_QUIT;
  }
  if(k=='a')
  {
    if(!autorun_nextshot) autorun_nextshot=SDL_GetTicks();
       else autorun_nextshot=0;
    fprintf(stderr,"fixedseed=%d\n",fixedseed);
    srand(fixedseed);
  }
  if(u=='S')
  {
    fixedseed=time(NULL);
    fprintf(stderr,"new fixedseed=%d\n",fixedseed);
    srand(fixedseed);
  }
  if(k=='p')
  {
    confirmer_visible=CONFIRM_PURGE;
  }
  if(k=='x')
  {
    confirmer_visible=CONFIRM_CLEAR;
  }
  if(k=='c')
  {
    copy_to_currwv();
  }
  if(k=='d')
  {
    dump_context();
  }
  if(k=='+')
  {
    context_tags[cursor_slot]++; smallchange=1;
  }
  if(k=='-')
  {
    context_tags[cursor_slot]--; smallchange=1;
  }
  if(u=='<') temperature-=0.05;
  if(u=='>') temperature+=0.05;
  if(k=='m')
  {
    mix_tagged();
  }
  if(k=='t')
  {
    showlayernodes^=1;
  }
  if(k=='r')
  {
    randomize_wv();
  }
  if(k=='n')
  {
    userinput_visible=2;
    userinput_addchar(' ');
  }
  if(k==SDLK_RETURN)
  {
    int i;
    runModel(currwv,cursor_slot);
    update_matchlist();
    matchlist_visible=1;
    cursor_match=pickmatch(matchlist,nummatches,minp);
    if(verbose>=1) fprintf(stderr,"match picked: %d\n",cursor_match);
    cursor_slot+=1;
    // if > 1024 -> autopurge
  }
}

/*
int lastframeat;
void ui_dumpframes()
{
  int i;
  do
  {
  for(i=0;i<640*480;i++)
  {
    int c=((int*)fb->pixels)[i];
    putchar(c>>16);
    putchar(c>>8);
    putchar(c&255);
  }
  lastframeat+=1000/20;
  } while(lastframeat<SDL_GetTicks());
}
*/

void ui_refresh()
{
  if(!uirunning) return;
  renderscreen();
  SDL_Flip(fb);
}

void ui_run()
{
  char justwaited=0;
  uirunning=1;
  /*lastframeat=SDL_GetTicks();*/
  while(!wannaquit)
  {
    SDL_Event e;
    if(!justwaited && !autorun_nextshot)
    {
      ui_refresh();
      SDL_WaitEvent(&e);
      justwaited=1;
    } else {
      justwaited=SDL_PollEvent(&e);
      if(!justwaited) e.type=0;
    }
    if(autorun_nextshot && e.type==0 && autorun_nextshot<=SDL_GetTicks())
    {
      autorun_shot();
      if(autorun_nextshot) autorun_nextshot=SDL_GetTicks()+autorun_delay;
    }
    if(e.type==SDL_KEYDOWN)
    {
      handlekey(&e.key.keysym);
    }
    if(e.type==SDL_VIDEORESIZE)
    {
      scrw=e.resize.w;
      scrh=e.resize.h;
      fb=SDL_SetVideoMode(scrw,scrh,32,SDL_RESIZABLE);
    }
    if(e.type==SDL_QUIT) wannaquit=1;
  }
  SDL_Quit();
  uirunning=0;
}
