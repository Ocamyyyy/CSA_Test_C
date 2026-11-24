#ifndef FFTTEST_H
#define FFTTEST_H
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#define FRAME 8192u
#define DMA_BASE 0x40400000ULL
#define ERR_MASK 0x4770u
#define IOC 0x1000u
/* Compound Batch/M2 metadata is retained in compact form.  The 2 KiB value
 * bound accommodates the audited MATLAB M2 source object while remaining
 * deterministic and well below the 32 KiB document limit. */
struct pair { char key[64], value[2048]; int type; };
struct json { struct pair p[64]; unsigned n; };
struct contract {
 unsigned mode, btt, addr_bits, align, m0, m1, req, done, setup;
 uint64_t window_base, window_size;
};
struct gpio {
 char chip[256]; unsigned base, lines; int out, in, sysfs, mapping_verified;
 unsigned offsets[4]; unsigned exported, mode;
 int (*set_outputs)(void *, unsigned, unsigned);
 int (*read_done)(void *);
 uint64_t (*clock_ms)(void *);
 void (*sleep_us)(void *, unsigned);
 void *ctx;
};
struct dma { volatile uint32_t *regs; void *map; size_t len; int fd;
 uint32_t (*read)(void *, unsigned); void (*write)(void *, unsigned, uint32_t); void *ctx; };
struct ubuf { uint64_t phys, size, mode; int fd; void *map; size_t mapped; };
extern volatile sig_atomic_t stopped;
void logmsg(const char *fmt, ...);
uint64_t now_ms(void);
uint64_t now_us(void);
int fail(const char *fmt, ...);
int read_text(const char *, char *, size_t);
int write_text(const char *, const char *);
int read_num(const char *, uint64_t *);
int parse_json(const char *, struct json *);
const char *jget(const struct json *, const char *);
int jnum(const struct json *, const char *, uint64_t *);
int parse_batch_json(const char *, struct json *);
int load_batch_json(const char *, struct json *);
int batch_json_columns(const struct json *, unsigned);
int batch_json_real(const struct json *, const char *, double *);
int batch_json_object(const struct json *, const char *, struct json *);
int batch_json_file(const struct json *, const char *, uint64_t *, char [65]);
int load_json(const char *, struct json *);
int validate_contract(const struct json *, const struct json *, unsigned, struct contract *);
int layout(uint64_t, uint64_t, uint64_t, unsigned, unsigned, unsigned, uint64_t *);
int dma_window_valid(uint64_t, uint64_t, uint64_t, uint64_t);
int exact_input(const char *, unsigned char *);
uint32_t pack_iq(int16_t, int16_t, int);
void unpack_iq(uint32_t, int, int16_t *, int16_t *);
int dma_controller_name(const char *);
int dma_unbound(void);
int dma_open(struct dma *, int);
void dma_close(struct dma *);
uint32_t dr(struct dma *, unsigned);
void dw(struct dma *, unsigned, uint32_t);
void dma_dump(struct dma *);
int dma_reset(struct dma *, unsigned);
int dma_transfer(struct dma *, uint64_t, uint64_t, unsigned, unsigned);
int dma_batch_transfer(struct dma *, uint64_t, uint64_t, unsigned, unsigned, int);
int dma_abort(struct dma *, unsigned);
int gpio_mapping_at(const char *, unsigned);
int gpio_discover(struct gpio *);
int gpio_acquire(struct gpio *, const struct contract *);
int fft_configure(struct gpio *, const struct contract *, unsigned);
void gpio_release(struct gpio *);
void ubuf_write_frame(void *, const unsigned char *);
void ubuf_zero_frame(void *);
void ubuf_read_frame(unsigned char *, const void *);
int ubuf_info(struct ubuf *);
int ubuf_open(struct ubuf *, size_t);
int ubuf_sync(uint64_t, uint64_t, int, int);
void ubuf_close(struct ubuf *);
struct batch_options {
 const char *input, *output, *contract, *manifest, *stats;
 unsigned frames, mode, timeout;
 int fresh_reset;
};
int normalize_batch_manifest(const struct json *, const struct json *, unsigned, struct json *);
int batch_validate(const struct batch_options *, struct contract *, int *);
int batch_execute(const struct batch_options *, const struct contract *, int);
#endif
