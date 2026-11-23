#include "ffttest.h"
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
struct mock {
 uint32_t r[64];unsigned seq[128],n;int outcome,stuck_reset;
};
static uint32_t mockread(void *ctx,unsigned o){return ((struct mock*)ctx)->r[o/4];}
static void mockwrite(void *ctx,unsigned o,uint32_t v){
 struct mock *m=ctx;assert(m->n<128);m->seq[m->n++]=o;
 if((o==0||o==0x30)&&(v&4)){
  if(m->stuck_reset){m->r[o/4]=v;return;}
  memset(m->r,0,sizeof m->r);m->r[1]=m->r[0x34/4]=1;return;
 }
 if(o==4||o==0x34){m->r[o/4]&=~v;return;}
 m->r[o/4]=v;if((o==0||o==0x30)&&(v&1))m->r[o/4+1]=0;
 if(o==0x28){
  if(m->outcome==0||m->outcome==3){m->r[1]=m->r[0x34/4]=IOC|2;if(m->outcome==3)m->r[0x58/4]=FRAME-4;}
  if(m->outcome==1)m->r[0x34/4]=0x20;
  if(m->outcome==4)m->r[0x34/4]=1;
  if(m->outcome==5)m->r[1]=m->r[0x34/4]=IOC;
  if(m->outcome==6)m->r[1]=IOC|2;
 }
}
int main(void){
 unsigned char bytes[FRAME],back[FRAME];uint32_t memory[FRAME/4];
 for(unsigned i=0;i<FRAME;i++)bytes[i]=(unsigned char)(i*17);
 ubuf_write_frame(memory,bytes);ubuf_read_frame(back,memory);assert(!memcmp(bytes,back,FRAME));
 ubuf_zero_frame(memory);for(unsigned i=0;i<FRAME/4;i++)assert(memory[i]==0);
 assert(dma_controller_name("/amba_pl/dma@40400000"));
 assert(dma_controller_name("/amba_pl/axi_dma@40400000"));
 assert(dma_controller_name("/amba_pl/dma-controller@40400000"));
 assert(!dma_controller_name("/amba_pl/dma@40400000/dma-channel@40400000"));
 assert(!dma_controller_name("/amba_pl/dma@404000000"));
 struct json j;
 assert(!parse_json("{\"a\":1,\"b\":\"x\",\"c\":true}",&j));uint64_t n;assert(!jnum(&j,"a",&n)&&n==1);assert(jnum(&j,"b",&n));assert(!strcmp(jget(&j,"b"),"x"));
 const char *bad[]={"", "{", "{\"x\":1,}","{\"x\":1,\"x\":2}","{\"x\":-1}","{\"x\":01}","{\"x\":1.0}","{\"x\":18446744073709551616}","{\"x\":{}}","{\"x\":[]}","{}x","{\"x\":\"bad\\n\"}"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(parse_json(bad[i],&j));
 uint64_t off;
 assert(!layout(0x30000000,128*1024*1024,4096,4,14,32,&off)&&off==12288);
 assert(!layout(0x30000000,128*1024*1024,65536,64,23,32,&off)&&off==131072);
 assert(layout(0x30000001,128*1024*1024,4096,4,14,32,&off));
 assert(layout(0x30000000,128*1024*1024,4096,4,13,32,&off));
 assert(layout(0x30000000,8192,4096,4,14,32,&off));
 assert(layout(UINT64_MAX-1024,128*1024*1024,4096,4,14,64,&off));
 assert(layout(0xfffff000,128*1024*1024,4096,4,14,32,&off));
 assert(layout(0x30000000,128*1024*1024,4096,3,14,32,&off));
 assert(layout(0x30000000,128*1024*1024,4000,4,14,32,&off));
 assert(layout(0x30000000,128*1024*1024,4096,16384,23,32,&off));
 assert(!layout(0x100000000ULL,128*1024*1024,4096,4,14,40,&off));
 for(unsigned order=0;order<2;order++)for(int i=-32768;i<=32767;i+=257){int16_t a,b;uint32_t w=pack_iq(i,-i-1,order);unpack_iq(w,order,&a,&b);assert(a==i&&b==-i-1);}
 assert(!dma_window_valid(0,1073741824,0x30000000,12288));
 assert(dma_window_valid(0,1073741824,0x40000000,12288));
 assert(dma_window_valid(0,1073741824,0x3ffff000,12288));
 assert(dma_window_valid(0,1073741824,UINT64_MAX-1023,12288));
 assert(dma_window_valid(0,0,0,12288));
 assert(dma_window_valid(UINT64_MAX,2,UINT64_MAX,0));
 assert(!dma_window_valid(0,20480,0,12288));
 assert(dma_window_valid(0,20479,0,12288));
 assert(pack_iq(-2,0x1234,1)==0x1234fffe);
 assert((0x1&1)&&!(0x1&2)&&!(0x1&ERR_MASK));assert((0x1002&(IOC|2))==(IOC|2));
 const unsigned errbits[]={4,5,6,8,9,10,14};for(unsigned i=0;i<7;i++)assert(ERR_MASK&(1u<<errbits[i]));
 for(int outcome=0;outcome<=6;outcome++){
  struct mock m={.outcome=outcome};m.r[1]=m.r[0x34/4]=1;
  struct dma d={.read=mockread,.write=mockwrite,.ctx=&m};
  assert(!dma_reset(&d,5));int r=dma_transfer(&d,0x123456780ULL,0x123459780ULL,40,3);assert((r==0)==(outcome==0));
  unsigned rx=0,tx=0;for(unsigned i=0;i<m.n;i++){if(m.seq[i]==0x58)rx=i;if(m.seq[i]==0x28)tx=i;}
  assert(rx<tx);assert(m.r[0x1c/4]==1&&m.r[0x4c/4]==1);
  if(r)assert(!dma_abort(&d,5));
 }
 struct mock busy={0};struct dma d={.read=mockread,.write=mockwrite,.ctx=&busy};
 assert(dma_reset(&d,1)&&busy.n==0);busy.r[1]=9;assert(dma_reset(&d,1)&&busy.n==0);
 struct mock cancel={.outcome=2};cancel.r[1]=cancel.r[0x34/4]=1;d.ctx=&cancel;assert(!dma_reset(&d,1));stopped=1;assert(dma_transfer(&d,0,4096,32,3));stopped=0;
 struct mock stuck={.stuck_reset=1};stuck.r[1]=stuck.r[0x34/4]=1;d.ctx=&stuck;assert(dma_reset(&d,2));assert(dma_abort(&d,2));
 puts("core tests PASSED (JSON, packing, bounds, SR bits, reset, RX-before-TX, MSB, completion/error/timeout/short-frame/halt/cancel)");return 0;
}
