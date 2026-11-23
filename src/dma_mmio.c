#include "ffttest.h"
#include <sys/mman.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <glob.h>
#include <ftw.h>
#include <stdlib.h>
static inline void barrier(void){
#ifdef __aarch64__
 __asm__ volatile("dsb sy" ::: "memory");
#else
 __sync_synchronize();
#endif
}
uint32_t dr(struct dma *d,unsigned o){barrier();uint32_t v=d->read?d->read(d->ctx,o):d->regs[o/4];barrier();return v;}
void dw(struct dma *d,unsigned o,uint32_t v){barrier();if(d->write)d->write(d->ctx,o,v);else d->regs[o/4]=v;barrier();}
int dma_controller_name(const char *path){
 const char *name=strrchr(path,'/');name=name?name+1:path;
 return !strcmp(name,"dma@40400000")||!strcmp(name,"dma-controller@40400000")||!strcmp(name,"axi_dma@40400000");
}
static int dt_found,dt_disabled;
static int dtvisit(const char *p,const struct stat *st,int type,struct FTW *f){
 (void)st;(void)f;
 if(type==FTW_D && dma_controller_name(p)){
  char name[4096],b[64];snprintf(name,sizeof name,"%s/status",p);dt_found++;
  if(!read_text(name,b,sizeof b)&&!strcmp(b,"disabled"))dt_disabled++;
 }
 return 0;
}
int dma_unbound(void){
 glob_t g={0};int bound=0;
 if(!glob("/sys/bus/platform/devices/*40400000*/driver",0,NULL,&g))for(size_t i=0;i<g.gl_pathc;i++){logmsg("DMA driver binding: %s\n",g.gl_pathv[i]);bound=1;}
 globfree(&g);dt_found=dt_disabled=0;
 if(nftw("/sys/firmware/devicetree/base",dtvisit,16,FTW_PHYS)<0)return fail("cannot audit live device tree: %s",strerror(errno));
 logmsg("DMA DT nodes=%d disabled=%d, driver bound=%d\n",dt_found,dt_disabled,bound);
 if(bound||dt_found!=1||dt_disabled!=1)return fail("DMA ownership not proven (need exactly one disabled DMA DT node and no driver)");
 return 0;
}
int dma_open(struct dma *d,int writable){
 memset(d,0,sizeof *d);d->fd=-1;
 long page=sysconf(_SC_PAGESIZE);if(page<=0)return fail("page size");
 uint64_t base=DMA_BASE&~((uint64_t)page-1);d->len=(size_t)(DMA_BASE-base+0x100+page-1)/(size_t)page*(size_t)page;
 d->fd=open("/dev/mem",(writable?O_RDWR:O_RDONLY)|O_SYNC|O_CLOEXEC);
 if(d->fd<0)return fail("open /dev/mem: %s",strerror(errno));
 if(writable&&flock(d->fd,LOCK_EX|LOCK_NB))return fail("/dev/mem cooperative lock: %s",strerror(errno));
 d->map=mmap(NULL,d->len,PROT_READ|(writable?PROT_WRITE:0),MAP_SHARED,d->fd,(off_t)base);
 if(d->map==MAP_FAILED){d->map=NULL;return fail("DMA mmap: %s",strerror(errno));}
 d->regs=(volatile uint32_t *)((char*)d->map+DMA_BASE-base);return 0;
}
void dma_close(struct dma *d){if(d->map)munmap(d->map,d->len);if(d->fd>=0)close(d->fd);}
void dma_dump(struct dma *d){
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t cr=dr(d,ch),s=dr(d,ch+4);
 logmsg("%s CR=%08x SR=%08x Halted=%u Idle=%u SG=%u IOC=%u DelayIRQ=%u ErrIRQ=%u IntErr=%u SlvErr=%u DecErr=%u SGInt=%u SGSlv=%u SGDec=%u\n",
 ch?"S2MM":"MM2S",cr,s,s&1,(s>>1)&1,(s>>3)&1,(s>>12)&1,(s>>13)&1,(s>>14)&1,(s>>4)&1,(s>>5)&1,(s>>6)&1,(s>>8)&1,(s>>9)&1,(s>>10)&1);
 logmsg("  ADDR=%08x:%08x LENGTH=%u\n",dr(d,ch+0x1c),dr(d,ch+0x18),dr(d,ch+0x28));}
}
static int waitbits(struct dma *d,unsigned reg,uint32_t mask,uint32_t want,unsigned timeout,int cancellable){
 uint64_t end=now_ms()+timeout;
 do{if(cancellable&&stopped)return fail("interrupted");if((dr(d,reg)&mask)==want)return 0;usleep(100); }while(now_ms()<end);
 return fail("timeout reg=0x%x mask=%08x expected=%08x actual=%08x",reg,mask,want,dr(d,reg));
}
int dma_reset(struct dma *d,unsigned timeout){
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t s=dr(d,ch+4);
 if(s&8)return fail("SG enabled: not Simple Mode");
 if(!(s&3))return fail("DMA already active: refusing reset");}
 dw(d,0,4);if(waitbits(d,0,4,0,timeout,1)||waitbits(d,0x30,4,0,timeout,1))return -1;
 dw(d,4,0x7000);dw(d,0x34,0x7000);return 0;
}
int dma_transfer(struct dma *d,uint64_t src,uint64_t dst,unsigned addr_bits,unsigned timeout){
 if(stopped)return fail("DMA interrupted before any transfer write");
 dw(d,0x30,1);if(waitbits(d,0x34,1,0,timeout,1))return -1;
 dw(d,0x48,(uint32_t)dst);if(addr_bits>32)dw(d,0x4c,dst>>32);
 dw(d,0x58,FRAME); /* arm receive before sending */
 if(stopped)return fail("DMA interrupted after arming receive");
 dw(d,0,1);if(waitbits(d,4,1,0,timeout,1))return -1;
 dw(d,0x18,(uint32_t)src);if(addr_bits>32)dw(d,0x1c,src>>32);
 dw(d,0x28,FRAME);
 uint64_t start=now_ms();
 do{
  uint32_t a=dr(d,4),b=dr(d,0x34);
  if((a|b)&ERR_MASK)return fail("DMA status error MM2S=%08x S2MM=%08x",a,b);
  if((a&1)||(b&1))return fail("DMA halted unexpectedly MM2S=%08x S2MM=%08x",a,b);
  if((a&(IOC|2))==(IOC|2)&&(b&(IOC|2))==(IOC|2)){
   uint32_t n=dr(d,0x58);if(n!=FRAME)return fail("S2MM actual length %u != %u",n,FRAME);
   logmsg("DMA completed: %u bytes, elapsed=%llu ms (poll resolution)\n",n,(unsigned long long)(now_ms()-start));return 0;
  }
  if(stopped)return fail("DMA interrupted");
  usleep(100);
 }while(now_ms()-start<timeout);
 return fail("DMA timeout after %u ms; check TLAST/config/clock/reset/TREADY",timeout);
}
int dma_abort(struct dma *d,unsigned timeout){
 /* Bounded controller-only reset. If it fails, memory ownership remains with DMA. */
 dw(d,0,4);
 if(waitbits(d,0,4,0,timeout,0)||waitbits(d,0x30,4,0,timeout,0)||
    waitbits(d,4,1,1,timeout,0)||waitbits(d,0x34,1,1,timeout,0))return fail("DMA reset failed: buffer must remain quarantined; do not reuse until controller is stopped");
 logmsg("DMA controller reset; both channels halted\n");return 0;
}

/* Batch-only preparation. The proven single-frame dma_transfer is unchanged. */
int dma_batch_transfer(struct dma *d,uint64_t src,uint64_t dst,unsigned bits,unsigned timeout,int first){
 uint32_t a=dr(d,4),b=dr(d,0x34),ac=dr(d,0),bc=dr(d,0x30);
 if(stopped)return fail("Batch interrupted before IOC acknowledgement");
 if((a|b)&(ERR_MASK|8))return fail("Batch refuses pending DMA errors/SG: MM2S=%08x S2MM=%08x",a,b);
 if(first){
  /* Immediately after reset, Idle may be zero: no transfer has run yet. */
  if(!(a&1)||!(b&1)||(ac&1)||(bc&1))return fail("Batch first frame requires freshly reset, halted DMA");
 }else if((a&(IOC|3))!=(IOC|2)||(b&(IOC|3))!=(IOC|2)||!(ac&1)||!(bc&1)){
  return fail("Batch previous frame not complete/running: MM2S=%08x S2MM=%08x",a,b);
 }
 /* Clear IOC only: do not erase ErrIRQ or any error evidence. */
 dw(d,4,IOC);dw(d,0x34,IOC);
 a=dr(d,4);b=dr(d,0x34);
 if((a|b)&(IOC|ERR_MASK|8))return fail("Batch IOC clear/readback failed: MM2S=%08x S2MM=%08x",a,b);
 /* dma_transfer writes RS=1 idempotently, never toggles it or resets here. */
 return dma_transfer(d,src,dst,bits,timeout);
}
