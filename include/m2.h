#ifndef FFTTEST_M2_H
#define FFTTEST_M2_H
#include "ffttest.h"
#define M2_GPIO_BASE 0x41210000ULL
#define INPUT_SWITCH_BASE 0x43c00000ULL
#define OUTPUT_SWITCH_BASE 0x43c10000ULL
#define M2_BUSY 1u
#define M2_LOAD_DONE 2u
#define M2_PROCESS_DONE 4u
#define M2_ERROR 8u
#define M2_SAT_I 128u
#define M2_SAT_Q 256u
#define M2_START 1u
#define M2_CLEAR 32768u
struct axis_switch {
 struct dma input,output;
 uint32_t saved[3];int snapshot,changed;
};
struct m2_gpio {
 char chip[256];int output,input,mapping_verified;uint32_t control;
 int (*set)(void *,uint32_t);int (*get)(void *,uint32_t *);
 uint64_t (*clock_ms)(void *);void (*delay_us)(void *,unsigned);void *ctx;
};
struct m2_options {
 const char *input,*coeff,*output,*manifest,*contract;
 unsigned samples,timeout;
 int require_deployment;
};
int mmio_map_shared(struct dma *,int,uint64_t,int);
int mmio_no_driver(uint64_t);
int dma_packet(struct dma *,uint64_t,uint64_t,unsigned,unsigned,unsigned,int);
void ubuf_write_bytes(void *,const unsigned char *,size_t);
void ubuf_zero_bytes(void *,size_t);
void ubuf_read_bytes(unsigned char *,const void *,size_t);
int axis_switch_open(struct axis_switch *,int,int);
void axis_switch_close(struct axis_switch *);
void axis_switch_dump(struct axis_switch *);
int axis_switch_route_m2(struct axis_switch *,unsigned);
int axis_switch_route_fft(struct axis_switch *,unsigned);
int axis_switch_restore(struct axis_switch *,unsigned);
int m2_gpio_mapping_at(const char *,unsigned);
int m2_gpio_discover(struct m2_gpio *);
int m2_gpio_open(struct m2_gpio *,int);
void m2_gpio_close(struct m2_gpio *);
int m2_gpio_read(struct m2_gpio *,uint32_t *);
int m2_gpio_write(struct m2_gpio *,uint32_t);
void m2_status_dump(const char *,uint32_t);
int m2_clear_error(struct m2_gpio *,unsigned);
int m2_start(struct m2_gpio *,unsigned,int,unsigned);
int m2_wait_done(struct m2_gpio *,int,unsigned,uint32_t *);
int m2_validate_contract(const char *,unsigned,int,struct contract *);
int m2_validate(const struct m2_options *,struct contract *,int *,int *);
int dma_buffer_layout(const struct contract *,const struct ubuf *,uint64_t,unsigned,uint64_t *,size_t *);
int m2_buffer_layout(const struct contract *,const struct ubuf *,uint64_t,unsigned,uint64_t *,size_t *);
int m2_execute(const struct m2_options *,const struct contract *,int,int);
int m2_inspect(void);
#endif
