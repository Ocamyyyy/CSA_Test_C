#include "ffttest.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

int batch_validate(const struct batch_options *o,struct contract *c,int *input_fd){
 struct json cj,mj,single;*input_fd=-1;
 if(o->frames!=2&&o->frames!=16&&o->frames!=64)return fail("Batch --frames must be 2, 16 or 64");
 if(!o->stats||!*o->stats||!o->input||!o->output)return fail("Batch requires input, output and --stats paths");
 if(!strcmp(o->stats,o->output))return fail("Batch output and stats must be distinct paths");
 if(load_json(o->contract,&cj)||load_batch_json(o->manifest,&mj))return -1;
 if(normalize_batch_manifest(&cj,&mj,o->frames,&single))return -1;
 if(validate_contract(&cj,&single,o->mode,c))return -1;
 if(strcmp(jget(&single,"direction"),"forward")||strcmp(jget(&single,"byte_order"),"little")||
    strcmp(jget(&single,"iq_order"),"I-low")||strcmp(jget(&single,"output_order"),"natural")||
    strcmp(jget(&single,"config_word"),"0x0D550B")||c->mode!=1)
  return fail("Batch requires 2048 Forward, little-endian I-low, natural order, mode_sel=1, config_word=0x0D550B");
 int fd=open(o->input,O_RDONLY|O_CLOEXEC);if(fd<0)return fail("Batch input: %s",strerror(errno));
 struct stat st;uint64_t expected=(uint64_t)o->frames*FRAME;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size!=expected){
  close(fd);return fail("Batch input must be a regular file of exactly %" PRIu64 " bytes",expected);
 }
 if(!o->fresh_reset){close(fd);return fail("--run-batch requires --confirm-fresh-reset");}
 *input_fd=fd;logmsg("Batch validated: frames=%u frame_bytes=%u total=%" PRIu64 " contract=%s\n",o->frames,FRAME,expected,jget(&cj,"contract_id"));
 return 0;
}

static int read_frame(int fd,unsigned char *data){
 size_t n=0;
 while(n<FRAME){
  if(stopped)return fail("Batch interrupted while reading input");
  ssize_t z=read(fd,data+n,FRAME-n);
  if(z<0&&errno==EINTR)continue;
  if(z<=0)return fail("Batch input truncated or read failed: %s",z<0?strerror(errno):"unexpected EOF");
  n+=(size_t)z;
 }
 return 0;
}
static int write_all(int fd,const void *data,size_t size){
 const unsigned char *p=data;size_t n=0;
 while(n<size){ssize_t z=write(fd,p+n,size-n);if(z<0&&errno==EINTR)continue;if(z<=0)return -1;n+=(size_t)z;}
 return 0;
}
/* Keep JSONL records whole even when a write is partial. On persistent I/O
 * failure stdout/run-log remains the diagnostic channel. */
static int stats_line(int fd,const char *line){
 off_t start=lseek(fd,0,SEEK_CUR);
 if(start<0||write_all(fd,line,strlen(line))||fsync(fd)){
  int saved=errno;
  if(start>=0){if(ftruncate(fd,start)||lseek(fd,start,SEEK_SET)<0)logmsg("WARNING: partial stats record retained\n");}
  errno=saved;return fail("Batch stats write/fsync failed: %s",strerror(errno));
 }
 return 0;
}
struct frame_result {uint64_t dma_us,frame_us;uint32_t mm2s,s2mm,rx;int rx_valid;const char *phase;};
static int frame_record(int fd,unsigned index,const struct frame_result *r,int pass){
 char line[1024];
 snprintf(line,sizeof line,
  "{\"type\":\"frame\",\"frame_index\":%u,\"input_offset\":%" PRIu64 ",\"output_offset\":%" PRIu64 ","
  "\"rx_bytes\":%u,\"rx_bytes_valid\":%s,\"dma_us\":%" PRIu64 ",\"frame_us\":%" PRIu64 ","
  "\"mm2s_sr\":%u,\"s2mm_sr\":%u,\"status\":\"%s\",\"phase\":\"%s\"}\n",
  index,(uint64_t)index*FRAME,(uint64_t)index*FRAME,r->rx,r->rx_valid?"true":"false",r->dma_us,r->frame_us,
  r->mm2s,r->s2mm,pass?"PASS":"FAIL",r->phase);
 return stats_line(fd,line);
}
static int process_frame(const struct batch_options *o,const struct contract *c,int input,int output,
                         struct dma *d,struct ubuf *u,uint64_t off,unsigned index,int *owned,struct frame_result *r){
 unsigned char data[FRAME];uint64_t start=now_us(),t;int rc=-1;
 r->phase="input";if(read_frame(input,data))goto done;
 r->phase="prepare";ubuf_write_frame(u->map,data);ubuf_zero_frame((char*)u->map+off);
 *owned=1;r->phase="sync_for_device";
 if(ubuf_sync(0,FRAME,1,0)||ubuf_sync(off,FRAME,0,0))goto done;
 r->phase="dma";t=now_us();
 int result=dma_batch_transfer(d,u->phys,u->phys+off,c->addr_bits,o->timeout,index==0);
 r->dma_us=now_us()-t;if(result)goto done;
 r->rx=FRAME;r->rx_valid=1;r->phase="sync_for_cpu";
 if(ubuf_sync(0,FRAME,1,1)||ubuf_sync(off,FRAME,0,1))goto done;
 *owned=0;ubuf_read_frame(data,(char*)u->map+off);
 if(stopped){fail("Batch interrupted before output write");goto done;}
 r->phase="output";
 if(write_all(output,data,FRAME)||fsync(output)){fail("Batch output write/fsync failed: %s",strerror(errno));goto done;}
 r->phase="complete";rc=0;
 done:
 r->frame_us=now_us()-start;r->mm2s=dr(d,4);r->s2mm=dr(d,0x34);
 return rc;
}
static void rollback_output(int fd,unsigned completed){
 off_t size=(off_t)completed*FRAME;
 if(ftruncate(fd,size)||lseek(fd,size,SEEK_SET)<0||fsync(fd))
  logmsg("ERROR: failed to retain an exact complete-frame output prefix; inspect actual file size\n");
}

/* Called only after batch_validate and the CLI's native-AArch64 guard.
 * Tests link this orchestration to explicit software-only hardware providers. */
int batch_execute(const struct batch_options *o,const struct contract *c,int input){
 uint64_t start=now_us(),sum_us=0,off=0;unsigned completed=0;
 int rc=1,failed=-1,stats=-1,output=-1,dmapped=0,uopened=0,gused=0,touched=0,owned=0,safe=1;
 const char *phase="open_files";
 struct dma d={.fd=-1};struct ubuf u={.fd=-1};struct gpio g={.out=-1,.in=-1};
 stats=open(o->stats,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);
 if(stats<0){fail("new Batch stats file: %s",strerror(errno));goto cleanup;}
 output=open(o->output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);
 if(output<0){fail("new Batch output file: %s",strerror(errno));goto cleanup;}
 phase="devices";
 if(dma_unbound()||ubuf_info(&u))goto cleanup;
 long page=sysconf(_SC_PAGESIZE);
 if(page<=0||layout(u.phys,u.size,page,c->align,c->btt,c->addr_bits,&off)||
    dma_window_valid(c->window_base,c->window_size,u.phys,off)){
  fail("Batch DMA buffer address/length/window invalid");goto cleanup;
 }
 uint64_t maplen=(off+FRAME+(uint64_t)page-1)/(uint64_t)page*(uint64_t)page;
 if(maplen>SIZE_MAX||maplen>u.size){fail("Batch DMA map size invalid");goto cleanup;}
 logmsg("Batch buffers: input_phys=0x%" PRIx64 " output_phys=0x%" PRIx64 " output_offset=%" PRIu64 "\n",u.phys,u.phys+off,off);
 uopened=1;if(ubuf_open(&u,(size_t)maplen))goto cleanup;
 if(gpio_discover(&g))goto cleanup;
 if(!g.mapping_verified){fail("Batch GPIO live mapping UNVERIFIED");goto cleanup;}
 dmapped=1;if(dma_open(&d,1))goto cleanup;
 dma_dump(&d);
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t s=dr(&d,ch+4);if((s&8)||!(s&3)){fail("Batch DMA active/SG; refusing takeover");goto cleanup;}}
 gused=1;if(gpio_acquire(&g,c))goto cleanup;
 touched=1;phase="dma_reset";if(dma_reset(&d,o->timeout))goto cleanup;
 phase="configure";if(fft_configure(&g,c,o->timeout))goto cleanup;
 for(unsigned i=0;i<o->frames;i++){
  struct frame_result r={0};failed=(int)i;
  logmsg("Batch frame %u/%u start\n",i,o->frames);
  int bad=process_frame(o,c,input,output,&d,&u,off,i,&owned,&r);
  if(!bad&&frame_record(stats,i,&r,1)){bad=1;r.phase="stats";}
  if(bad){
   phase=r.phase;rollback_output(output,completed);
   /* Snapshot precedes cleanup reset. Do not clear evidence or retry. */
   logmsg("Batch failed frame=%u completed=%u phase=%s MM2S=%08x S2MM=%08x\n",i,completed,phase,r.mm2s,r.s2mm);
   (void)frame_record(stats,i,&r,0);goto cleanup;
  }
  completed++;sum_us+=r.frame_us;failed=-1;
  logmsg("Batch frame=%u PASS dma_us=%" PRIu64 " frame_us=%" PRIu64 " rx=%u MM2S=%08x S2MM=%08x\n",i,r.dma_us,r.frame_us,r.rx,r.mm2s,r.s2mm);
 }
 phase="input_tail";
 unsigned char extra;ssize_t n;
 do{n=read(input,&extra,1);}while(n<0&&errno==EINTR&&!stopped);
 if(n!=0||stopped){fail("Batch input changed or interrupted at final EOF check");goto cleanup;}
 phase="complete";rc=0;
 cleanup:
 if(dmapped&&d.regs){
  if(rc&&touched){logmsg("Batch failure snapshot BEFORE cleanup:\n");dma_dump(&d);safe=!dma_abort(&d,o->timeout);dma_dump(&d);}
 }
 if(owned&&safe){if(ubuf_sync(0,FRAME,1,1)||ubuf_sync(off,FRAME,0,1)){rc=1;phase="cleanup_sync";}}
 if(owned&&!safe)logmsg("QUARANTINE udmabuf0: Batch reset failed; do not reuse memory until DMA is stopped\n");
 if(dmapped)dma_close(&d);
 if(gused)gpio_release(&g);
 if(uopened)ubuf_close(&u);
 uint64_t actual=0;
 if(output>=0){
  struct stat st;
  if(fstat(output,&st)||st.st_size<0){fail("Batch output size unavailable");rc=1;phase="output_size";}
  else {actual=(uint64_t)st.st_size;if(!rc&&actual!=(uint64_t)completed*FRAME){fail("Batch final output length mismatch");rc=1;phase="output_size";}}
  int bad=fsync(output);if(close(output))bad=1;
  if(bad){fail("Batch final output sync/close failed");rc=1;phase="output_close";}
 }
 uint64_t total=now_us()-start;
 double average=completed?(double)sum_us/completed:0;
 double throughput=total?(double)completed*FRAME*1000000.0/total:0;
 char failure[32];if(failed<0)strcpy(failure,"null");else snprintf(failure,sizeof failure,"%d",failed);
 if(stats>=0){
  char line[1024];snprintf(line,sizeof line,
   "{\"type\":\"summary\",\"requested_frames\":%u,\"successful_frames\":%u,\"failed_frame_index\":%s,"
   "\"batch_total_us\":%" PRIu64 ",\"average_frame_us\":%.3f,\"output_bytes_per_second\":%.3f,"
   "\"actual_output_bytes\":%" PRIu64 ",\"status\":\"%s\",\"phase\":\"%s\"}\n",
   o->frames,completed,failure,total,average,throughput,actual,rc?"FAIL":"PASS",phase);
  if(stats_line(stats,line)){rc=1;phase="stats_summary";}
  if(close(stats)){fail("Batch stats close failed");rc=1;phase="stats_close";}
 }
 logmsg("Batch summary: requested=%u completed=%u failed_frame=%s status=%s total_us=%" PRIu64 " average_frame_us=%.3f output_bytes_per_second=%.3f actual_output_bytes=%" PRIu64 "\n",
        o->frames,completed,failure,rc?"FAIL":"PASS",total,average,throughput,actual);
 return rc;
}
