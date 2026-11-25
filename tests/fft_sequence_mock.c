/* Five-mode orchestration SOFTWARE MODEL ONLY; never an FPGA result. */
#include "fft_modes.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *fault;static unsigned configs,packets,resets,aborts,restores,routes,syncs;
static unsigned modes[5],sizes[5];static unsigned char memory[65536];static int started;
static int is(const char *s){return fault&&!strcmp(fault,s);}
int fft_mode_validate(const struct fft_mode_options *o,const struct fft_mode_spec **s,struct contract *c,int *fd){(void)o;(void)s;(void)c;(void)fd;assert(0);return -1;}
int dma_unbound(void){return 0;}int mmio_no_driver(uint64_t a){(void)a;return 0;}
int ubuf_info(struct ubuf *u){u->phys=0x30000000;u->size=sizeof memory;return 0;}
int dma_buffer_layout(const struct contract *c,const struct ubuf *u,uint64_t p,unsigned b,uint64_t *o,size_t *n){(void)c;(void)u;(void)p;(void)b;*o=32768;*n=sizeof memory;return 0;}
int ubuf_open(struct ubuf *u,size_t n){u->map=memory;u->mapped=n;return 0;}void ubuf_close(struct ubuf *u){u->map=NULL;}
void ubuf_write_bytes(void *d,const unsigned char *s,size_t n){memcpy(d,s,n);}void ubuf_zero_bytes(void *d,size_t n){memset(d,0,n);}void ubuf_read_bytes(unsigned char *d,const void *s,size_t n){memcpy(d,s,n);}
int ubuf_sync(uint64_t o,uint64_t n,int dir,int begin){(void)o;(void)n;(void)dir;(void)begin;syncs++;return is("sync")&&syncs==3?fail("SOFTWARE injected sequence sync failure"):0;}
int gpio_discover(struct gpio *g){g->mapping_verified=1;return 0;}int gpio_acquire(struct gpio *g,const struct contract *c){(void)g;(void)c;return 0;}void gpio_release(struct gpio *g){(void)g;}
int fft_configure(struct gpio *g,const struct contract *c,unsigned t){(void)g;(void)t;modes[configs]=c->mode;configs++;return is("configure-2")&&configs==2?fail("SOFTWARE injected sequence GPIO failure"):0;}
int dma_open(struct dma *d,int w){(void)w;d->regs=(volatile uint32_t *)memory;return 0;}void dma_close(struct dma *d){d->regs=NULL;}
int dma_reset(struct dma *d,unsigned t){(void)d;(void)t;resets++;started=0;return 0;}
uint32_t dr(struct dma *d,unsigned o){(void)d;if(is("status")&&packets==1&&(o==4||o==0x34))return ERR_MASK;if(o==4||o==0x34)return started?IOC|2:1;if(o==0||o==0x30)return started?1:0;return 0;}
int dma_packet(struct dma *d,uint64_t s,uint64_t out,unsigned bits,unsigned bytes,unsigned timeout,int receive){
 (void)d;(void)s;(void)out;(void)bits;(void)timeout;assert(receive);sizes[packets]=bytes;packets++;
 if((is("dma-2")||is("timeout")||is("abort"))&&packets==2)return fail("SOFTWARE injected sequence DMA failure");
 memcpy(memory+32768,memory,bytes);started=1;return 0;
}
void dma_dump(struct dma *d){(void)d;}int dma_abort(struct dma *d,unsigned t){(void)d;(void)t;aborts++;return is("abort")?-1:0;}
int axis_switch_open(struct axis_switch *s,int fd,int w){(void)fd;(void)w;s->changed=0;return 0;}
int axis_switch_route_fft(struct axis_switch *s,unsigned t){(void)t;routes++;s->changed=1;return 0;}
int axis_switch_restore(struct axis_switch *s,unsigned t){(void)s;(void)t;restores++;return is("switch-restore")?fail("SOFTWARE injected sequence restore failure"):0;}
void axis_switch_close(struct axis_switch *s){(void)s;}

int main(int argc,char **argv){
 assert(argc==7);fault=getenv("FFTTEST_SEQUENCE_FAULT");puts("SOFTWARE FFT SEQUENCE MODEL ONLY; NOT A BOARD/DMA/XFFT RESULT");
 static const char *stage[]={"S2","S5","S7","S10","S2"};static const unsigned mode[]={1,3,2,0,1},bytes[]={8192,16384,16384,8192,8192};
 struct fft_mode_spec specs[5];struct fft_sequence s={.count=5};struct fft_sequence_options o={.output_dir=argv[6],.timeout=5};
 for(unsigned i=0;i<5;i++){
  specs[i].stage=stage[i];specs[i].bytes=bytes[i];specs[i].mode_sel=mode[i];s.entries[i].stage=stage[i];s.entries[i].spec=&specs[i];
  s.entries[i].contract=(struct contract){.mode=mode[i],.addr_bits=32,.btt=15,.align=4,.window_size=1ULL<<30};
  s.entries[i].input_fd=open(argv[i+1],O_RDONLY|O_CLOEXEC);assert(s.entries[i].input_fd>=0);
  snprintf(s.entries[i].output,sizeof s.entries[i].output,"%s/%s-%u.bin",argv[6],stage[i],i);
 }
 int rc=fft_sequence_execute(&o,&s);fft_sequence_close(&s);
 if(!fault){assert(!rc&&configs==5&&packets==5&&resets==1&&routes==1&&restores==1);for(unsigned i=0;i<5;i++){assert(modes[i]==mode[i]&&sizes[i]==bytes[i]);}}
 else {assert(rc&&resets==1&&routes==1);if(!is("abort"))assert(restores==1);}
 printf("sequence mock: configs=%u packets=%u resets=%u aborts=%u restores=%u status=%s\n",configs,packets,resets,aborts,restores,rc?"FAIL":"PASS");return rc?1:0;
}
