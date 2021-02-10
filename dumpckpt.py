#!/usr/bin/env python

# Probably uses deprecated Tensorflow API (the same used by OpenAI's code)

import sys

if(len(sys.argv)<=1):
  print("Usage: "+sys.argv[0]+" <sourcedir> [<targetdir>]")
  print("Dumps the data from a Tensorflow checkpoint into raw files in <targetdir>.");
  print("If <sourcedir> contains .npy files, also dumps them.");
  print("GPT-2 tokens (encoder.json) are converted to a null-terminated format.")
  print("If <targetdir> is not given, lists the tensors and their formats to stdout.");
  exit(1)

sourcedir=sys.argv[1]
if len(sys.argv)>=3:
   targetdir=sys.argv[2]
else:
   targetdir=None

import os
import struct
import numpy as np
import tensorflow as tf

files = os.listdir(sourcedir)
ckpt = tf.train.latest_checkpoint(sourcedir)
vars = tf.train.list_variables(ckpt)

if targetdir!=None:
  if not os.path.isdir(targetdir):
     os.mkdir(targetdir)
  tlf=open(targetdir+'/tensorlist.txt','w')

# GPT-2 tokenlist

if os.path.isfile(sourcedir+'/encoder.json') and targetdir!=None:
   import json
   print("encoder.json")
   f=open(sourcedir+'/encoder.json', 'r')
   of=open(targetdir+'/tokens.dat','wb')
   tokens = json.load(f)
   o=[]
   for t in tokens:  
     for c in t:
       c=ord(c)
       if c>=0x100 and c<=0x120: c-=0x100
       if c>=0x121 and c<=0x142: c=c-0x121+0x7f
       if c==0x143: c=0xad
       if c==0: # chr(0) becomes ^@
          o.append(94)
          c=64
       o.append(c)
     o.append(0)
   of.write(bytes(o))
   f.close()

# .npy files

for fn in files:
  if fn.endswith('.npy'):
     o = np.load(sourcedir+'/'+fn)

     if targetdir!=None:
        fn+='.raw'

     tlfline="%s %s %s\n"%(fn,o.shape,o.dtype)
     print(tlfline,end='')

     if targetdir!=None:
        tlf.write(tlfline)
        of=open(targetdir+'/'+fn,'wb')
        of.write(o.tobytes())
        of.close()

# ckpt tensors       

for name,dims in vars:

  fn = name
  if targetdir!=None:
     fn=fn.replace('model/','')
     fn=fn.replace('_','')
     fn=fn.replace('/','_')
     fn+='.raw'

  o = tf.train.load_variable(ckpt,name)

  tlfline="%s %s %s\n"%(fn,dims,o.dtype)
  print(tlfline,end='')

  if targetdir!=None:
     tlf.write(tlfline)
     of=open(targetdir+'/'+fn,'wb')
     of.write(o.tobytes())
     of.close()

# that's it

if targetdir!=None:
   tlf.close()
