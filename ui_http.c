#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <err.h>
#include <errno.h>
#include <time.h>

#include "common.h"

#define URL "http://viznut.fi:8080/"

#define PORT 8080
#define MAXCONNECTIONS 16
#define BUFSZ 8192

#define MAXCTX 999
// 1022 is ok for a while

/*
int currslot=-1; // last generated
int genstart;
int genend=64;
*/
char generator_running=0;

int dummysignal(int s)
{
  fprintf(stderr,"ignore signal %d\n",s);
  return 0;
}

struct
{
  int fd;
  enum { FREE,HEADERS,CONTENT } mode;
  char buf[BUFSZ];
  int buflgt;
  int age; // if connections are full, drop the oldest
  char is_get;
  int arg;
  int contentlgt;
} connections[MAXCONNECTIONS];

char*indexhtml;

int txt2html(char*d,char*s)
{
  char*d0=d;
  while(*s)
  {
    int c=(unsigned char)*s;
    if(c=='\n')
      d+=sprintf(d,"<br>");
    else if(c<32) continue;
    else if(c=='<')
      d+=sprintf(d,"&lt;");
    else if(c=='&')
      d+=sprintf(d,"&amp;");
    else if(c=='>')
      d+=sprintf(d,"&gt;");
    else
      *d++=c;
    s++;
  }
  return d-d0;
}

void http_sendContent(int s,char*content,char connmode)
{
  int fd=connections[s].fd;
  char hdrs[128];
  int lgt=strlen(content);
  int hdrlgt=sprintf(hdrs,
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html;charset=UTF-8\r\n"
    "Connection: %s\r\n"
    "Content-Length: %d\r\n\r\n",
    connmode?"keep-alive":"close",lgt);
  write(fd,hdrs,hdrlgt);
  write(fd,content,lgt);
  connections[s].mode=HEADERS;
}

void http_sendToAll(char*b)
{
  int i;
  for(i=0;i<MAXCONNECTIONS;i++)
    if(connections[i].mode==CONTENT && connections[i].contentlgt==0)
      http_sendContent(i,b,1);
}

void http_sendTokens(int slot,int i0,int i1)
{
  if(i0<0) i0=0;
  if(i1<i0) return;
  if(i1>MAXCTX) i1=MAXCTX;
  char buf[32768];
  char*d=buf;
  d+=sprintf(d,"%d\t",i1+1);
  if(i0<genstart) d+=sprintf(d,"<b>");
  for(int i=i0;i<=i1;i++)
  {
    char bb[64];
    //sprintf(bb," %s[%d]",joopajoo[rand()&7],i);
    if(i==genstart) d+=sprintf(d,"</b>");
    if(i<=MAXCTX-1) d+=txt2html(d,tokenstrings[context[i]]);
    else if(i==MAXCTX) d+=sprintf(d,"[END OF BUFFER]");
  }
  *d='\0';
  if(slot<0) http_sendToAll(buf);
  else http_sendContent(slot,buf,1);
}

void http_sendToken(int slot,int i)
{
  http_sendTokens(slot,i,i);
}

void generator_run()
{
  fprintf(stderr,"generator running: slot %d\n",currslot);
  if(currslot>=0) runModel(currwv,currslot);
  currslot++;
  int tok=context[currslot];
  if(tok<0 || tok>MAXCTX) tok=emptytoken;
  if(currslot>=genstart)
  {
    int match;
    matchToTokens(currwv,matchlist,nummatches,temperature);
    match=pickmatch(matchlist,nummatches,minp);
    tok=matchlist[match].tok;
    context[currslot]=tok;
  }
  http_sendToken(-1,currslot);
  if(currslot>=MAXCTX)
  {
    generator_running=0;
  }
}

void generator_reset(char*prompt)
{
  if(currslot>0) http_sendToAll("0\t<br>[RESET]</br>");
  if(!prompt[0]) prompt="\n";
  int here=tokenize_to_context(prompt,0);
  currslot=-1;
  genstart=here;
  genend=1022;
  generator_running=1;
}

int http_getSlot(int fd)
{
  for(int i=0;i<MAXCONNECTIONS;i++) if(connections[i].fd==fd) return i;
  return -1;
}

int http_getFreeSlot()
{
  for(int i=0;i<MAXCONNECTIONS;i++) if(connections[i].mode==FREE) return i;
  return -1;
}

int http_getOldest()
{
  int oldest=time(NULL)+1,where=-1;
  for(int i=0;i<MAXCONNECTIONS;i++)
    if(connections[i].mode!=FREE && connections[i].age<oldest)
    {
      oldest=connections[i].age;
      where=i;
    }
  return where;
}

void http_sendLocation(int s,char*url)
{
  char hdrs[128];
  int hdrlgt=sprintf(hdrs,
    "HTTP/1.1 303 See Other\r\n"
    "Location: %s\r\n"
    "Content-Length: 0\r\n"
    "\r\n",url);
  write(connections[s].fd,hdrs,hdrlgt);
  fprintf(stderr,"RELOC %s\n",hdrs);
  connections[s].mode=HEADERS;
}

void http_removeConnection(int fd)
{
  printf("LOST CONNECTION %d\n",fd);
  int s=http_getSlot(fd);
  connections[s].mode=FREE;
}

int http_newConnection(int fd)
{
  int removed=0;
  printf("NEW CONNECTION %d\n",fd);
  int i=http_getSlot(fd);
  if(i<0)
  {
    i=http_getFreeSlot();
    removed=connections[i].fd;
  }
  connections[i].fd=fd;
  connections[i].mode=HEADERS;
  connections[i].age=time(NULL);
  connections[i].buflgt=0;
  connections[i].contentlgt=BUFSZ;
  return removed;
}

int parsehex(char c)
{
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  if(c>='A' && c<='F') return c-'A'+10;
  return 0;
}

void http_processLine(int s)
{
  char*ln=connections[s].buf;
//  fprintf(stderr,"LINE (#%d): >",s);
//  for(int i=0;i<connections[s].buflgt;i++) fputc(connections[s].buf[i],stderr);
//  fprintf(stderr,"<\n");
  if(!strncmp(ln,"GET ",4)) connections[s].is_get=1; else
  if(!strncmp(ln,"POST ",5))
  {
    connections[s].is_get=0;
    if(ln[6]=='?') connections[s].arg=atoi(ln+7);
      else connections[s].arg=-1;
  }
  if(!strncasecmp(ln,"Content-Length: ",16))
    connections[s].contentlgt=atoi(ln+16);

  if(connections[s].mode==CONTENT)
  {
    if(!strncmp(ln,"prompt=",7))
    {
      int j=0;
      char*in=connections[s].buf;
      char b[BUFSZ];
      for(int i=7;i<connections[s].buflgt;i++)
      {
        unsigned char c=in[i];
        if(c=='+') c=' '; else
        if(c=='%')
        {
          c=parsehex(in[i+1])*16+parsehex(in[i+2]);
          i+=2;
        }
        if(c>=0x20 || c=='\n')
        {
          b[j]=c;
          j++;
        }
      }
      b[j]='\0';
      if(j!=0)
      {
        fprintf(stderr,"text to tokenize: >%s<\n",b);
        http_sendLocation(s,URL);
        generator_reset(b);
      }
    }
  }

  if(connections[s].buflgt==0)
  {
//    printf("empty line! now in contentmode. contentlength=%d\n",
//      connections[s].contentlgt);
    connections[s].mode=CONTENT;
    if(connections[s].is_get)
    {
      fprintf(stderr,"sending mainpage to #%d (%p)\n",s,indexhtml);
      fprintf(stderr,"mainpage=>%s<\n",indexhtml);
      http_sendContent(s,indexhtml,1);
    } else
    {
      if(connections[s].arg>=0)
      {
        fprintf(stderr,"#%d wants stuff from #%d on (clgt=%d)\n",s,
          connections[s].arg,connections[s].contentlgt);
        if(connections[s].arg<=currslot && currslot<=MAXCTX)
        {
          http_sendTokens(s,connections[s].arg,currslot);
          connections[s].arg=currslot+1;
        }
        else if(connections[s].arg>currslot+1) // unnoticed reset
            http_sendContent(s,"0\t<br>[RESET]<br>",1);
        else
        {
          fprintf(stderr,"waiting for that stuff\n");
          connections[s].mode=CONTENT;
        }
      } else if(connections[s].contentlgt==0)
      {
        fprintf(stderr,"!? redirecting\n");
        http_sendLocation(s,URL);
      }
    }
  }
//  fprintf(stderr,"done processline\n");
}

void http_onReceive(int fd,char*data,int nbytes)
{
  int s;
//  printf("RECEIVE %d: >",fd);
//  for(int i=0;i<nbytes;i++) putchar(data[i]);
//  printf("<\n\n");
  s=http_getSlot(fd);
  if(s<0) return;
  for(int i=0;i<nbytes;i++)
  {
    char c=data[i];
    if(connections[s].mode==HEADERS) {
    if(c=='\r') continue;
    if(c=='\n')
    {
      http_processLine(s);
      connections[s].buflgt=0;
      c=0;
    }
    }
    if(c && connections[s].buflgt<connections[s].contentlgt)
    {
      connections[s].buf[connections[s].buflgt]=c;
      connections[s].buflgt++;
      if(connections[s].mode==CONTENT &&
         connections[s].buflgt==connections[s].contentlgt)
      {
        http_processLine(s);
        connections[s].buflgt=0;
        connections[s].contentlgt=BUFSZ;
      }
    }
  }
//  fprintf(stderr,"received. mode=%d buflgt=%d/%d\n",
//    connections[s].mode,
//    connections[s].buflgt,
//    connections[s].contentlgt);
}

void http_server()
{
  int i;
  fd_set fds;
  fd_set readfds;
  struct sockaddr_in serveraddr;
  struct sockaddr_in clientaddr;
  int maxfd;
  int listener;
  int newfd;
  int rc;
  int yes=1;

  indexhtml=readtextfile("ui_http.html",NULL);
  currslot=-1;
  genend=1022;
  generator_running=0;
  signal(SIGPIPE,dummysignal);

  for(i=0;i<MAXCONNECTIONS;i++) connections[i].mode=FREE;

  listener=socket(AF_INET,SOCK_STREAM,0);
  if(listener==-1)
  {
    perror("socket();");
    exit(1);
  }
  rc=setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));
  if(rc<0)
  {
    perror("setsockopt()");
    exit(1);
  }
  serveraddr.sin_family=AF_INET;
  serveraddr.sin_addr.s_addr=INADDR_ANY;
  serveraddr.sin_port=htons(PORT);
  memset(&(serveraddr.sin_zero),0,8);
  rc=bind(listener,(struct sockaddr*)&serveraddr,sizeof(serveraddr));
  if(rc<0)
  {
    perror("bind()");
    exit(1);
  }
  rc=listen(listener,MAXCONNECTIONS+1);
  if(rc<0)
  {
    perror("listen()");
    exit(1);
  }
  FD_ZERO(&fds);
  FD_ZERO(&readfds);
  FD_SET(listener,&fds);
  maxfd=listener;
  for(;;)
  {
    int numrdy;
    memcpy(&readfds,&fds,sizeof(fds));
    if(!generator_running)
      numrdy=select(maxfd+1,&readfds,NULL,NULL,NULL);
    else
    {
      struct timeval zero={0,0};
      generator_run();
      numrdy=select(maxfd+1,&readfds,NULL,NULL,&zero);
    }
    if(numrdy<0)
    {
      perror("select()");
      exit(1);
    }
    for(i=0;i<=maxfd && numrdy>0;i++)
    {
      if(FD_ISSET(i,&readfds))
      {
        numrdy--;
        if(i==listener) // new connection
        {
          int newfd;
          //int addrlen=sizeof(clientaddr);
          newfd=accept(listener,NULL,NULL); //(struct sockaddr*)&clientaddr,&addrlen);
          if(newfd<0)
          {
            if(errno==EWOULDBLOCK)
            {
              perror("accept()");
              exit(1);
            } else break;
          }
          FD_SET(newfd,&fds);
          if(newfd>maxfd) maxfd=newfd;
          int dropped=http_newConnection(newfd);
          if(dropped>0)
          {
            close(dropped);
            FD_CLR(dropped,&fds);
          }
        } else
        {
          char buf[1024];
          int nbytes=recv(i,buf,sizeof(buf),0);
          if(nbytes>0)
          {
            http_onReceive(i,buf,nbytes);
          }
          else
          {
            // connection closed (0) or error (<0)
            close(i);
            FD_CLR(i,&fds);
            http_removeConnection(i);
          }
        }
      }
    }
  }
}
