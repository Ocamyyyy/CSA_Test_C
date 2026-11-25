#include "fft_modes.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *stages[5]={"S2","S5","S7","S10","S2"};
static const char *outputs[5]={"S2-first-actual.bin","S5-actual.bin","S7-actual.bin","S10-actual.bin","S2-return-actual.bin"};

void fft_sequence_close(struct fft_sequence *s){
 for(unsigned i=0;i<s->count;i++)if(s->entries[i].input_fd>=0){close(s->entries[i].input_fd);s->entries[i].input_fd=-1;}
}
static int join(char *dst,size_t n,const char *dir,const char *name){
 const char *sep=dir[0]&&dir[strlen(dir)-1]!='/'?"/":"";
 int z=snprintf(dst,n,"%s%s%s",dir,sep,name);
 return z<0||(size_t)z>=n?fail("FFT sequence path too long"):0;
}
int fft_sequence_validate(const struct fft_sequence_options *o,struct fft_sequence *s){
 static const char *in_keys[]={"s2_input","s5_input","s7_input","s10_input"};
 static const char *mf_keys[]={"s2_manifest","s5_manifest","s7_manifest","s10_manifest"};
 struct json p;memset(s,0,sizeof *s);for(unsigned i=0;i<5;i++)s->entries[i].input_fd=-1;
 if(!o->plan||!o->output_dir||!*o->output_dir||!o->contract||load_json(o->plan,&p))return -1;
 if(strcmp(jget(&p,"schema"),"ffttest-mode-sequence-v1")||
    strcmp(jget(&p,"status"),"REFERENCE_READY_BOARD_NOT_RUN")||
    strcmp(jget(&p,"order"),"S2,S5,S7,S10,S2"))return fail("FFT sequence plan schema/status/order mismatch");
 struct stat d;if(o->require_deployment&&(stat(o->output_dir,&d)||!S_ISDIR(d.st_mode)))return fail("FFT sequence output directory must already exist");
 for(unsigned i=0;i<5;i++){
  unsigned k=i==0||i==4?0:i;
  const char *input=jget(&p,in_keys[k]),*manifest=jget(&p,mf_keys[k]);
  if(!*input||!*manifest)return fail("FFT sequence plan missing %s paths",stages[i]);
  struct fft_sequence_entry *e=&s->entries[i];e->stage=stages[i];
  if(strlen(input)>=sizeof e->input||strlen(manifest)>=sizeof e->manifest)return fail("FFT sequence input/manifest path too long");
  strcpy(e->input,input);strcpy(e->manifest,manifest);if(join(e->output,sizeof e->output,o->output_dir,outputs[i]))goto fail;
  struct fft_mode_options one={.stage=e->stage,.input=e->input,.output=e->output,.manifest=e->manifest,
   .contract=o->contract,.timeout=o->timeout,.require_deployment=o->require_deployment};
  if(fft_mode_validate(&one,&e->spec,&e->contract,&e->input_fd))goto fail;
  s->count++;
  if(i&&(e->contract.btt!=s->entries[0].contract.btt||e->contract.addr_bits!=s->entries[0].contract.addr_bits||
         e->contract.align!=s->entries[0].contract.align||e->contract.window_base!=s->entries[0].contract.window_base||
         e->contract.window_size!=s->entries[0].contract.window_size)){
   fail("FFT sequence stages disagree on shared DMA/address contract");goto fail;
  }
 }
 logmsg("FFT sequence offline validation PASS: S2->S5->S7->S10->S2, five independent inputs/outputs; hardware not accessed\n");
 return 0;
fail:fft_sequence_close(s);return -1;
}
static int read_packet(int fd,unsigned char *data,unsigned bytes){
 unsigned n=0;while(n<bytes){ssize_t z=pread(fd,data+n,bytes-n,n);if(z<0&&errno==EINTR)continue;if(z<=0)return fail("FFT sequence input changed/short read");n+=(unsigned)z;}
 unsigned char extra;return pread(fd,&extra,1,bytes)==0?0:fail("FFT sequence input length changed");
}
static int write_packet(const char *path,const unsigned char *data,unsigned bytes){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);if(fd<0)return fail("new FFT sequence output %s: %s",path,strerror(errno));
 unsigned n=0;while(n<bytes){ssize_t z=write(fd,data+n,bytes-n);if(z<0&&errno==EINTR)continue;if(z<=0){close(fd);return fail("FFT sequence output write failed");}n+=(unsigned)z;}
 int bad=fsync(fd);if(close(fd))bad=1;return bad?fail("FFT sequence output sync/close failed"):0;
}
static int prior_complete(struct dma *d,unsigned index){
 uint32_t a=dr(d,4),b=dr(d,0x34),ac=dr(d,0),bc=dr(d,0x30);
 if((a|b)&(ERR_MASK|8))return fail("FFT sequence pending DMA error/SG before stage %u: MM2S=%08x S2MM=%08x",index,a,b);
 if(!index){if(!(a&1)||!(b&1)||(ac&1)||(bc&1))return fail("FFT sequence requires halted DMA after its initial controller-only reset");}
 else if((a&(IOC|3))!=(IOC|2)||(b&(IOC|3))!=(IOC|2)||!(ac&1)||!(bc&1))
  return fail("FFT sequence previous frame is not complete/idle before stage %u: MM2S=%08x S2MM=%08x",index,a,b);
 return 0;
}
int fft_sequence_execute(const struct fft_sequence_options *o,struct fft_sequence *s){
 unsigned char data[16384];uint64_t off=0;size_t span=0;unsigned completed=0;
 int rc=1,dmapped=0,uopened=0,gused=0,swopened=0,touched=0,owned=0,safe=1;
 struct dma d={.fd=-1};struct ubuf u={.fd=-1};struct gpio g={.out=-1,.in=-1};
 struct axis_switch sw={.input={.fd=-1},.output={.fd=-1}};const struct contract *base=&s->entries[0].contract;
 if(s->count!=5)return fail("FFT sequence execution requires exactly five validated stages");
 if(dma_unbound()||mmio_no_driver(INPUT_SWITCH_BASE)||mmio_no_driver(OUTPUT_SWITCH_BASE)||ubuf_info(&u))goto cleanup;
 long page=sysconf(_SC_PAGESIZE);if(page<=0||dma_buffer_layout(base,&u,(uint64_t)page,16384,&off,&span))goto cleanup;
 uopened=1;if(ubuf_open(&u,span))goto cleanup;
 if(gpio_discover(&g)||!g.mapping_verified){fail("FFT sequence GPIO live mapping unverified");goto cleanup;}
 dmapped=1;if(dma_open(&d,1))goto cleanup;
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t sr=dr(&d,ch+4);if((sr&8)||!(sr&3)){fail("FFT sequence DMA active/SG, refusing takeover");goto cleanup;}}
 gused=1;if(gpio_acquire(&g,base))goto cleanup;
 touched=1;if(dma_reset(&d,o->timeout))goto cleanup;
 swopened=1;if(axis_switch_open(&sw,d.fd,1)||axis_switch_route_fft(&sw,o->timeout))goto cleanup;
 for(unsigned i=0;i<s->count;i++){
  struct fft_sequence_entry *e=&s->entries[i];unsigned bytes=e->spec->bytes;
  if(prior_complete(&d,i)||read_packet(e->input_fd,data,bytes))goto cleanup;
  logmsg("FFT sequence stage %u/%u %s: configure mode=%u, then one %u-byte frame\n",i+1,s->count,e->stage,e->contract.mode,bytes);
  if(fft_configure(&g,&e->contract,o->timeout))goto cleanup;
  ubuf_write_bytes(u.map,data,bytes);ubuf_zero_bytes((char*)u.map+off,bytes);owned=1;
  if(ubuf_sync(0,bytes,1,0)||ubuf_sync(off,bytes,0,0)||stopped)goto cleanup;
  if(dma_packet(&d,u.phys,u.phys+off,e->contract.addr_bits,bytes,o->timeout,1))goto cleanup;
  if(ubuf_sync(0,bytes,1,1)||ubuf_sync(off,bytes,0,1))goto cleanup;
  owned=0;ubuf_read_bytes(data,(char*)u.map+off,bytes);
  if(write_packet(e->output,data,bytes))goto cleanup;
  completed++;logmsg("FFT sequence stage %s PASS: output=%s bytes=%u\n",e->stage,e->output,bytes);
 }
 rc=0;
cleanup:
 if(dmapped&&d.regs&&rc&&touched){logmsg("FFT sequence failure after %u complete stages; snapshot before bounded DMA cleanup:\n",completed);dma_dump(&d);safe=!dma_abort(&d,o->timeout);dma_dump(&d);}
 if(owned&&safe){unsigned bytes=s->entries[completed<s->count?completed:0].spec->bytes;if(ubuf_sync(0,bytes,1,1)||ubuf_sync(off,bytes,0,1))rc=1;}
 if(owned&&!safe)logmsg("QUARANTINE udmabuf0: sequence DMA stop unproven; no CPU reuse\n");
 if(swopened&&sw.changed){if(safe){if(axis_switch_restore(&sw,o->timeout))rc=1;}else logmsg("ERROR: FFT sequence routes not restored because DMA stop is unproven\n");}
 if(swopened)axis_switch_close(&sw);
 if(gused)gpio_release(&g);
 if(uopened)ubuf_close(&u);
 if(dmapped)dma_close(&d);
 logmsg("FFT sequence summary: requested=5 completed=%u status=%s; no PL/global reset was requested\n",completed,rc?"FAIL":"PASS");return rc;
}
