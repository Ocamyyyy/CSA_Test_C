/* S5/S7/S10 execution orchestration MODEL ONLY; never an FPGA result. */
#include "fft_modes.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fault;static unsigned calls,last_bytes;
static int is(const char *s){return fault&&!strcmp(fault,s);}
int __wrap_fft_packet_stage(const char *output,unsigned bytes,unsigned timeout,const struct contract *c,int input){
 (void)timeout;calls++;last_bytes=bytes;
 if(bytes==16384)assert(c->mode==2||c->mode==3);else {assert(bytes==8192);assert(c->mode==0);}
 if(is("dma-error")||is("timeout")||is("status")||is("gpio")||is("switch-restore")||is("output"))
  return fail("SOFTWARE injected FFT mode %s",fault);
 unsigned char data[16384];for(unsigned n=0;n<bytes;){ssize_t z=pread(input,data+n,bytes-n,n);assert(z>0);n+=(unsigned)z;}
 int fd=open(output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);assert(fd>=0);assert(write(fd,data,bytes)==(ssize_t)bytes);assert(!close(fd));return 0;
}
int main(int argc,char **argv){
 assert(argc==6);fault=getenv("FFTTEST_MODE_FAULT");puts("SOFTWARE FFT MODE MODEL ONLY; NOT A BOARD/DMA/XFFT RESULT");
 struct fft_mode_options o={.stage=argv[1],.input=argv[2],.output=argv[3],.manifest=argv[4],.contract=argv[5],
  .timeout=5,.require_deployment=1};
 const struct fft_mode_spec *s=NULL;struct contract c;int input=-1;int rc=fft_mode_validate(&o,&s,&c,&input);
 if(!rc)rc=fft_mode_execute(&o,s,&c,input);
 if(input>=0)close(input);
 if(fault)assert(rc&&calls==1);else assert(!rc&&calls==1&&last_bytes==s->bytes);
 printf("FFT mode mock: stage=%s calls=%u bytes=%u fault=%s\n",argv[1],calls,last_bytes,fault?fault:"none");return rc?1:0;
}
