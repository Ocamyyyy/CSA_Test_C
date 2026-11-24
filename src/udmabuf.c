#include "ffttest.h"
#include <sys/mman.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#define SYS "/sys/class/u-dma-buf/udmabuf0/"
int ubuf_info(struct ubuf *u){
 memset(u,0,sizeof *u);u->fd=-1;
 if(read_num(SYS "phys_addr",&u->phys)||read_num(SYS "size",&u->size)||read_num(SYS "sync_mode",&u->mode))return fail("cannot read udmabuf phys_addr/size/sync_mode: %s",strerror(errno));
 logmsg("udmabuf0 phys=0x%llx size=%llu sync_mode=%llu\n",(unsigned long long)u->phys,(unsigned long long)u->size,(unsigned long long)u->mode);
 const char *attrs[]={"driver_version","sync_offset","sync_size","sync_direction","sync_owner","sync_for_device","sync_for_cpu","dma_coherent","quirk_mmap_mode"};
 for(unsigned i=0;i<sizeof attrs/sizeof attrs[0];i++){char p[256],v[256];snprintf(p,sizeof p,SYS "%s",attrs[i]);if(read_text(p,v,sizeof v))logmsg("  %s: unavailable\n",attrs[i]);else logmsg("  %s=%s\n",attrs[i],v);}
 return 0;
}
int ubuf_open(struct ubuf *u,size_t len){
 char v[64];if(read_text(SYS "driver_version",v,sizeof v)||strcmp(v,"4.5.3"))return fail("run requires audited u-dma-buf driver_version 4.5.3");
 if(u->mode<1||u->mode>7||(u->mode&3)==0)return fail("unsupported sync_mode");
 if(len>u->size)return fail("udmabuf mapping exceeds size");
 u->fd=open("/dev/udmabuf0",O_RDWR|O_SYNC|O_CLOEXEC);if(u->fd<0)return fail("open udmabuf: %s",strerror(errno));
 if(flock(u->fd,LOCK_EX|LOCK_NB))return fail("udmabuf cooperative lock: %s",strerror(errno));
 uint64_t owner;
 if(read_num(SYS "sync_owner",&owner)||owner!=0)return fail("udmabuf owner is not CPU: inspect previous failure/quarantine before reuse");
 u->map=mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,u->fd,0);if(u->map==MAP_FAILED){u->map=NULL;return fail("mmap udmabuf: %s",strerror(errno));}
 u->mapped=len;return 0;
}
int ubuf_sync(uint64_t off,uint64_t len,int direction,int cpu){
 char b[64];snprintf(b,sizeof b,"%llu",(unsigned long long)off);if(write_text(SYS "sync_offset",b))return -1;
 snprintf(b,sizeof b,"%llu",(unsigned long long)len);if(write_text(SYS "sync_size",b))return -1;
 snprintf(b,sizeof b,"%d",direction);if(write_text(SYS "sync_direction",b))return -1;
 if(write_text(cpu?SYS "sync_for_cpu":SYS "sync_for_device","1"))return -1;
 logmsg("sync %s offset=%llu size=%llu direction=%d\n",cpu?"CPU":"device",(unsigned long long)off,(unsigned long long)len,direction);return 0;
}
void ubuf_close(struct ubuf *u){if(u->map)munmap(u->map,u->mapped);if(u->fd>=0)close(u->fd);}

/* AArch64 sync_mode=1 can map Device memory. Avoid libc vector/unaligned
 * copies on that mapping: all accesses below are aligned volatile 32-bit.
 * memcpy operates only on ordinary local staging memory. */
void ubuf_write_frame(void *dst,const unsigned char *src){
 volatile uint32_t *d=dst;
 for(unsigned i=0;i<FRAME/4;i++){uint32_t word;memcpy(&word,src+4*i,4);d[i]=word;}
}
void ubuf_zero_frame(void *dst){
 volatile uint32_t *d=dst;for(unsigned i=0;i<FRAME/4;i++)d[i]=0;
}
void ubuf_read_frame(unsigned char *dst,const void *src){
 const volatile uint32_t *s=src;
 for(unsigned i=0;i<FRAME/4;i++){uint32_t word=s[i];memcpy(dst+4*i,&word,4);}
}

/* Variable packets for M2, using the same aligned scalar-access policy. */
void ubuf_write_bytes(void *dst,const unsigned char *src,size_t bytes){
 volatile uint32_t *d=dst;
 for(size_t i=0;i<bytes/4;i++){uint32_t word;memcpy(&word,src+i*4,4);d[i]=word;}
}
void ubuf_zero_bytes(void *dst,size_t bytes){volatile uint32_t *d=dst;for(size_t i=0;i<bytes/4;i++)d[i]=0;}
void ubuf_read_bytes(unsigned char *dst,const void *src,size_t bytes){
 const volatile uint32_t *s=src;
 for(size_t i=0;i<bytes/4;i++){uint32_t word=s[i];memcpy(dst+i*4,&word,4);}
}
