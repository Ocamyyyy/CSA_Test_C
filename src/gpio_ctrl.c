#include "ffttest.h"
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
static int matches(const char *chip,const char *label){
 char p[512],r[4096];snprintf(p,sizeof p,"/sys/bus/gpio/devices/%s",chip);
 if(realpath(p,r)&&strstr(r,"41200000"))return 1;
 snprintf(p,sizeof p,"/sys/class/gpio/%s",chip);
 if(realpath(p,r)&&strstr(r,"41200000"))return 1;
 return label&&strstr(label,"41200000");
}
/* gpio-xilinx v5.4 concatenates channel widths: offsets 0..2, then 3.
 * Confirm live DT widths and driver identity before relying on that mapping. */
static int be_property(const char *node,const char *attr,unsigned *value){
 char path[8192];snprintf(path,sizeof path,"%s/%s",node,attr);
 FILE *f=fopen(path,"rb");if(!f)return -1;
 unsigned char b[5];size_t n=fread(b,1,sizeof b,f);fclose(f);
 if(n!=4)return -1;
 *value=((unsigned)b[0]<<24)|((unsigned)b[1]<<16)|((unsigned)b[2]<<8)|b[3];return 0;
}
int gpio_mapping_at(const char *start,unsigned lines){
 char root[4096],node[8192],driver[8192],resolved[4096];
 if(lines!=4||!realpath(start,root))return -1;
 /* chardev IDs and legacy sysfs bases differ. Follow the canonical chip
  * ancestry to the platform device, rather than assuming a device symlink. */
 for(unsigned depth=0;depth<64;depth++){
  snprintf(driver,sizeof driver,"%s/driver",root);
  if(strstr(root,"41200000")&&realpath(driver,resolved)){
   const char *name=strrchr(resolved,'/');
   if(name&&!strcmp(name+1,"gpio-xilinx")){
    snprintf(node,sizeof node,"%s/of_node",root);
    unsigned w1,w2,dual;
    if(!be_property(node,"xlnx,gpio-width",&w1)&&!be_property(node,"xlnx,gpio2-width",&w2)&&
       !be_property(node,"xlnx,is-dual",&dual)&&w1==3&&w2==1&&dual==1)return 0;
   }
  }
  char *slash=strrchr(root,'/');if(!slash||slash==root)break;*slash=0;
 }
 return -1;
}
static void mapping_check(struct gpio *g,const char *chip){
 char path[512];
 snprintf(path,sizeof path,"/sys/bus/gpio/devices/%s",chip);
 g->mapping_verified=!gpio_mapping_at(path,g->lines);
 if(!g->mapping_verified){snprintf(path,sizeof path,"/sys/class/gpio/%s",chip);g->mapping_verified=!gpio_mapping_at(path,g->lines);}
 logmsg("GPIO mapping: live DT/driver check=%s; expected gpio-xilinx, widths=3+1, offsets mode=0,1 req=2 done=3\n",g->mapping_verified?"VERIFIED":"UNVERIFIED");
}
int gpio_discover(struct gpio *g){
 memset(g,0,sizeof *g);g->out=g->in=-1;glob_t paths={0};unsigned count=0;
 if(!glob("/dev/gpiochip*",0,NULL,&paths))for(size_t i=0;i<paths.gl_pathc;i++){
  int fd=open(paths.gl_pathv[i],O_RDONLY|O_CLOEXEC);if(fd<0)continue;
  struct gpiochip_info ci={0};if(!ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&ci)&&matches(strrchr(paths.gl_pathv[i],'/')+1,ci.label)){
   count++;snprintf(g->chip,sizeof g->chip,"%s",paths.gl_pathv[i]);g->lines=ci.lines;
   logmsg("GPIO chardev=%s name=%s label=%s lines=%u\n",g->chip,ci.name,ci.label,ci.lines);
   for(unsigned n=0;n<ci.lines;n++){struct gpioline_info li={.line_offset=n};if(!ioctl(fd,GPIO_GET_LINEINFO_IOCTL,&li))logmsg("  offset=%u flags=%08x name=%s consumer=%s\n",n,li.flags,li.name,li.consumer);}
  }close(fd);
 }
 globfree(&paths);if(count==1){mapping_check(g,strrchr(g->chip,'/')+1);return 0;}if(count>1)return fail("ambiguous GPIO chips at 41200000");
 if(!glob("/sys/class/gpio/gpiochip*",0,NULL,&paths))for(size_t i=0;i<paths.gl_pathc;i++){
  char p[512],label[256];snprintf(p,sizeof p,"%s/label",paths.gl_pathv[i]);if(read_text(p,label,sizeof label))continue;
  if(!matches(strrchr(paths.gl_pathv[i],'/')+1,label))continue;
  uint64_t b,n;snprintf(p,sizeof p,"%s/base",paths.gl_pathv[i]);if(read_num(p,&b))continue;
  snprintf(p,sizeof p,"%s/ngpio",paths.gl_pathv[i]);if(read_num(p,&n)||b>UINT32_MAX||n>1024)continue;
  count++;g->sysfs=1;g->base=b;g->lines=n;snprintf(g->chip,sizeof g->chip,"%s",paths.gl_pathv[i]);
  logmsg("GPIO sysfs=%s label=%s base=%u lines=%u (chardev unavailable)\n",g->chip,label,g->base,g->lines);
 }
 globfree(&paths);if(count!=1)return fail("cannot uniquely locate accessible GPIO 41200000");
 mapping_check(g,strrchr(g->chip,'/')+1);return 0;
}
static int sysvalue(struct gpio *g,unsigned index,const char *attr,const char *v){
 char p[256];snprintf(p,sizeof p,"/sys/class/gpio/gpio%u/%s",g->base+g->offsets[index],attr);return write_text(p,v);
}
static int setout(struct gpio *g,unsigned mode,unsigned req){
 if(g->set_outputs)return g->set_outputs(g->ctx,mode,req);
 if(g->sysfs){
  if(sysvalue(g,2,"value",req?"1":"0"))return -1;
  if(sysvalue(g,0,"value",mode&1?"1":"0")||sysvalue(g,1,"value",mode&2?"1":"0"))return -1;
  return 0;
 }
 struct gpiohandle_data data={{mode&1,(mode>>1)&1,req}};
 if(ioctl(g->out,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&data))return fail("GPIO set values: %s",strerror(errno));
 return 0;
}
static int getdone(struct gpio *g){
 if(g->read_done)return g->read_done(g->ctx);
 if(g->sysfs){char p[256];uint64_t v;snprintf(p,sizeof p,"/sys/class/gpio/gpio%u/value",g->base+g->offsets[3]);if(read_num(p,&v)||v>1)return -1;return v;}
 struct gpiohandle_data data={0};if(ioctl(g->in,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&data))return -1;return data.values[0];
}
int gpio_acquire(struct gpio *g,const struct contract *c){
 if(g->mapping_verified&&(c->m0!=0||c->m1!=1||c->req!=2||c->done!=3))return fail("contract lines conflict with verified GPIO 3+1 wiring");
 g->mode=c->mode;
 unsigned o[]={c->m0,c->m1,c->req,c->done};for(unsigned i=0;i<4;i++){if(o[i]>=g->lines)return fail("GPIO offset %u >= chip lines %u",o[i],g->lines);g->offsets[i]=o[i];}
 if(!g->sysfs){
  int fd=open(g->chip,O_RDONLY|O_CLOEXEC);if(fd<0)return fail("open GPIO chip: %s",strerror(errno));
  struct gpiohandle_request in={.flags=GPIOHANDLE_REQUEST_INPUT,.lines=1};in.lineoffsets[0]=c->done;strcpy(in.consumer_label,"ffttest-done");
  if(ioctl(fd,GPIO_GET_LINEHANDLE_IOCTL,&in)){close(fd);return fail("request config_done: %s",strerror(errno));}g->in=in.fd;
  struct gpiohandle_request out={.flags=GPIOHANDLE_REQUEST_OUTPUT,.lines=3};strcpy(out.consumer_label,"ffttest-config");
  for(unsigned i=0;i<3;i++)out.lineoffsets[i]=o[i];
  out.default_values[0]=c->mode&1;out.default_values[1]=(c->mode>>1)&1;
  int rc=ioctl(fd,GPIO_GET_LINEHANDLE_IOCTL,&out);close(fd);if(rc)return fail("request config outputs: %s",strerror(errno));g->out=out.fd;return 0;
 }
 /* Request first (low) so mode bits can change only while req is inactive. */
 unsigned order[]={2,0,1,3};for(unsigned n=0;n<4;n++){
  unsigned i=order[n];char b[32];snprintf(b,sizeof b,"%u",g->base+o[i]);
  if(write_text("/sys/class/gpio/export",b))return fail("sysfs export failed; never take over pre-exported GPIO");
  g->exported|=1u<<i;
  uint64_t end=now_ms()+1000;char p[256];snprintf(p,sizeof p,"/sys/class/gpio/gpio%u/direction",g->base+o[i]);
  while(access(p,F_OK)&&now_ms()<end&&!stopped)usleep(1000);
  const char *v=i==3?"in":i==0&&(c->mode&1)?"high":i==1&&(c->mode&2)?"high":"low";
  if(write_text(p,v))return -1;
 }return 0;
}
static uint64_t gpio_now(struct gpio *g){return g->clock_ms?g->clock_ms(g->ctx):now_ms();}
static void gpio_delay(struct gpio *g,unsigned us){if(g->sleep_us)g->sleep_us(g->ctx,us);else usleep(us);}
static int waitdone(struct gpio *g,int want,unsigned timeout){
 uint64_t end=gpio_now(g)+timeout;do{int v=getdone(g);if(v<0)return fail("GPIO config_done read failed");if(v==want)return 0;if(stopped)return fail("configuration interrupted");gpio_delay(g,100);}while(gpio_now(g)<end);
 return fail("FFT config timeout waiting done=%d",want);
}
int fft_configure(struct gpio *g,const struct contract *c,unsigned timeout){
 /* Reconfigurable RTL handshake:
  *   mode stable, req=1 -> observe this request's done=0;
  *   req=0 -> observe this request's new done=1.
  * Keep req asserted while waiting for low.  The setup delay after assertion
  * prevents an already-low first-use baseline from being sampled before RTL
  * has had an opportunity to accept the request. */
 if(stopped)return fail("configuration interrupted before request");
 g->mode=c->mode;
 if(setout(g,c->mode,0))return -1;
 gpio_delay(g,c->setup);
 int initial=getdone(g);
 logmsg("GPIO config baseline: mode_sel=%u req=0 done=%d\n",c->mode,initial);
 if(initial<0)return fail("GPIO config_done read failed");
 if(g->configured&&initial!=1)return fail("previous FFT configuration completion is not high before reconfiguration");
 if(stopped)return fail("configuration interrupted before request assertion");
 if(setout(g,c->mode,1))return -1;
 gpio_delay(g,c->setup);
 logmsg("GPIO config request asserted; waiting done=0 while req remains high\n");
 int low=waitdone(g,0,timeout);
 int lower=setout(g,c->mode,0);
 if(low||lower)return -1;
 logmsg("GPIO config request released after done=0; waiting new done=1\n");
 if(waitdone(g,1,timeout))return -1;
 /* Keep the accepted configuration separated from frame data. */
 gpio_delay(g,c->setup);
 g->configured=1;
 logmsg("FFT configured mode_sel=%u; observed done 0->1 with req high->low\n",c->mode);
 return 0;
}
void gpio_release(struct gpio *g){
 if(g->out>=0){
  struct gpiohandle_data v={{g->mode&1,(g->mode>>1)&1,0}};
  if(ioctl(g->out,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&v))logmsg("WARNING: unable to deassert config_req\n");
  close(g->out);
 }
 if(g->in>=0)close(g->in);
 if(g->sysfs){if(g->exported&4)sysvalue(g,2,"value","0");for(unsigned i=0;i<4;i++)if(g->exported&(1u<<i)){char b[32];snprintf(b,sizeof b,"%u",g->base+g->offsets[i]);write_text("/sys/class/gpio/unexport",b);}}
}
