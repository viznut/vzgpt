--- this file is automatically run by vzgpt when started up in lua mode

--- some constants (variables)

nummatches=40
temp=1.0
minp=0.001
chatprefix="|kysymys|"
chatsuffix="\n|vastaus|"
ender="\n"

--- essential data structures and manipulation functions

ctx={
  lastset = 0,
  lastvalid = 0,
  lastrun = 0,
  lastmatched = 0,
  maxsize = 1024,
  set = function(b)
    b=sanitize(b,(15*ctx.maxsize)/16)
    --printobj(b)
    --print('<= set')
    if b[1]~=ctx[1] then ctx.lastvalid=0 end
    for i=1,math.min(ctx.lastvalid,#b,ctx.maxsize) do
      if ctx[i]~=b[i] then break end
      ctx.lastvalid=i
    end
    ctx.lastset=math.min(#b,ctx.maxsize)
    for i=ctx.lastvalid+1,#ctx do
      ctx[i]=b[i]
    end
    ctx.lastrun=0
    ctx.lastmatched=0
  end,
  append = function(b)
    b=detok(ctx.get())..b
    ctx.set(b)
  end,
  get = function() return getcontext(ctx.lastset) end
}
setmetatable(ctx,{
  __newindex = function(t,k,v) setslot(k,v) end,
  __index = function(t,k) return getslot(k) end,
  __tostring = function(t) return detok(ctx.get()) end,
  __len = function(t) return t.lastset end
})

function sanitize(b,maxlgt)
  if type(b)=='string' then b=tok(b) end
  if #b>maxlgt then
    c={}
    for i=#b-maxlgt+1,#b do
      c[i-(#b-maxlgt)]=b[i]
    end
    b=c
  end
  return b
end

--- generator functions

function validate()
  if ctx.lastset>=ctx.maxsize then
    h={}
    for i=1,(ctx.maxsize/2) do h[i]=ctx[i+ctx.maxsize/2] end
    ctx.set(h)
  end
  --if ctx.lastvalid<ctx.lastset-10 and not noisy then
  --  io.write("["..tostring(ctx.lastset-ctx.lastvalid).."...]\n")
  --  io.flush()
  --end
  while ctx.lastvalid<ctx.lastset do
    ctx.lastvalid=ctx.lastvalid+1
    if noisy then
      io.write(detok(ctx[ctx.lastvalid]))
      io.flush()
    else
      i=ctx.lastset-ctx.lastvalid-4
      if i>0 then
        i=(i%4)+1
        io.write(string.sub('\\|/-',i,i).."\b")
        io.flush()
      end
    end
    --print('validating at ',ctx.lastvalid)
    runstep(ctx.lastvalid)
    ctx.lastrun=ctx.lastvalid
    if breaking() then break end
  end
end

function genstep()
  validate()
  if breaking() then return end
  if ctx.lastrun~=ctx.lastset then
    runstep(ctx.lastset)
    ctx.lastrun=ctx.lastset
    ctx.lastvalid=ctx.lastset
  end
  if ctx.lastmatched~=ctx.lastset then
    match(nummaches,temp)
    ctx.lastmatched=ctx.lastset
  end
  t=choosenext(nummatches,minp)
  ctx.lastvalid=ctx.lastset
  ctx.lastset=ctx.lastset+1
  ctx[ctx.lastset]=t
  return t
end

function listmatches()
  if ctx.lastrun~=ctx.lastset then
    runstep(ctx.lastset)
    ctx.lastrun=ctx.lastset
  end
  m=getmatches()
  for i=1,#m,2 do
    print(i,detok(m[i]),m[i+1])
  end
end

function gen(ender)
  noisy=true
  validate(true)
  noisy=false
  tt=''
  while not breaking() do
    t=detok(genstep())
    io.write(t)
    io.flush()
    if ender~=nil then
      tt=tt..t
      if string.sub(tt,0-#ender)==ender then break end
    end
  end
  io.write("\n")
end

--- interactive commands

function g()
  gen(nil)
end

function l()
  gen("\n")
  -- generate until
  -- string.sub(a,0-#ender)==ender
end

function p()
  return detok(ctx.get())
end

function c()
  while true do
    txt=readline('human> ')
    if txt==nil or txt=='' then break end
    ctx.append(chatprefix..txt..chatsuffix)
    io.write("\ncomputer> ")
    io.flush()
    --noisy=true
    validate()
    --noisy=false
    l()
  end
end

function fixlines()
  txt=detok(ctx.get())
  txt,n0=string.gsub(txt,'\n([^%s])',' %1')
  txt,n1=string.gsub(txt,'\n%s+','\n')
  ctx.set(txt)
  io.write(tostring(n0+n1).." substitutions.\n")
end

function sb()
  ctx.set('')
  print("Press ^D when finished.")
  while true do
    l=readline('lines> ')
    if l==nil then break end
    if ctx.lastset>0 then ctx.append("\n") end
    ctx.append(l)
  end
  print("Context initialized! Use fixlines() to remove hard linebreaks.")
end

function v()
io.write(string.format([[
temp=%f
nummatches=%.0f
minp=%f
chatprefix=%q
chatsuffix=%q
ender=%q
]],temp,nummatches,minp,chatprefix,chatsuffix,ender))
end

function h()
io.write([[
==========================[ vzgpt-lua help ]===========================
g : generate until ^C pressed   <tab>   : edit context buffer
l : generate until newline      =<text> : set context buffer
c : chat mode                   -<text> : set last paragraph of buffer
v : show variables              +<text> : append to context buffer
m : list matches                sb      : set buffer (multiple lines)
p : print context buffer
h : this help text
q : quit                        lua commands are also accepted
=======================================================================
]])
end

--- other functions related to the command line interface

function printobj(o)
  if type(o)=='table' and o~=ctx then
    io.write('{')
    for j=1,#o do
      if j>=2 then io.write(',') end
      printobj(o[j])
    end
    io.write("}")
  else
    io.write(tostring(o))
  end
end

function printobjs(...)
  a={...}
  for i=1,#a do
    printobj(a[i])
    io.write("\n")
  end
end

function trim(s)
  while string.sub(s,-1)=="\n" or string.sub(s,-1)==' ' do
    s=string.sub(s,1,-2)
  end
  return s
end

function splitlastline(s)
  s=trim(s)
  --while string.sub(s,-1)=="\n" do s=string.sub(s,1,-2) end
  --i=string.find(s,"\n",-1,true)
  pt=0
  for i=#s,1,-1 do
    if string.sub(s,i,i)=="\n" then pt=i break end
  end
  if pt~=nil then
    return trim(string.sub(s,1,pt-1)),string.sub(s,pt+1)
  else
    return '',s
  end
end

function getcompletion(s,state)
  --print('getcompletion',s,state)
  if state>0 then return nil end
  if s=='' then
    return '='..detok(ctx.get())
  elseif s=='=' then
    return detok(ctx.get())
  elseif s=='-' then
    s,e=splitlastline(detok(ctx.get()))
    return e
  else
    return nil
  end
end

function cli()
  h()
  while true do
    cmd=readline()
    if cmd=='' then
    elseif cmd=='quit' or cmd=='q' or cmd==nil then
      print "QUIT!"
      break
    elseif string.sub(cmd,1,1)=='=' then
      ctx.set(string.sub(cmd,2))
    elseif string.sub(cmd,1,1)=='-' then
      s,e=splitlastline(detok(ctx.get()))
      ctx.set(s.."\n"..string.sub(cmd,2))
    elseif string.sub(cmd,1,1)=='+' then
      ctx.set(detok(ctx.get()).."\n"..string.sub(cmd,2))
    else
      op=nil
      if _ENV[cmd]~=nil and type(_ENV[cmd])=='function' then
        op,err=load('printobjs('..cmd..'())')
      else
        op,err=load('printobjs('..cmd..')')
      end
      if op==nil then
        op,err=load(cmd) end
      if op~=nil then
        succ,msg=pcall(op,msg)
        if not succ then
          print("Runtime error: ",msg)
        end
      else
        print("Syntax error: "..err)
      end
    end
  end
end

noisy=false
