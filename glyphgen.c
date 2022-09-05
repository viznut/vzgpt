#include "common.h"

#ifdef HAVE_SDL
int makecol(float r,float g,float b)
{
  if(r<0) r=0;
  if(r>1) r=1;
  if(g<0) g=0;
  if(g>1) g=1;
  if(b<0) b=0;
  if(b>1) b=1;
  return (((int)(r*255))<<16) +
         (((int)(g*255))<<8) +
          ((int)(b*255));
}

void renderblankwordvec(int x0,int y0,int dim)
{
  int i,j;
  for(j=0;j<dim;j++)
  for(i=0;i<dim;i++)
    ((int*)fb->pixels)[(j+y0)*scrw+(i+x0)]=0x888888;
}

void renderwordvec(float*wv0,int x0,int y0,int dim)
{  
  char spots[1024];
  float wv[1024];
  int y,x;
  
  if(x0<0 || y0<0 || x0+dim>scrw || y0+dim>scrh) return;

  /* get some statistics to be used for colors */
  int col0,col1,colbg;
  {
  float pos_locsum=0,pos_sum=0.0001,neg_locsum=0,neg_sum=0.0001;
  float pos_ctr=0,neg_ctr=0,max=0;
  for(x=0;x<WVSIZE;x++)
  {
    float a=fabs(wv0[x%WVSIZE]);
    if(a>max)max=a;
    if(wv0[x]<0) { neg_locsum+=x*a; neg_sum+=a; neg_ctr+=1; }
    else         { pos_locsum+=x*a; pos_sum+=a; pos_ctr+=1; }
  }
  if(pos_ctr==WVSIZE)
  {
    renderblankwordvec(x0,y0,dim);
    return;
  }

  float pos_avgloc = pos_locsum/pos_sum;
  float neg_avgloc = neg_locsum/neg_sum;
  float posvsneg   = pos_sum/neg_sum;
//  fprintf(stderr,"pos_avgloc %f neg_avgloc %f posvsnegctr %f posvsneg %f\n",
//    pos_avgloc,neg_avgloc,pos_ctr-neg_ctr,posvsneg);
  float r0,g0,b0,r1,g1,b1,r2,g2,b2;
  r0 = cos((pos_avgloc-WVSIZE/2)/12.0)*.5+.5;  // was 8.0 & 9.0
  g0 = cos((neg_avgloc-WVSIZE/2)/12.0)*.5+.5;
  b0 = cos(posvsneg)*.5+.5;
  r1 = cos((pos_avgloc+pos_ctr-neg_ctr-WVSIZE/2)/8.0)*.5+.5;
  g1 = cos((neg_avgloc+neg_ctr-pos_ctr-WVSIZE/2)/8.0)*.5+.5;
  b1 = 1.0-(r1+g1)/2.0;
  r2 = 1.0-(r0+r1)/2;
  g2 = 1.0-(g0+g1)/2;
  b2 = 1.0-(b0+b1)/2;
  if(3*r2+6*g2+b2>10.0) { r2=(1+r2)/2; g2=(1+g2)/2; b2=(1+b2)/2; }
                  else  { r2/=2; g2/=2; b2/=2; }
  col0  = makecol(r0,g0,b0);
  col1  = makecol(r1,g1,b1);
  colbg = makecol(r2,g2,b2);

  float spotlim=(max*3+(pos_sum/pos_ctr))/4;
  if(spotlim<quanter_wte) spotlim=quanter_wte;
  for(x=0;x<1024;x++)
  {
    float a=wv0[x%WVSIZE];
    if(a<-spotlim) spots[x]=-1; else
    if(a> spotlim) spots[x]= 1; else
                   spots[x]= 0;
  }
  }

  /* copy wv0, fade out the extremities */
  for(y=0;y<32;y++)
  for(x=0;x<32;x++)
  {
    float r2=(y-16)*(y-16)+(x-16)*(x-16);
    float a = wv0[(y*32+x)%WVSIZE]/sqrt(r2+4.0);
    if(a>1.0) a=1.0;
    if(a<-1.0) a=-1.0;
    wv[y*32+x] = a; //wv0[(y*32+x)%lgt]/sqrt(r2+4.0);
  }

  /* blur it a few times. */
  for(y=0;y<8;y++)  
  for(x=0;x<1024;x++)
    wv[x]= (wv[x&1023]*2 + wv[(x-32)&1023] + wv[(x+32)&1023] +
           wv[(x-1)&1023] + wv[(x+1)&1023] /* + wv[(x^31)&1023] +
           wv[(x^(31*32))&1023] */ ) / 6.0;

  /* find max after blur */
  float max=0;
  for(x=0;x<1024;x++)
  {
    float a=fabs(wv[x]);
    if(a>max) max=a;
  }

  /* find threshold that gives nice proportion of negative and positive space */
  float range0=0,range1=max,lim;
  for(y=0;y<6;y++) {
    lim=(range1+range0)/2;
    int c=0;
    for(x=0;x<1024;x++) if(fabs(wv[x])>lim) c++;
    if(c>=160 && c<=224) break;
    if(c<160) range1=lim;
    if(c>224) range0=lim;
  }

  /* render */
  for(y=0;y<dim;y++)
  for(x=0;x<dim;x++)
  {
    int c;
    int j=(y*32)/dim;
    int i=(x*32)/dim;
    i+=j*32;
    if(spots[i]>0) c=0xffffff; else
    if(spots[i]<0) c=0x000000; else
    if(wv[i]>lim)  c=col0; else
    if(wv[i]<-lim) c=col1; else c=colbg;
    ((int*)fb->pixels)[(y+y0)*scrw+(x+x0)]=c;//pal[o];
  }
}

void renderwordvec_pkd(wte_t*wv0,int x0,int y0,int dim)
{
  float wv[WVSIZE];
  int i;
  for(i=0;i<WVSIZE;i++) wv[i]=UNPKWTE(wv0[i]);
  renderwordvec(wv,x0,y0,dim);
}

void renderlayernode(float*wv0,float*att,int numheads,int x0,int y0)
{
  int i,j,x,y;
  if(x0<0 || y0<0 || x0+64>scrw || y0+16>scrh) return;
  if(!wv0 || !att) return;

  float wv[1024];
  for(x=0;x<1024;x++) wv[x]=wv0[x%WVSIZE];

  for(y=0;y<8;y++)  
  for(x=0;x<1024;x++)
    wv[x]= (wv[x&1023]*2 + wv[(x-64)&1023] + wv[(x+64)&1023] +
           wv[(x-1)&1023] + wv[(x+1)&1023] /* + wv[(x^31)&1023] +
           wv[(x^(31*32))&1023] */ ) / 6.0;

  float r=0,g=0,b=0;
  for(i=0;i<NUMHEADS;i+=3)
  {
    r+=att[(i+0)%NUMHEADS];
    g+=att[(i+1)%NUMHEADS];
    b+=att[(i+2)%NUMHEADS];
  }
  int fg=makecol((r*3)/12,(g*3)/12,(b*3)/12);
  int bg=(fg&0xfefefe)>>1;

  for(j=0;j<16;j++)
  {
    for(i=0;i<64;i++)
    {
      float a=wv[(64*j+i)];
      ((int*)fb->pixels)[(j+y0)*scrw+(i+x0)]=a<0?bg:fg;
    }
  }
}
#endif
