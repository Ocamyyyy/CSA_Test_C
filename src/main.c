#include "ffttest.h"
#include <getopt.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
extern FILE *logfile;
static void onsignal(int s){(void)s;stopped=1;}
static void help(void){
 puts("FFTTest v1 - 2048 point PS-DDR / DMA / FFT / DMA test\n"
 "  --inspect                 read-only board audit (no GPIO request/MMIO writes)\n"
 "  --run                     explicit single hardware transfer; reviewed contract required\n"
 "  --run-batch               configure once, sequential 2/16/64 frame transfers\n"
 "  --frames N                Batch frame count: 2, 16 or 64\n"
 "  --stats PATH              Batch JSONL statistics, new file required\n"
 "  --validate                offline contract/manifest/input checks, no devices\n"
 "  --input PATH              raw binary (default fft_input.bin), single: 8192 bytes; Batch: frames * 8192\n"
 "  --output PATH             raw result (default fft_actual.bin), must not exist\n"
 "  --manifest PATH           default fft_test_manifest.json\n"
 "  --contract PATH           default hardware_contract.json\n"
 "  --mode 2048               required for run/validate; does not select GPIO code\n"
 "  --confirm-fresh-reset     run/run-batch: operator confirms FFT/controller reset and no prior request\n"
 "  --timeout-ms N            1..600000, default 2000 per wait phase\n"
 "  --log PATH                save log to new file (never overwrite existing)\n"
 "  --help\n"
 "Inspect requires board permissions; run requires exclusive hardware ownership.\n"
 "SHA-256: sha256sum fft_input.bin fft_actual.bin fft_test_manifest.json hardware_contract.json");
}
static int uintarg(const char *s,unsigned *v){
 char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno||!*s||*end||*s=='-'||n>600000)return -1;*v=n;return 0;
}
int main(int argc,char **argv){
 enum { INSPECT=1,RUN,VALIDATE,BATCH };int action=0,fresh_reset=0,frames_set=0;unsigned mode=0,timeout=2000,frames=0;
 const char *stats=NULL;
 const char *input="fft_input.bin",*output="fft_actual.bin",*manifest="fft_test_manifest.json",*contract="hardware_contract.json",*logpath=NULL;
 static const struct option opts[]={
 {"run-batch",0,0,'B'},{"frames",1,0,'N'},{"stats",1,0,'S'},{"inspect",0,0,'i'},{"run",0,0,'r'},{"validate",0,0,'v'},{"help",0,0,'h'},
 {"input",1,0,'I'},{"output",1,0,'O'},{"manifest",1,0,'M'},{"contract",1,0,'C'},
 {"confirm-fresh-reset",0,0,'F'},{"mode",1,0,'m'},{"timeout-ms",1,0,'t'},{"log",1,0,'l'},{0,0,0,0}};
 int opt;while((opt=getopt_long(argc,argv,"",opts,NULL))!=-1){
  switch(opt){
   case 'B':case 'i':case 'r':case 'v':if(action)return fail("choose exactly one action"),2;action=opt=='B'?BATCH:opt=='i'?INSPECT:opt=='r'?RUN:VALIDATE;break;
   case 'h':help();return 0;case 'I':input=optarg;break;case 'O':output=optarg;break;
   case 'F':fresh_reset=1;break;
   case 'N':frames_set=1;if(uintarg(optarg,&frames))return fail("invalid Batch frames"),2;break;
   case 'S':stats=optarg;break;
   case 'M':manifest=optarg;break;case 'C':contract=optarg;break;case 'l':logpath=optarg;break;
   case 'm':if(uintarg(optarg,&mode))return fail("invalid mode"),2;break;
   case 't':if(uintarg(optarg,&timeout)||!timeout)return fail("invalid timeout"),2;break;
   default:return 2;
  }
 }
 if(!action||optind!=argc){help();return 2;}
 if(action!=BATCH&&(frames_set||stats))return fail("--frames/--stats require --run-batch"),2;
 if(logpath){int fd=open(logpath,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);if(fd<0)return fail("new log %s: %s",logpath,strerror(errno)),2;
 logfile=fdopen(fd,"w");if(!logfile){close(fd);return 2;}}
 struct sigaction sa={0};sa.sa_handler=onsignal;sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);
 struct utsname un;if(!uname(&un))logmsg("FFTTest v1 platform=%s %s %s %s\n",un.sysname,un.release,un.machine,un.nodename);
 logmsg("action=%s DMA=0x%llx GPIO=0x41200000 timeout=%u ms per phase\n",action==BATCH?"run-batch":action==INSPECT?"inspect":action==RUN?"run":"validate",(unsigned long long)DMA_BASE,timeout);
 logmsg("input=%s output=%s manifest=%s contract=%s log=%s\n",input,output,manifest,contract,logpath?logpath:"stdout");
 int rc=1,dmapped=0,gused=0,uopened=0,device_owned=0,dma_touched=0,safe=1;
 struct dma d={.fd=-1};struct ubuf u={.fd=-1};struct gpio g={.out=-1,.in=-1};uint64_t off=0;
 if(action==BATCH){
  struct batch_options options={input,output,contract,manifest,stats,frames,mode,timeout,fresh_reset};
  struct contract bc;int input_fd=-1;
  if(batch_validate(&options,&bc,&input_fd))goto cleanup;
#ifndef __aarch64__
  fail("hardware execution requires native AArch64 build/platform");
#else
  rc=batch_execute(&options,&bc,input_fd);
#endif
  close(input_fd);goto cleanup;
 }
 if(action==INSPECT){
  int bad=0;if(ubuf_info(&u))bad=1;if(dma_unbound())bad=1;
  dmapped=1;
  if(dma_open(&d,0))bad=1;else dma_dump(&d);
  if(gpio_discover(&g))bad=1;else if(!g.mapping_verified){fail("GPIO live mapping remains UNVERIFIED");bad=1;}
  struct json cj;struct stat st;
  if(!stat(contract,&st)&&!load_json(contract,&cj))logmsg("GPIO contract status=%s mode0=%s mode1=%s req=%s done=%s (chip-relative; not verified by discovery)\n",jget(&cj,"status"),jget(&cj,"mode0_line"),jget(&cj,"mode1_line"),jget(&cj,"req_line"),jget(&cj,"done_line"));
  else logmsg("GPIO configuration lines UNVERIFIED; provide reviewed contract for mapping\n");
  rc=bad?1:0;goto cleanup;
 }
 struct json cj,mj;struct contract c;unsigned char data[FRAME];
 if(load_json(contract,&cj)||load_json(manifest,&mj)||validate_contract(&cj,&mj,mode,&c)||exact_input(input,data))goto cleanup;
 logmsg("Contract validated id=%s mode_sel=%u NFFT=2048 BTT=%u addr=%u alignment=%u\n",jget(&cj,"contract_id"),c.mode,c.btt,c.addr_bits,c.align);
 logmsg("format=%s %s %s direction=%s scaling=%s order=%s quantization=%s config_word=%s\n",jget(&cj,"byte_order"),jget(&cj,"iq_order"),jget(&cj,"encoding"),jget(&cj,"direction"),jget(&cj,"scaling"),jget(&cj,"output_order"),jget(&cj,"quantization"),jget(&cj,"config_word"));
 if(action==VALIDATE){logmsg("Offline validation PASSED; hardware not accessed\n");rc=0;goto cleanup;}
 if(!fresh_reset){fail("--run requires --confirm-fresh-reset: RTL has sticky done and no software clear/pending status");goto cleanup;}
#ifndef __aarch64__
 fail("hardware execution requires native AArch64 build/platform");goto cleanup;
#endif
 struct stat ost;if(!lstat(output,&ost)||errno!=ENOENT){fail("output must be a new file: %s",output);goto cleanup;}
 if(dma_unbound()||ubuf_info(&u))goto cleanup;
 long pg=sysconf(_SC_PAGESIZE);
 if(pg<=0||layout(u.phys,u.size,pg,c.align,c.btt,c.addr_bits,&off)||
    dma_window_valid(c.window_base,c.window_size,u.phys,off)){fail("DMA physical address/length/alignment/buffer bounds invalid");goto cleanup;}
 uint64_t maplen=(off+FRAME+(uint64_t)pg-1)/(uint64_t)pg*(uint64_t)pg;
 if(maplen>SIZE_MAX||maplen>u.size){fail("mapping size invalid");goto cleanup;}
 logmsg("DMA input phys=0x%llx offset=0 output phys=0x%llx offset=%llu map=%llu\n",(unsigned long long)u.phys,(unsigned long long)(u.phys+off),(unsigned long long)off,(unsigned long long)maplen);
 uopened=1;if(ubuf_open(&u,maplen))goto cleanup;
 if(gpio_discover(&g))goto cleanup;
 if(!g.mapping_verified){fail("GPIO live DT/driver mapping unverified; refusing configuration");goto cleanup;}
 dmapped=1;if(dma_open(&d,1))goto cleanup;
 dma_dump(&d);
 /* Check quiescence before any reset; dma_reset refuses an active channel. */
 for(unsigned ch=0;ch<=0x30;ch+=0x30){unsigned s=dr(&d,ch+4);if((s&8)||!(s&3)){fail("DMA active or SG configured; refusing to touch controller");goto cleanup;}}
 gused=1;if(gpio_acquire(&g,&c))goto cleanup;
 dma_touched=1;if(dma_reset(&d,timeout))goto cleanup;
 if(fft_configure(&g,&c,timeout))goto cleanup;
 ubuf_write_frame(u.map,data);ubuf_zero_frame((char*)u.map+off);
 device_owned=1;
 if(ubuf_sync(0,FRAME,1,0)||ubuf_sync(off,FRAME,0,0))goto cleanup;
 if(stopped){fail("interrupted before DMA");goto cleanup;}
 uint64_t start=now_ms();if(dma_transfer(&d,u.phys,u.phys+off,c.addr_bits,timeout))goto cleanup;
 dma_dump(&d);
 if(ubuf_sync(0,FRAME,1,1)||ubuf_sync(off,FRAME,0,1))goto cleanup;
 device_owned=0;
 ubuf_read_frame(data,(char*)u.map+off);
 int ofd=open(output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);if(ofd<0){fail("create output: %s",strerror(errno));goto cleanup;}
 size_t n=0;int writebad=0;while(n<FRAME){ssize_t x=write(ofd,data+n,FRAME-n);if(x<0&&errno==EINTR)continue;if(x<=0){writebad=1;break;}n+=x;}
 if(fsync(ofd))writebad=1;
 if(close(ofd))writebad=1;
 if(writebad){fail("output write failed; partial file retained: %s",output);goto cleanup;}
 logmsg("Saved %u bytes to %s; transfer+sync+save=%llu ms\n",FRAME,output,(unsigned long long)(now_ms()-start));rc=0;
cleanup:
 if(dmapped&&d.regs){
  if(rc&&dma_touched){logmsg("Failure snapshot BEFORE controller-only cleanup:\n");dma_dump(&d);safe=!dma_abort(&d,timeout);dma_dump(&d);}
  dma_close(&d);
 }else if(dmapped)dma_close(&d);
 if(device_owned&&safe){if(ubuf_sync(0,FRAME,1,1)||ubuf_sync(off,FRAME,0,1))rc=1;}
 if(device_owned&&!safe)logmsg("QUARANTINE udmabuf0: reset failed; other processes must not reuse this memory until DMA is stopped.\n");
 if(gused)gpio_release(&g);
 if(uopened)ubuf_close(&u);
 logmsg("Result=%s exit=%d; SHA-256: use sha256sum on input/output/manifest/contract/log\n",rc?"FAILED":"OK",rc);
 if(logfile)fclose(logfile);
 return rc;
}
