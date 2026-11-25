#ifndef FFTTEST_FFT_MODES_H
#define FFTTEST_FFT_MODES_H
#include "fft_m2.h"
struct fft_mode_spec {
 const char *stage,*direction,*config_word,*scale_schedule,*operation;
 unsigned samples,bytes,mode_sel,shift;
 double scale;
};
struct fft_mode_options {
 const char *stage,*input,*output,*manifest,*contract;
 unsigned timeout;
 int fresh_reset,require_deployment;
};
struct fft_sequence_entry {
 const char *stage;
 char input[512],manifest[512],output[512];
 const struct fft_mode_spec *spec;
 struct contract contract;
 int input_fd;
};
struct fft_sequence {
 struct fft_sequence_entry entries[5];
 unsigned count;
};
struct fft_sequence_options {
 const char *plan,*output_dir,*contract;
 unsigned timeout;
 int require_deployment;
};
const struct fft_mode_spec *fft_mode_find(const char *);
int fft_mode_validate(const struct fft_mode_options *,const struct fft_mode_spec **,struct contract *,int *);
int fft_mode_execute(const struct fft_mode_options *,const struct fft_mode_spec *,const struct contract *,int);
int fft_sequence_validate(const struct fft_sequence_options *,struct fft_sequence *);
void fft_sequence_close(struct fft_sequence *);
int fft_sequence_execute(const struct fft_sequence_options *,struct fft_sequence *);
#endif
