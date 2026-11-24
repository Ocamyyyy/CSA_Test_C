#include "m2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t in_regs[64],out_regs[64];static unsigned commits;static int corrupt;
static uint32_t rd(void *ctx,unsigned off){
 uint32_t *r=ctx;if(off==0&&r[0]==2)r[0]=0;
 if(corrupt&&ctx==in_regs&&off==0x44)return r[off/4]^1u;
 return r[off/4];
}
static void wr(void *ctx,unsigned off,uint32_t value){uint32_t *r=ctx;r[off/4]=value;if(off==0)commits++;}
int mmio_map_shared(struct dma *d,int fd,uint64_t addr,int writable){(void)d;(void)fd;(void)addr;(void)writable;return -1;}
void dma_close(struct dma *d){(void)d;}
uint32_t dr(struct dma *d,unsigned off){return d->read(d->ctx,off);}
void dw(struct dma *d,unsigned off,uint32_t value){d->write(d->ctx,off,value);}
static struct axis_switch make_switch(void){
 struct axis_switch s={0};s.input.ctx=in_regs;s.input.read=rd;s.input.write=wr;s.output.ctx=out_regs;s.output.read=rd;s.output.write=wr;return s;
}
int main(void){
 in_regs[0x40/4]=7;in_regs[0x44/4]=8;out_regs[0x40/4]=9;struct axis_switch s=make_switch();
 assert(!axis_switch_route_fft(&s,5));assert(in_regs[0x40/4]==0&&in_regs[0x44/4]==0x80000000&&out_regs[0x40/4]==0&&commits==2);
 assert(!axis_switch_restore(&s,5));assert(in_regs[0x40/4]==7&&in_regs[0x44/4]==8&&out_regs[0x40/4]==9&&commits==4);
 s=make_switch();assert(!axis_switch_route_m2(&s,5));assert(in_regs[0x40/4]==0x80000000&&in_regs[0x44/4]==0&&out_regs[0x40/4]==1);
 assert(!axis_switch_restore(&s,5));
 corrupt=1;s=make_switch();assert(axis_switch_route_fft(&s,5));corrupt=0;
 stopped=1;s=make_switch();assert(axis_switch_route_fft(&s,5));stopped=0;
 puts("AXIS Switch tests PASS: FFT route, M2 route, exact readback, restoration, failure/cancel");return 0;
}
