/* Link main with test-only providers: prove inspect never requests writes,
 * and offline validation never calls any hardware provider. */
#include "ffttest.h"
#include "m2.h"
#include "fft_m2.h"
#include "fft_modes.h"
#include <assert.h>
#include <string.h>
int ffttest_entry(int,char **);
static unsigned calls;
int dma_unbound(void){calls++;return 0;}
int dma_open(struct dma *d,int writable){static uint32_t regs[64];calls++;assert(!writable);d->regs=regs;d->fd=-1;return 0;}
void dma_close(struct dma *d){(void)d;calls++;}
void dma_dump(struct dma *d){(void)d;calls++;}
uint32_t dr(struct dma *d,unsigned o){(void)d;(void)o;calls++;return 1;}
int dma_reset(struct dma *d,unsigned t){(void)d;(void)t;assert(0);return -1;}
int dma_transfer(struct dma *d,uint64_t a,uint64_t b,unsigned w,unsigned t){(void)d;(void)a;(void)b;(void)w;(void)t;assert(0);return -1;}
int dma_batch_transfer(struct dma *d,uint64_t a,uint64_t b,unsigned w,unsigned t,int first){(void)d;(void)a;(void)b;(void)w;(void)t;(void)first;assert(0);return -1;}
int dma_abort(struct dma *d,unsigned t){(void)d;(void)t;assert(0);return -1;}
int gpio_discover(struct gpio *g){calls++;g->mapping_verified=1;return 0;}
int gpio_acquire(struct gpio *g,const struct contract *c){(void)g;(void)c;assert(0);return -1;}
int fft_configure(struct gpio *g,const struct contract *c,unsigned t){(void)g;(void)c;(void)t;assert(0);return -1;}
void gpio_release(struct gpio *g){(void)g;assert(0);}
int ubuf_info(struct ubuf *u){calls++;u->fd=-1;return 0;}
int ubuf_open(struct ubuf *u,size_t n){(void)u;(void)n;assert(0);return -1;}
int ubuf_sync(uint64_t o,uint64_t n,int d,int c){(void)o;(void)n;(void)d;(void)c;assert(0);return -1;}
void ubuf_close(struct ubuf *u){(void)u;assert(0);}
void ubuf_write_frame(void *d,const unsigned char *s){(void)d;(void)s;assert(0);}
void ubuf_zero_frame(void *d){(void)d;assert(0);}
void ubuf_read_frame(unsigned char *d,const void *s){(void)d;(void)s;assert(0);}
int m2_validate(const struct m2_options *o,struct contract *c,int *i,int *k){(void)o;(void)c;(void)i;(void)k;assert(0);return -1;}
int m2_execute(const struct m2_options *o,const struct contract *c,int i,int k){(void)o;(void)c;(void)i;(void)k;assert(0);return -1;}
int m2_inspect(void){assert(0);return -1;}
int fft_m2_validate(const struct fft_m2_options *o,struct contract *a,struct contract *b,int *i,int *k){(void)o;(void)a;(void)b;(void)i;(void)k;assert(0);return -1;}
int fft_m2_execute(const struct fft_m2_options *o,const struct contract *a,const struct contract *b,int i,int k){(void)o;(void)a;(void)b;(void)i;(void)k;assert(0);return -1;}
int fft_mode_validate(const struct fft_mode_options *o,const struct fft_mode_spec **s,struct contract *c,int *i){(void)o;(void)s;(void)c;(void)i;assert(0);return -1;}
int fft_mode_execute(const struct fft_mode_options *o,const struct fft_mode_spec *s,const struct contract *c,int i){(void)o;(void)s;(void)c;(void)i;assert(0);return -1;}
int fft_sequence_validate(const struct fft_sequence_options *o,struct fft_sequence *s){(void)o;(void)s;assert(0);return -1;}
void fft_sequence_close(struct fft_sequence *s){(void)s;assert(0);}
int fft_sequence_execute(const struct fft_sequence_options *o,struct fft_sequence *s){(void)o;(void)s;assert(0);return -1;}
int main(int argc,char **argv){
 int inspect=0;for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--inspect"))inspect=1;
 int rc=ffttest_entry(argc,argv);
 if(inspect&&rc==0)assert(calls>0);else assert(calls==0);
 return rc;
}
