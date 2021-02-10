#include "common.h"
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define COLOR0 "\033[0m"
#define COLOR1 "\033[1m"

int getch()
{
  struct termios old,curr;
  int c;
  tcgetattr(STDIN_FILENO,&old);
  memcpy(&curr,&old,sizeof(struct termios));
  curr.c_lflag&=~(ECHO|ICANON);
  tcsetattr(STDIN_FILENO,TCSANOW,&curr);
  c = getchar();
  tcsetattr(STDIN_FILENO,TCSANOW,&old);
  return c;
}

int hascontrolchars(char*s)
{
  while(*s)
  {
    if(*s<32) return 1;
    s++;
  }
  return 0;
}

void ttyui_printtail(int endslot)
{
  int i=endslot+1;
  while(i>endslot-8)
  {
    if(i==0) break;
    if(context[i-1]<0) break;
    if(hascontrolchars(tokenstrings[context[i-1]])) break;
    i--;
  }
  for(;i<=endslot;i++)
  {
    fprintf(stderr,"%s",tokenstrings[context[i]]);
  }
}

void ttyui_edit()
{
  int redraw=1;
  char addition[80];
  *addition='\0';
  fputc('\n',stderr);
  for(;;)
  {
    int c;
    if(redraw)
    {
      fprintf(stderr,COLOR1 "\r\033[K@%d:",currslot);
      ttyui_printtail(currslot);
      fprintf(stderr,COLOR0 "%s",addition);
      redraw=0;
    }
    c=getch();
    if(c=='\n') break;
    if(c=='\b' || c==127)
    {
      if(*addition)
      {
        currslot=tokenize_to_context(addition,currslot+1)-1;
        *addition='\0';
      }
      currslot--;
      redraw=1;
    }
    if(c=='\t')
    {
      currslot=tokenize_to_context(addition,currslot+1)-1;
    }
    if(c>=32 && c!=127)
    {
      int i=strlen(addition);
      if(c==32 || i>78)
      {
        currslot=tokenize_to_context(addition,currslot+1)-1;
        i=0;
      }
      addition[i]=c;
      addition[i+1]='\0';
      putchar(c);
    }
  }
  if(*addition) currslot=tokenize_to_context(addition,currslot+1)-1;
}

void ttyui_matchlist()
{
  int i;
  fputc('\n',stderr);
  for(i=0;i<nummatches;i++)
  {
    if(matchlist[i].prob<minp) break;
    fprintf(stderr,"%3d." COLOR1 "%s" COLOR0 "(%3.1f%%)",i,
      tokenstrings[(int)matchlist[i].tok], matchlist[i].prob*100);
  }
}

#define NUMVARS 5

void ttyui_settings()
{
  char buf[20];
  int i;
  fputc('\n',stderr);
  for(i=0;i<NUMVARS;i++)
  {
    fprintf(stderr,"%d.%s=",i,settingvars[i].name);
    if(settingvars[i].type)
      fprintf(stderr,"%d\n",*((int*)settingvars[i].ptr));
    else
      fprintf(stderr,"%f\n",*((float*)settingvars[i].ptr));
  }
  fprintf(stderr,COLOR1 "var # to change:" COLOR0);
  i=getch()-'0';
  if(i<0 || i>=NUMVARS) return;
  fprintf(stderr,COLOR1 "\r\033[K%s=" COLOR0, settingvars[i].name);
  fgets(buf,19,stdin);
  if(settingvars[i].type)
    *((int*)settingvars[i].ptr)=strtol(buf,NULL,10);
  else
    *((float*)settingvars[i].ptr)=strtof(buf,NULL);
}

void ttyui()
{
  int menu=0;
  for(;;)
  {
    int c;
    fprintf(stderr,COLOR1 "\nvzgpt (h=help):" COLOR0);
    c=getch();
    if(c=='\r' || c=='\n')
    {
      fputc('\n',stderr);
      // set currslot to first unprocessed slot
      genstart=currslot;
      return;
    }
    if(c=='e') ttyui_edit();
    if(c=='m') ttyui_matchlist();
    if(c=='s') ttyui_settings();
    if(c=='d')
    {
      int i=0;
      fputc('\n',stderr);
      for(i=0;i<=currslot;i++)
        if(context[i]>0) printf("%s",tokenstrings[context[i]]);
      fflush(stdout);
    }
    if(c=='.')
    {
      int match,tok;
      fputc('\n',stderr);
      runModel(currwv,currslot);
      currslot++;
      matchToTokens(currwv,matchlist,nummatches,temperature);
      match=pickmatch(matchlist,nummatches,minp);
      tok=matchlist[match].tok;
      context[currslot]=tok;
      if(tok>=0) printf("%s",tokenstrings[tok]);
      fflush(stdout);
    }
    if(c=='h')
    {
      fprintf(stderr,"\n"
        "<RET>: continue running\n"
        ".: run one step\n"
        "d: show (dump) contextbuffer\n"
        "e: edit contextbuffer\n"
        "m: show matches\n"
        "s: settings\n"
        "h: help\n"
        "q: quit"
        /* TODO: f: set logfile */
      );
      continue;
    }
    if(c=='q' || c==3)
    {
      fputc('\n',stderr);
      exit(0);
    }
  }
  getch();
}
