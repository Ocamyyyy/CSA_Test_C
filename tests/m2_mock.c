/* SOFTWARE MODEL ONLY. Real M2/Switch/DMA orchestration, no device access.
 * Integer arithmetic below is a test model, never a replacement for board data. */
#include "m2.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static struct {
 uint32_t dma[64],in[64],out[64],status,control;unsigned char memory[1048576],coeff[16384];
 unsigned samples,bytes,starts,clears,resets,tx,rx,commits,polls,edge_load,edge_process;
 int active,armed,operation,valid,tx_device,rx_device,restore_blocked,inspect,partial;
 uint64_t tick,off;
} m;
static const char *fault;
static int is(const char *name){return fault&&!strcmp(fault,name);}
uint64_t __wrap_now_ms(void){return m.tick++;}
static uint64_t gpio_clock(void *ctx){(void)ctx;return m.tick++;}
static void gpio_delay(void *ctx,unsigned us){(void)ctx;(void)us;}
static int64_t rounded(int64_t n){
 int64_t q=n/32768,r=n%32768,a=r<0?-r:r;
 if(a>16384||(a==16384&&(q%2)))q+=n<0?-1:1;
 return q;
}
static int16_t saturated(int64_t n,uint32_t flag){
 int64_t q=rounded(n);if(q>32767){m.status|=flag;return 32767;}if(q< -32768){m.status|=flag;return -32768;}return (int16_t)q;
}
static void complete(void){
 if(m.operation==1){
  memcpy(m.coeff,m.memory,m.bytes);m.valid=1;m.status=M2_LOAD_DONE;
  if(is("load-tlast")){m.status=M2_ERROR|(3u<<4);m.valid=0;}
  if(is("load-tkeep")){m.status=M2_ERROR|(4u<<4);m.valid=0;}
 }else{
  m.status=M2_PROCESS_DONE;
  assert(m.valid&&m.tx_device&&m.rx_device);
  for(unsigned i=0;i<m.samples;i++){
   int16_t di,dq,ci,cq;uint32_t d,c,result;
   memcpy(&d,m.memory+i*4,4);memcpy(&c,m.coeff+i*4,4);
   unpack_iq(d,1,&di,&dq);unpack_iq(c,1,&ci,&cq);
   int16_t ri=saturated((int64_t)di*ci-(int64_t)dq*cq,M2_SAT_I);
   int16_t rq=saturated((int64_t)di*cq+(int64_t)dq*ci,M2_SAT_Q);
   result=pack_iq(ri,rq,1);memcpy(m.memory+m.off+i*4,&result,4);
  }
  if(is("process-tlast"))m.status=M2_ERROR|(3u<<4);
  if(is("process-tkeep"))m.status=M2_ERROR|(4u<<4);
  if(is("done-timeout"))m.status=0;
 }
 m.dma[1]=IOC|2;if(m.operation==2){m.dma[0x34/4]=IOC|2;m.dma[0x58/4]=is("short-rx")?m.bytes-4:m.bytes;}
 m.active=0;
}
static uint32_t reg_read(void *ctx,unsigned off){
 if(ctx==m.in||ctx==m.out){uint32_t *r=ctx;
  if(off==0&&(r[0]&2)&&!is("switch-timeout"))r[0]=0;
  if(is("switch-readback")&&ctx==m.in&&off==0x44&&m.commits==2)return 1;
  return r[off/4];
 }
 assert(ctx==m.dma);
 if(off==4&&m.active){
  if(is("cancel")){stopped=1;return m.dma[1];}
  if(!is("dma-timeout")&&!is("abort-timeout")&&++m.polls>=2)complete();
 }
 return m.dma[off/4];
}
static void reg_write(void *ctx,unsigned off,uint32_t v){
 if(ctx==m.in||ctx==m.out){uint32_t *r=ctx;assert(!m.active&&!(m.status&M2_BUSY));r[off/4]=v;
  if(off==0){assert(v==2);m.commits++;}return;
 }
 assert(ctx==m.dma);
 /* No S2MM write whatsoever during coefficient-load operation. */
 if(m.starts==1)assert(off<0x30);
 if((off==0||off==0x30)&&(v&4)){
  m.resets++;
  if(is("abort-timeout")&&m.resets>1){m.dma[0]=4;return;}
  memset(m.dma,0,sizeof m.dma);m.dma[1]=m.dma[0x34/4]=1;m.active=m.armed=0;
  return;
 }
 if(off==4||off==0x34){assert(v==0x1000||v==0x7000);m.dma[off/4]&=~v;return;}
 if(off==0||off==0x30){assert(v==1);m.dma[off/4]=v;m.dma[off/4+1]&=~1u;return;}
 m.dma[off/4]=v;
 if(off==0x58){assert(m.operation==2&&v==m.bytes&&m.starts==2);assert(m.dma[0x48/4]==0x30000000+m.off);assert(!(m.dma[0x34/4]&IOC));m.armed=1;m.rx++;m.dma[0x34/4]&=~2u;}
 if(off==0x28){
  assert(v==m.bytes&&v==4*m.samples&&m.tx_device);assert(m.dma[0x18/4]==0x30000000);assert(!(m.dma[1]&IOC));
  if(m.operation==1)assert(!m.armed&&!m.rx&&(m.dma[0x34/4]&1));
  else assert(m.armed&&m.rx==1&&m.valid&&m.clears==1&&m.resets==1);
  m.armed=0;m.tx++;m.active=1;m.polls=0;m.dma[1]&=~2u;
  if(is("dma-error")){m.dma[1]=0x4020;m.active=0;m.status=0;}
 }
}
static int control(void *ctx,uint32_t word){
 (void)ctx;uint32_t old=m.control;m.control=word;
 if((word&M2_CLEAR)&&!(old&M2_CLEAR)){
  assert(m.starts==0&&((word>>2)&0x1fff)==m.samples);m.clears++;m.valid=0;if(!is("clear-timeout"))m.status&=M2_LOAD_DONE|M2_PROCESS_DONE;
 }
 if((word&M2_START)&&!(old&M2_START)){
  assert(m.commits==2&&m.in[0x40/4]==0x80000000&&m.in[0x44/4]==0&&m.out[0x40/4]==1);
  assert(((word>>2)&0x1fff)==m.samples);m.starts++;
  if(is("stale-done")||(is("stale-process")&&m.starts==2)){m.status=M2_LOAD_DONE|M2_PROCESS_DONE;return 0;}
  if(word&2){m.edge_load++;m.operation=1;assert(m.starts==1&&m.clears==1&&!m.valid);}
  else {m.edge_process++;m.operation=2;assert(m.starts==2&&m.valid&&m.clears==1&&m.resets==1);}
  m.status=M2_BUSY;
  if(is("coeff-invalid")&&m.starts==2){m.valid=0;m.status=M2_ERROR|(2u<<4);}
  if(is("bad-length"))m.status=M2_ERROR|(2u<<4);
  if(is("busy-start"))m.status=M2_ERROR|(1u<<4);
  if(is("internal"))m.status=M2_ERROR|(7u<<4);
 }
 return 0;
}
static int get_status(void *ctx,uint32_t *s){(void)ctx;static unsigned reads;*s=m.status;if(is("status-unstable"))*s^=(reads++&1);return 0;}
int __wrap_dma_unbound(void){return 0;}
int __wrap_mmio_no_driver(uint64_t address){assert(address==INPUT_SWITCH_BASE||address==OUTPUT_SWITCH_BASE);return 0;}
int __wrap_dma_open(struct dma *d,int writable){
 assert(writable==!m.inspect);d->fd=-1;d->regs=m.dma;d->ctx=m.dma;d->read=reg_read;d->write=reg_write;m.dma[1]=m.dma[0x34/4]=1;return 0;
}
void __wrap_dma_close(struct dma *d){(void)d;if(!is("abort-timeout"))assert(!m.active);}
int __wrap_mmio_map_shared(struct dma *r,int fd,uint64_t address,int writable){
 (void)fd;assert(writable==!m.inspect);memset(r,0,sizeof *r);r->fd=-1;
 r->regs=address==INPUT_SWITCH_BASE?m.in:m.out;r->ctx=(void*)r->regs;r->read=reg_read;r->write=reg_write;return 0;
}
int __wrap_ubuf_info(struct ubuf *u){u->fd=-1;u->phys=0x30000000;u->size=128*1024*1024;return 0;}
int __wrap_ubuf_open(struct ubuf *u,size_t len){assert(len<=sizeof m.memory);u->map=m.memory;u->mapped=len;return 0;}
void __wrap_ubuf_close(struct ubuf *u){(void)u;}
int __wrap_ubuf_sync(uint64_t off,uint64_t len,int direction,int cpu){
 assert(len==m.bytes&&(off==0||off==m.off));assert(direction==(off?0:1));
 if(cpu){assert(!m.active);if(off)m.rx_device=0;else m.tx_device=0;}
 else {assert(!m.active);if(off)m.rx_device=1;else m.tx_device=1;}
 return 0;
}
void __real_ubuf_write_bytes(void *,const unsigned char *,size_t);
void __wrap_ubuf_write_bytes(void *d,const unsigned char *s,size_t n){assert(!m.tx_device&&!m.active);__real_ubuf_write_bytes(d,s,n);}
void __real_ubuf_read_bytes(unsigned char *,const void *,size_t);
void __wrap_ubuf_read_bytes(unsigned char *d,const void *s,size_t n){assert(!m.rx_device&&!m.active);__real_ubuf_read_bytes(d,s,n);}
int __wrap_m2_gpio_discover(struct m2_gpio *g){
 memset(g,0,sizeof *g);g->output=g->input=-1;g->mapping_verified=1;g->set=control;g->get=get_status;g->clock_ms=gpio_clock;g->delay_us=gpio_delay;return 0;
}
int __wrap_m2_gpio_open(struct m2_gpio *g,int output){if(m.inspect)assert(!output);g->input=100;if(output){if(is("gpio-request"))return fail("SOFTWARE injected GPIO request failure");assert(!(m.status&M2_BUSY));g->output=101;}return 0;}
void __wrap_m2_gpio_close(struct m2_gpio *g){if(g->output>=0)assert(!m2_gpio_write(g,g->control&~(M2_START|M2_CLEAR)));g->input=g->output=-1;}
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *data,size_t bytes){
 if(is("output-error")){
  if(!m.partial){m.partial=1;return __real_write(fd,data,bytes/2);}
  errno=ENOSPC;return -1;
 }
 return __real_write(fd,data,bytes);
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--inspect")){
  m.inspect=1;m.status=6;
  int rc=m2_inspect();assert(!rc&&!m.starts&&!m.clears&&!m.resets&&!m.commits&&!m.tx&&!m.rx);
  puts("M2 SOFTWARE inspect: no Switch/DMA/control writes PASS");return 0;
 }
 assert(argc==7);puts("SOFTWARE M2/Switch/DMA MODEL ONLY; NOT A BOARD RESULT");
 fault=getenv("FFTTEST_M2_FAULT");m.samples=(unsigned)atoi(argv[6]);m.bytes=4*m.samples;
 m.status=is("initial-busy")?M2_BUSY:M2_LOAD_DONE|M2_PROCESS_DONE;
 if(getenv("FFTTEST_M2_INITIAL_ERROR")||is("clear-timeout")){m.status|=M2_ERROR|(4u<<4);m.valid=1;}
 m.in[0x40/4]=0;m.in[0x44/4]=0x80000000;m.out[0x40/4]=0;
 struct m2_options o={.input=argv[1],.coeff=argv[2],.output=argv[3],.manifest=argv[5],
  .contract=argv[4],.samples=m.samples,.timeout=5,.require_deployment=1};
 struct contract c;int input=-1,coeff=-1;int rc=m2_validate(&o,&c,&input,&coeff);
 if(!rc){struct ubuf u={.phys=0x30000000,.size=128*1024*1024};size_t len;assert(!m2_buffer_layout(&c,&u,sysconf(_SC_PAGESIZE),m.bytes,&m.off,&len));
  rc=m2_execute(&o,&c,input,coeff);close(input);close(coeff);}
 if(!rc){assert(m.starts==2&&m.clears==1&&m.resets==1&&m.tx==2&&m.rx==1&&m.valid&&m.commits==4);
  assert(m.in[0x40/4]==0&&m.in[0x44/4]==0x80000000&&m.out[0x40/4]==0);}
 if(fault)assert(rc);
 if(is("stale-done")||is("busy-start")||is("bad-length")||is("internal"))assert(m.tx==0&&m.rx==0);
 if(is("load-tlast")||is("load-tkeep"))assert(m.tx==1&&m.rx==0&&m.starts==1);
 if(is("stale-process")||is("coeff-invalid"))assert(m.tx==1&&m.rx==0&&m.starts==2);
 if(is("clear-timeout"))assert(m.starts==0&&m.clears==1);
 if(is("initial-busy"))assert(!m.starts&&!m.clears&&!m.commits&&!m.resets);
 if(is("abort-timeout"))assert(m.tx_device&&m.commits==2);
 printf("M2 mock counts: starts=%u clear=%u DMA_resets=%u MM2S=%u S2MM=%u Switch_commits=%u\n",m.starts,m.clears,m.resets,m.tx,m.rx,m.commits);
 return rc?1:0;
}
