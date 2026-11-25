/* FFT-to-M2 orchestration MODEL ONLY. No MMIO, GPIO, DMA or FPGA access. */
#include "fft_m2.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fault,*expected_fft_path;static unsigned fft_calls,m2_calls;static unsigned char simulated_fft[FRAME];
static int is(const char *s){return fault&&!strcmp(fault,s);}
static int read_exact(int fd,unsigned char *b){
 for(unsigned n=0;n<FRAME;){ssize_t z=pread(fd,b+n,FRAME-n,n);if(z<=0)return -1;n+=(unsigned)z;}return 0;
}
int __wrap_fft_chain_stage(const struct fft_m2_options *o,const struct contract *c,int input){
 (void)c;fft_calls++;unsigned char source[FRAME];assert(!read_exact(input,source));
 for(unsigned i=0;i<FRAME;i++)simulated_fft[i]=(unsigned char)(source[i]^(unsigned char)(0x5a+(i&3)));
 if(is("fft-dma")||is("fft-timeout")||is("fft-status")||is("fft-gpio")||is("fft-switch-restore"))
  return fail("SOFTWARE injected %s",fault);
 int fd=open(o->fft_output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);assert(fd>=0);
 assert(write(fd,simulated_fft,FRAME)==FRAME);assert(!close(fd));return 0;
}
int __wrap_m2_execute(const struct m2_options *o,const struct contract *c,int input,int coeff){
 (void)c;m2_calls++;unsigned char actual[FRAME],coefficient[FRAME];
 assert(!strcmp(o->input,expected_fft_path));assert(o->samples==2048);assert(!read_exact(input,actual));assert(!read_exact(coeff,coefficient));
 assert(!memcmp(actual,simulated_fft,FRAME));
 unsigned nonzero=0;for(unsigned i=0;i<FRAME;i++)nonzero|=coefficient[i];assert(nonzero);
 if(is("m2-dma")||is("m2-timeout")||is("m2-status")||is("m2-gpio")||is("m2-switch-restore"))
  return fail("SOFTWARE injected %s",fault);
 int fd=open(o->output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);assert(fd>=0);
 assert(write(fd,actual,FRAME)==FRAME);assert(!close(fd));return 0;
}
int main(int argc,char **argv){
 assert(argc==5);fault=getenv("FFTTEST_CHAIN_FAULT");expected_fft_path=argv[3];
 puts("SOFTWARE FFT-to-M2 CHAIN MODEL ONLY; NOT A BOARD/DMA/FFT RESULT");
 int input=open(argv[1],O_RDONLY|O_CLOEXEC),coeff=open(argv[2],O_RDONLY|O_CLOEXEC);assert(input>=0&&coeff>=0);
 struct fft_m2_options o={.input=argv[1],.coeff=argv[2],.fft_output=argv[3],.output=argv[4],
  .manifest="model",.fft_contract="model",.m2_contract="model",.timeout=5,.require_deployment=1};
 struct contract fc={0},mc={0};int rc=fft_m2_execute(&o,&fc,&mc,input,coeff);close(input);close(coeff);
 if(fault){assert(rc);if(!strncmp(fault,"fft-",4))assert(fft_calls==1&&!m2_calls);else assert(fft_calls==1&&m2_calls==1);}
 else {assert(!rc&&fft_calls==1&&m2_calls==1);unsigned char saved[FRAME];int fd=open(argv[3],O_RDONLY);assert(fd>=0&&!read_exact(fd,saved));close(fd);assert(!memcmp(saved,simulated_fft,FRAME));}
 printf("FFT-M2 mock: fft_stage=%u m2_stage=%u handoff_bit_exact=%s fault=%s\n",fft_calls,m2_calls,m2_calls?"yes":"not-reached",fault?fault:"none");
 return rc?1:0;
}
