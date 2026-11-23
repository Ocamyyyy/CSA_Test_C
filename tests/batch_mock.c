/* SOFTWARE ONLY: echo model for exercising real Batch/DMA control code.
 * No real device backends are invoked and no FFT correctness is claimed. */
#include "ffttest.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static struct {
 uint32_t r[64];unsigned started,completed,resets,configured,clear[2],polls;
 int active,armed,in_device,out_device,output_fd,partial,input_fd;
 unsigned char memory[32768];uint64_t off;
} m={.output_fd=-1};
uint64_t __wrap_now_ms(void){static uint64_t tick;return tick++;}
static const char *fault;
static unsigned fault_frame=1;
static int isfault(const char *name){return fault&&!strcmp(fault,name);}
static void complete(void){
 memcpy(m.memory+m.off,m.memory,FRAME);m.r[1]=m.r[0x34/4]=IOC|2;
 m.r[0x58/4]=isfault("short")&&m.started-1==fault_frame?FRAME-4:FRAME;
 m.completed++;m.active=0;
}
static uint32_t regread(void *ctx,unsigned off){
 (void)ctx;
 if((off==4||off==0x34)&&m.active){
  if(isfault("cancel")&&m.started-1==fault_frame){stopped=1;return m.r[off/4];}
  if(!(isfault("timeout")&&m.started-1==fault_frame)&&++m.polls>=4)complete();
 }
 return m.r[off/4];
}
static void regwrite(void *ctx,unsigned off,uint32_t value){
 (void)ctx;
 if((off==0||off==0x30)&&(value&4)){
  m.resets++;memset(m.r,0,sizeof m.r);m.r[1]=m.r[0x34/4]=1;m.active=m.armed=0;return;
 }
 if(off==4||off==0x34){
  assert(value==IOC||value==0x7000);
  if(value==IOC)m.clear[off==4?0:1]++;
  if(isfault("ioc-stuck")&&m.started==1&&value==IOC)return;
  m.r[off/4]&=~value;return;
 }
 if(off==0||off==0x30){assert(value==1);m.r[off/4]=value;m.r[off/4+1]&=~1u;return;}
 m.r[off/4]=value;
 if(off==0x58){
  assert(value==FRAME&&!m.active&&!m.armed);assert(!(m.r[0x34/4]&IOC));
  assert(m.r[0x48/4]==0x30000000+m.off);m.armed=1;m.r[0x34/4]&=~2u;
 }
 if(off==0x28){
  assert(value==FRAME&&m.armed&&!m.active&&m.configured==1);
  assert(m.in_device&&m.out_device&&!(m.r[1]&IOC));assert(m.r[0x18/4]==0x30000000);
  assert(m.clear[0]==m.started+1&&m.clear[1]==m.started+1);
  m.armed=0;m.started++;m.polls=0;m.r[1]&=~2u;
  if(isfault("error")&&m.started-1==fault_frame){m.r[0x34/4]=0x4020;return;}
  m.active=1;
 }
}
int __wrap_dma_unbound(void){return 0;}
int __wrap_dma_open(struct dma *d,int writable){
 assert(writable);d->fd=-1;d->regs=m.r;d->read=regread;d->write=regwrite;m.r[1]=m.r[0x34/4]=1;return 0;
}
void __wrap_dma_close(struct dma *d){(void)d;assert(!m.active);}
int __wrap_ubuf_info(struct ubuf *u){u->fd=-1;u->phys=0x30000000;u->size=128*1024*1024;return 0;}
int __wrap_ubuf_open(struct ubuf *u,size_t length){
 assert(length<=sizeof m.memory);u->map=m.memory;u->mapped=length;
 uint64_t off;assert(!layout(u->phys,u->size,sysconf(_SC_PAGESIZE),4,15,32,&off));m.off=off;return 0;
}
void __wrap_ubuf_close(struct ubuf *u){(void)u;}
int __wrap_ubuf_sync(uint64_t off,uint64_t length,int direction,int cpu){
 assert(length==FRAME);assert(off==0||off==m.off);assert(direction==(off==0?1:0));
 if(cpu){
  assert(!m.active);
  if(isfault("sync")&&m.started==2)return fail("SOFTWARE injected sync failure");
  if(off==0)m.in_device=0;else m.out_device=0;
 }else{assert(!m.active);
  if(m.started==1&&isfault("pre-error"))m.r[0x34/4]|=0x4020;
  if(m.started==1&&isfault("not-idle"))m.r[0x34/4]&=~2u;
  if(off==0)m.in_device=1;else m.out_device=1;}
 return 0;
}
int __wrap_gpio_discover(struct gpio *g){g->mapping_verified=1;g->lines=4;return 0;}
int __wrap_gpio_acquire(struct gpio *g,const struct contract *c){(void)g;assert(c->mode==1);return 0;}
void __wrap_gpio_release(struct gpio *g){(void)g;}
int __wrap_fft_configure(struct gpio *g,const struct contract *c,unsigned timeout){
 (void)g;(void)c;(void)timeout;assert(++m.configured==1);return 0;
}
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int fd,void *data,size_t n){
 if(fd==m.input_fd&&m.started==1&&isfault("input-short"))return 0;
 return __real_read(fd,data,n);
}
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *data,size_t n){
 if(n==FRAME)m.output_fd=fd;
 if(fd==m.output_fd&&isfault("partial")&&m.started-1==fault_frame){
  if(!m.partial){m.partial=1;return __real_write(fd,data,n/2);}errno=ENOSPC;return -1;
 }
 if(isfault("stats")&&n>10&&!memcmp(data,"{\"type\":",8)&&strstr(data,"\"frame_index\":1")){errno=ENOSPC;return -1;}
 if(isfault("short-writes")&&n>17)n=17;
 return __real_write(fd,data,n);
}
int __real_fsync(int);
int __wrap_fsync(int fd){
 if(fd==m.output_fd&&isfault("fsync")&&m.started==2){errno=EIO;return -1;}
 return __real_fsync(fd);
}
int main(int argc,char **argv){
 assert(argc==8);puts("SOFTWARE MOCK ONLY: DMA echo, not FPGA FFT");
 fault=getenv("FFTTEST_MOCK_FAULT");
 const char *index=getenv("FFTTEST_MOCK_FAIL_FRAME");if(index)fault_frame=(unsigned)atoi(index);
 struct batch_options o={argv[1],argv[2],argv[3],argv[4],argv[5],(unsigned)atoi(argv[6]),2048,(unsigned)atoi(argv[7]),1};
 struct contract c;int input=-1;int rc=batch_validate(&o,&c,&input);
 if(!rc){m.input_fd=input;rc=batch_execute(&o,&c,input);close(input);}
 if((isfault("pre-error")||isfault("not-idle"))&&m.started==1)assert(m.clear[0]==1&&m.clear[1]==1);
 if(!rc){assert(m.configured==1&&m.resets==1&&m.started==o.frames&&m.completed==o.frames);}
 if(fault&&strcmp(fault,"short-writes")){assert(rc&&m.started<=2&&m.configured<=1);}
 printf("SOFTWARE mock counts: configured=%u resets=%u submitted=%u completed=%u\n",m.configured,m.resets,m.started,m.completed);
 return rc?1:0;
}
