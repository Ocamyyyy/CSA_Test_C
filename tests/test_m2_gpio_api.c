/* Exercise the actual GPIO-v1 adapter, with only syscalls replaced. */
#include "m2.h"
#include <linux/gpio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
static int input_claimed,output_claimed,requests_in,requests_out,fail_output,readback_fault,sets;
static uint32_t output_word,status_word=0x800001d6;
int __wrap_open(const char *path,int flags,...){assert(!strcmp(path,"/dev/gpiochip-test")&&(flags&O_CLOEXEC));return 10;}
int __wrap_open64(const char *path,int flags,...){return __wrap_open(path,flags);}
int __wrap_close(int fd){
 if(fd==20){assert(input_claimed);input_claimed=0;}else if(fd==21){assert(output_claimed);output_claimed=0;}else assert(fd==10);return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...){
 va_list ap;va_start(ap,op);void *arg=va_arg(ap,void *);va_end(ap);
 if(op==GPIO_GET_LINEHANDLE_IOCTL){
  assert(fd==10);struct gpiohandle_request *r=arg;assert(r->lines==32);
  if(r->flags==GPIOHANDLE_REQUEST_INPUT){
   requests_in++;if(input_claimed){errno=EBUSY;return -1;}
   for(unsigned i=0;i<32;i++)assert(r->lineoffsets[i]==i+32);
   input_claimed=1;r->fd=20;
  }else{
   assert(r->flags==GPIOHANDLE_REQUEST_OUTPUT);requests_out++;
   if(fail_output){errno=EBUSY;return -1;}
   assert(!output_claimed);for(unsigned i=0;i<32;i++){assert(r->lineoffsets[i]==i);assert(!r->default_values[i]);}
   output_claimed=1;output_word=0;r->fd=21;
  }return 0;
 }
 struct gpiohandle_data *d=arg;
 if(op==GPIOHANDLE_SET_LINE_VALUES_IOCTL){
  assert(fd==21&&output_claimed);output_word=0;sets++;
  for(unsigned i=0;i<32;i++)output_word|=(uint32_t)(d->values[i]!=0)<<i;
 }else{
  assert(op==GPIOHANDLE_GET_LINE_VALUES_IOCTL);
  assert((fd==20&&input_claimed)||(fd==21&&output_claimed));
  uint32_t word=fd==20?status_word:output_word^(readback_fault?1:0);
  for(unsigned i=0;i<32;i++)d->values[i]=(word>>i)&1;
 }return 0;
}
static struct m2_gpio gpio(void){struct m2_gpio g={.input=-1,.output=-1,.mapping_verified=1};strcpy(g.chip,"/dev/gpiochip-test");return g;}
int main(void){
 struct m2_gpio g=gpio();uint32_t status;
 assert(!m2_gpio_open(&g,0));assert(g.input==20&&g.output==-1&&requests_in==1&&!requests_out&&!sets);
 assert(!m2_gpio_read(&g,&status)&&status==status_word);
 assert(!m2_gpio_open(&g,1));assert(requests_in==1&&requests_out==1); /* no duplicate input EBUSY */
 assert(!m2_gpio_open(&g,1));assert(requests_in==1&&requests_out==1);
 assert(!m2_gpio_write(&g,0x80004000));assert(output_word==0x80004000); /* bit31 and vector_len=4096 */
 readback_fault=1;assert(m2_gpio_write(&g,0x4001));readback_fault=0;
 m2_gpio_close(&g);assert(!input_claimed&&!output_claimed&&!(output_word&(M2_START|M2_CLEAR)));
 g=gpio();fail_output=1;assert(!m2_gpio_open(&g,0));assert(m2_gpio_open(&g,1));
 assert(g.input==20&&g.output==-1);m2_gpio_close(&g);assert(!input_claimed&&!output_claimed);
 g=gpio();g.mapping_verified=0;assert(m2_gpio_open(&g,1));
 puts("M2 GPIO-v1 API tests PASS: exact 64 offsets, input-handle reuse, output readback, input-only inspect, error cleanup");return 0;
}
