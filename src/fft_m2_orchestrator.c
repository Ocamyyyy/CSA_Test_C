#include "fft_m2.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

int fft_m2_execute(const struct fft_m2_options *o,const struct contract *fc,const struct contract *mc,int input,int coeff){
 if(!o->fresh_reset)return fail("--run-fft-m2 requires --confirm-fresh-reset for the sticky FFT configuration handshake");
 uint64_t start=now_us();if(fft_chain_stage(o,fc,input))return -1;
 int fftout=open(o->fft_output,O_RDONLY|O_CLOEXEC);if(fftout<0)return fail("reopen real FFT output for M2: %s",strerror(errno));
 struct m2_options mo={.input=o->fft_output,.coeff=o->coeff,.output=o->output,.manifest=o->manifest,
  .contract=o->m2_contract,.samples=2048,.timeout=o->timeout,.require_deployment=1};
 logmsg("FFT-M2 handoff: passing saved %s raw bytes directly to M2 (no CPU transform)\n",o->fft_output);
 int rc=m2_execute(&mo,mc,fftout,coeff);close(fftout);
 logmsg("FFT-M2 chain result=%s total_us=%" PRIu64 " (two DMA stages plus software/file overhead, not core-only latency)\n",rc?"FAIL":"OK",now_us()-start);
 return rc;
}
