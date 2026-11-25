#ifndef FFTTEST_FFT_M2_H
#define FFTTEST_FFT_M2_H
#include "m2.h"
struct fft_m2_options {
 const char *input,*coeff,*fft_output,*output,*manifest,*fft_contract,*m2_contract;
 unsigned timeout;
 int fresh_reset,require_deployment;
};
int fft_m2_validate(const struct fft_m2_options *,struct contract *,struct contract *,int *,int *);
int sha256_fd(int,char [65]);
int fft_packet_stage(const char *,unsigned,unsigned,const struct contract *,int);
int fft_chain_stage(const struct fft_m2_options *,const struct contract *,int);
int fft_m2_execute(const struct fft_m2_options *,const struct contract *,const struct contract *,int,int);
#endif
