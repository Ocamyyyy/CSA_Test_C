#include "m2.h"
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int property(const char *node,const char *key,unsigned *v){
 char path[8192];snprintf(path,sizeof path,"%s/%s",node,key);
 FILE *f=fopen(path,"rb");if(!f)return -1;
 unsigned char b[5];size_t n=fread(b,1,sizeof b,f);fclose(f);if(n!=4)return -1;
 *v=((unsigned)b[0]<<24)|((unsigned)b[1]<<16)|((unsigned)b[2]<<8)|b[3];return 0;
}
int m2_gpio_mapping_at(const char *path,unsigned lines){
 char root[4096],link[8192],target[4096];
 if(lines!=64||!realpath(path,root))return 0;
 for(unsigned n=0;n<64;n++){
  snprintf(link,sizeof link,"%s/driver",root);
  if(strstr(root,"41210000")&&realpath(link,target)){
   const char *driver=strrchr(target,'/');unsigned a,b,dual;
   snprintf(link,sizeof link,"%s/of_node",root);
   if(driver&&!strcmp(driver+1,"gpio-xilinx")&&!property(link,"xlnx,gpio-width",&a)&&
      !property(link,"xlnx,gpio2-width",&b)&&!property(link,"xlnx,is-dual",&dual)&&a==32&&b==32&&dual==1)return 1;
  }
  char *slash=strrchr(root,'/');if(!slash||slash==root)break;*slash=0;
 }
 return 0;
}
int m2_gpio_discover(struct m2_gpio *g){
 memset(g,0,sizeof *g);g->output=g->input=-1;glob_t files={0};unsigned matches=0;
 if(!glob("/dev/gpiochip*",0,NULL,&files))for(size_t i=0;i<files.gl_pathc;i++){
  int fd=open(files.gl_pathv[i],O_RDONLY|O_CLOEXEC);if(fd<0)continue;
  struct gpiochip_info ci={0};const char *name=strrchr(files.gl_pathv[i],'/')+1;
  char sysfs[512];snprintf(sysfs,sizeof sysfs,"/sys/bus/gpio/devices/%s",name);
  if(!ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&ci)&&m2_gpio_mapping_at(sysfs,ci.lines)){
   matches++;snprintf(g->chip,sizeof g->chip,"%s",files.gl_pathv[i]);g->mapping_verified=ci.lines==64;
   logmsg("M2 GPIO chip=%s label=%s lines=%u mapping=%s (ch1 offsets0..31, ch2 offsets32..63)\n",g->chip,ci.label,ci.lines,g->mapping_verified?"VERIFIED":"UNVERIFIED");
   if(ci.lines==64)for(unsigned line=0;line<64;line++){
    struct gpioline_info info={.line_offset=line};
    if(ioctl(fd,GPIO_GET_LINEINFO_IOCTL,&info)){g->mapping_verified=0;break;}
    logmsg("  M2 offset=%u ch%u[%u] flags=%08x consumer=%s\n",line,line<32?1:2,line%32,info.flags,info.consumer);
   }
  }close(fd);
 }
 globfree(&files);
 return matches==1&&g->mapping_verified?0:fail("M2 requires unique gpio-xilinx 41210000 with live DT 32+32 and 64 valid chardev offsets");
}
int m2_gpio_open(struct m2_gpio *g,int outputs){
 if(!g->mapping_verified)return fail("M2 GPIO mapping not verified");
 int fd=open(g->chip,O_RDONLY|O_CLOEXEC);if(fd<0)return fail("open M2 GPIO: %s",strerror(errno));
 struct gpiohandle_request r;
 if(g->input<0){
  memset(&r,0,sizeof r);r.flags=GPIOHANDLE_REQUEST_INPUT;r.lines=32;
  strcpy(r.consumer_label,"ffttest-m2-status");for(unsigned i=0;i<32;i++)r.lineoffsets[i]=i+32;
  if(ioctl(fd,GPIO_GET_LINEHANDLE_IOCTL,&r)){close(fd);return fail("request M2 status lines: %s",strerror(errno));}g->input=r.fd;
 }
 if(outputs&&g->output<0){
  memset(&r,0,sizeof r);r.flags=GPIOHANDLE_REQUEST_OUTPUT;r.lines=32;strcpy(r.consumer_label,"ffttest-m2-control");
  for(unsigned i=0;i<32;i++)r.lineoffsets[i]=i;
  if(ioctl(fd,GPIO_GET_LINEHANDLE_IOCTL,&r)){close(fd);return fail("request M2 control lines: %s",strerror(errno));}g->output=r.fd;
 }
 close(fd);return 0;
}
static int raw_read(struct m2_gpio *g,uint32_t *word){
 if(g->get)return g->get(g->ctx,word);
 struct gpiohandle_data data={0};
 if(ioctl(g->input,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&data))return fail("M2 status read: %s",strerror(errno));
 *word=0;for(unsigned i=0;i<32;i++)*word|=(uint32_t)(data.values[i]!=0)<<i;return 0;
}
int m2_gpio_read(struct m2_gpio *g,uint32_t *word){
 /* v1 get-values may be implemented as multiple pin reads. Require stable
  * consecutive snapshots so transitional busy/done/code bits are not mixed. */
 uint32_t a,b;if(raw_read(g,&a))return -1;
 for(unsigned i=0;i<16;i++){if(raw_read(g,&b))return -1;if(a==b){*word=b;return 0;}a=b;}
 return fail("M2 status did not stabilize");
}
int m2_gpio_write(struct m2_gpio *g,uint32_t word){
 if(g->set){int rc=g->set(g->ctx,word);if(!rc)g->control=word;return rc;}
 struct gpiohandle_data data={0};for(unsigned i=0;i<32;i++)data.values[i]=(word>>i)&1;
 if(ioctl(g->output,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&data))return fail("M2 control write: %s",strerror(errno));
 memset(&data,0,sizeof data);
 if(ioctl(g->output,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&data))return fail("M2 control readback failed");
 uint32_t actual=0;for(unsigned i=0;i<32;i++)actual|=(uint32_t)(data.values[i]!=0)<<i;
 if(actual!=word)return fail("M2 control readback %08x != %08x; check vendor gpio-xilinx multi-line writes",actual,word);
 g->control=word;return 0;
}
void m2_status_dump(const char *phase,uint32_t s){
 static const char *codes[]={"none","start-while-busy","length-or-coeff-invalid","TLAST","TKEEP","reserved5","reserved6","internal"};
 logmsg("M2 %s status=%08x busy=%u load_done=%u process_done=%u error=%u code=%u(%s) sat_i=%u sat_q=%u\n",phase,s,s&1,(s>>1)&1,(s>>2)&1,(s>>3)&1,(s>>4)&7,codes[(s>>4)&7],(s>>7)&1,(s>>8)&1);
}
static uint64_t tick(struct m2_gpio *g){return g->clock_ms?g->clock_ms(g->ctx):now_ms();}
static void delay(struct m2_gpio *g){if(g->delay_us)g->delay_us(g->ctx,1);else usleep(1);}
static int state_error(uint32_t s){if(s&M2_ERROR){m2_status_dump("ERROR",s);return fail("M2 hardware error");}return 0;}
int m2_clear_error(struct m2_gpio *g,unsigned timeout){
 uint32_t s;if(m2_gpio_read(g,&s))return -1;
 if(s&M2_BUSY)return fail("M2 is busy; refusing clear_error/takeover");
 /* This invalidates coefficients. Called only before initial coefficient load. */
 uint32_t base=g->control&~(M2_START|M2_CLEAR);unsigned n=(base>>2)&0x1fff;
 if(!n||n>4096)return fail("M2 clear requires a valid vector_len already set");
 if(stopped||m2_gpio_write(g,base))return fail("M2 initial clear interrupted/write failed");
 delay(g);if(m2_gpio_write(g,base|M2_CLEAR))return -1;delay(g);if(m2_gpio_write(g,base))return -1;
 uint64_t end=tick(g)+timeout;
 do{if(m2_gpio_read(g,&s))return -1;if(!(s&(M2_ERROR|M2_BUSY)))return 0;if(stopped)break;
  delay(g);}while(tick(g)<end);
 m2_status_dump("clear timeout",s);return fail("M2 initial clear_error timeout");
}
int m2_start(struct m2_gpio *g,unsigned samples,int load,unsigned timeout){
 uint32_t s;if(!samples||samples>4096||stopped)return fail("invalid/interrupted M2 start");
 if(m2_gpio_read(g,&s)||state_error(s))return -1;
 if(s&M2_BUSY)return fail("M2 busy before start; no new request issued");
 uint32_t word=(samples<<2)|(load?2u:0u);
 if(m2_gpio_write(g,word))return -1;
 delay(g);if(stopped)return fail("M2 interrupted before start edge");
 if(m2_gpio_write(g,word|M2_START))return -1;
 delay(g);if(m2_gpio_write(g,word))return -1;
 uint64_t end=tick(g)+timeout;
 do{
  if(m2_gpio_read(g,&s)||state_error(s))return -1;
  /* No DMA data has been submitted yet: old completion must be cleared.
   * Do not require observing a transient busy edge or reuse a sticky high. */
  if(!(s&(M2_LOAD_DONE|M2_PROCESS_DONE))){m2_status_dump(load?"load accepted/done clear":"process accepted/done clear",s);return 0;}
  if(stopped)break;
  delay(g);
 }while(tick(g)<end);
 m2_status_dump("start timeout",s);return fail("M2 start did not clear old sticky completion");
}
int m2_wait_done(struct m2_gpio *g,int load,unsigned timeout,uint32_t *last){
 uint32_t expected=load?M2_LOAD_DONE:M2_PROCESS_DONE;uint64_t end=tick(g)+timeout;
 do{
  if(m2_gpio_read(g,last)||state_error(*last))return -1;
  if(!(*last&M2_BUSY)&&(*last&(M2_LOAD_DONE|M2_PROCESS_DONE))==expected){m2_status_dump(load?"loaded":"processed",*last);return 0;}
  if(stopped)return fail("M2 wait interrupted");
  delay(g);
 }while(tick(g)<end);
 m2_status_dump("completion timeout",*last);return fail("M2 %s completion timeout",load?"load":"process");
}
void m2_gpio_close(struct m2_gpio *g){
 if(g->output>=0){if(m2_gpio_write(g,g->control&~(M2_START|M2_CLEAR)))logmsg("WARNING: M2 could not lower request lines\n");close(g->output);g->output=-1;}
 if(g->input>=0){close(g->input);g->input=-1;}
}
