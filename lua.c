#include "common.h"
#ifdef HAVE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#ifdef USE_LIBEDIT
#include <editline/readline.h>
#else
#include <readline/readline.h>
#endif

extern int breaks_called;

int l_tok(lua_State*L)
{
  lua_concat(L,lua_gettop(L));
  const char*s=lua_tostring(L,1);
  lua_newtable(L);
  int i=1;
  while(*s)
  {
    int tok=tokenize(s);
    lua_pushinteger(L,i);
    lua_pushinteger(L,tok);
    lua_settable(L,2);
    s+=strlen(tokenstrings[tok]);
    i++;
  }
  lua_replace(L,1);
  return 1;
}

char*strappend(char*s,int*len,char*n)
{
  int nl=strlen(n);
  s=realloc(s,nl+*len+1);
  strcpy(s+*len,n);
  (*len)+=nl;
  return s;
}

char*l_detok_top(lua_State*L,char*s,int*len)
{
  int n=lua_gettop(L);
  if(n<=0) return s;
  if(lua_istable(L,n))
  {
    int j;
    int nn=lua_rawlen(L,n);
    for(j=1;j<=nn;j++)
    {
      lua_pushinteger(L,j);
      lua_gettable(L,n);
      s=l_detok_top(L,s,len);
    }
  } else
  {
    int tok=lua_tointeger(L,n);
    char boh[5];
    if(tok>=0 && tok<numtokens)
      s=strappend(s,len,tokenstrings[tok]);
  }
  lua_pop(L,1);
  return s;
}

int l_detok(lua_State*L)
{
  char*s=strdup("");
  int len=0;
  while(lua_gettop(L)>=1) s=l_detok_top(L,s,&len);
  lua_pushstring(L,s);
  free(s);
  return 1;
}

int l_setslot(lua_State*L)
{
  int i=lua_tointeger(L,1);
  int t=lua_tointeger(L,2);
  i--;
  if(i>=0 && i<CTXSIZE) context[i]=t;
  return 0;
}

int l_getslot(lua_State*L)
{
  int i=lua_tointeger(L,1)-1,t;
  lua_pop(L,lua_gettop(L));
  if(i>=0 && i<CTXSIZE) t=context[i]; else t=emptytoken;
  lua_pushinteger(L,t);
  return 1;
}

int l_getcontext(lua_State*L)
{
  int i;
  int lgt=lua_tointeger(L,1);
  lua_pop(L,lua_gettop(L));
  lua_newtable(L);
  for(i=0;i<lgt;i++)
  {
    lua_pushinteger(L,i+1);
    lua_pushinteger(L,context[i]);
    lua_settable(L,1);
  }
  return 1;
}

int l_runstep(lua_State*L)
{
  int i=lua_tointeger(L,1);
  i--;
  if(i>=0 && i<CTXSIZE) runModel(currwv,i);
  return 0;
}

int l_match(lua_State*L)
{
  float temp=1.0;
  if(lua_gettop(L)>=1) nummatches=lua_tointeger(L,1);
  if(lua_gettop(L)>=2) temp=lua_tonumber(L,2);
  if(nummatches>MAXNUMMATCHES) nummatches=MAXNUMMATCHES;
  matchToTokens(currwv,matchlist,80,temp); // maxnummatches, temp
  return 0;
}

int l_getmatches(lua_State*L)
{
  int i;
  lua_pop(L,lua_gettop(L));
  lua_newtable(L);
  for(i=0;i<nummatches;i++)
  {
    lua_pushinteger(L,i*2+1);
    lua_pushnumber(L,matchlist[i].prob);
    lua_settable(L,1);
    lua_pushinteger(L,i*2+2);
    lua_pushinteger(L,matchlist[i].tok);
    lua_settable(L,1);
  }
  return 1;
}

int l_choosenext(lua_State*L)
{
  float minp=0.001;
  int n=lua_gettop(L);
  if(n>=1) nummatches=lua_tointeger(L,1);
  if(n>=2) minp=lua_tonumber(L,2);
  if(nummatches>MAXNUMMATCHES) nummatches=MAXNUMMATCHES;
  int match=pickmatch(matchlist,nummatches,minp);
  lua_pop(L,n);
  lua_pushinteger(L,matchlist[match].tok);
  return 1;
}

int l_getvec(lua_State*L)
{
  // token -> array
  // void -> array	gets current wv
}

int l_setvec(lua_State*L)
{
  // array -> void
}

int l_getvar(lua_State*L)
{
  // name -> val
}

int l_setvar(lua_State*L)
{
  // name,val -> void
}


int l_breaking(lua_State*L)
{
  lua_pop(L,lua_gettop(L));
  lua_pushboolean(L,breaks_called?1:0);
  return 1;
}

lua_State*luactx;

char*vzlua_readline_completions(const char*txt,int state)
{
  lua_getglobal(luactx,"getcompletion");
  lua_pushstring(luactx,txt);
  lua_pushinteger(luactx,state);
  lua_call(luactx,2,1);
  if(lua_isnil(luactx,lua_gettop(luactx))) return NULL;
  /* there doesn't seem to be a way to avoid string escapes in libedit,
     so we'll have to modify the buffer directly */
  rl_insert_text(lua_tostring(luactx,-1));
  /* rl_redisplay() would throw libedit into search mode(!?) so we'll
     have to kludge this in another way */
  fprintf(stderr,"\r");
  rl_forced_update_display();
  return NULL;
}

int l_readline(lua_State*L)
{
  const char*prompt="> ";
  char*line;
  int n=lua_gettop(L);
  if(n>=1) prompt=lua_tostring(L,1);
  breaks_called=0;
  //rl_pre_input_hook=l_readline_hook;
  //rl_bind_key("\x12",NULL);
  rl_completion_entry_function=vzlua_readline_completions;
  //rl_filename_completion_desired=0; // not impl in bsd
  rl_basic_word_break_characters="";
  line=readline(prompt);//linenoise(prompt);
  //free(l_readline_prebuf);
  if(n<2 && line && *line) add_history(line);//linenoiseHistoryAdd(line);
//  if(n<2 && linenoiseKeyType()>0)
//  {
//    lua_pop(L,n);
//    lua_pushstring(L,"quit");
//  }
//  else
  {
    lua_pop(L,n);
    lua_pushstring(L,line);
  }
  if(line) free(line);
  return 1;
}

void vzlua_dumpstack()
{
  int i,n=lua_gettop(luactx);
  fprintf(stderr,"stacktop=%d\n",n);
  if(n>=1)
  {
    //fprintf(stderr,"Returned:\n");
    for(i=1;i<=n;i++)
    {
      fprintf(stderr,">%s<\n",lua_tostring(luactx,i));
    }
  }
  lua_pop(luactx,n);
}

int vzlua_runstring(const char*s)
{
  //luaL_loadstring(luactx,s);
  int rc=luaL_dostring(luactx,s);
  /*
  vzlua_reader_kludge k;
  k.data=s;
  k.flag=0;
  int rc=lua_load(luactx,vzlua_reader,&k,"blah",NULL);
  fprintf(stderr,"RUNSTRING >%s<\n",s);
  fprintf(stderr,"lua_loaded\n");
  */
  if(rc==LUA_OK)
  {
    //fprintf(stderr,"call!\n");
    rc=lua_pcall(luactx,0,LUA_MULTRET,0); // why returns 2?
    fprintf(stderr,"vzgpt.lua executed (%d)\n",rc);
    return 0;
  } else
  {
    const char*msg=lua_tostring(luactx,1);
    fprintf(stderr,"Error: %s\n",msg);
    lua_pop(luactx,1);
  }
  vzlua_dumpstack();
  return rc;
}

int vzlua_runfile(char*fn)
{
  char*s=readtextfile(fn,NULL);
  if(!s) return -1;
  return vzlua_runstring(s);
}

static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize)
{
  if(nsize==0)
  {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

int vzlua_handlesignal(int s)
{
  breaks_called++;
  return 0;
}

void*vzlua_funcs[]=
{
//  "loadmodel",   l_loadmodel,
  "tok",         l_tok,
  "detok",       l_detok,
  "readline",    l_readline,
  "match",       l_match,
  "getmatches",  l_getmatches,
  "choosenext",  l_choosenext,
  "runstep",     l_runstep,
  "getcontext",  l_getcontext,
  "setslot",     l_setslot,
  "getslot",     l_getslot,
  "breaking",    l_breaking,
  NULL
};

void vzlua(char*scriptfile)
{
  luactx=luaL_newstate();//l_alloc,NULL);
  luaL_openlibs(luactx);
  void**s=vzlua_funcs;
  while(*s)
  {
    lua_pushcfunction(luactx,(lua_CFunction)(s[1]));
    lua_setglobal(luactx,(const char*)s[0]);
    s+=2;
  }
  int rc=vzlua_runfile("vzgpt.lua");
  if(rc)
  {
    fprintf(stderr,"failed to load vzgpt.lua!\n");
    return;
  }
  signal(SIGINT,vzlua_handlesignal);
  if(scriptfile)
  {
    fprintf(stderr,"running script from file: %s\n",scriptfile);
    vzlua_runfile(scriptfile);
  } else
  {
    fprintf(stderr,"starting cli...\n");
    lua_getglobal(luactx,"cli");
    lua_call(luactx,0,0);
  }
}

#endif
