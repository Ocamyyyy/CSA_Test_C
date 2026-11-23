#include "ffttest.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
/* Model the externally observable RTL: done remains high when req is lowered.
 * A previous done high must be rejected, since its next low may last one cycle. */
struct model { int done, req, pending, ready_after, reads, edges, writes, fail_read, fail_set, cancel; uint64_t us; };
static int outputs(void *ctx,unsigned mode,unsigned req){
 struct model *m=ctx;assert(mode==1);m->writes++;
 if(m->fail_set)return -1;
 if(req&&!m->req){m->edges++;m->done=0;m->pending=1;}
 m->req=req;return 0;
}
static int done(void *ctx){
 struct model *m=ctx;if(m->fail_read)return -1;
 if(m->cancel&&m->pending)stopped=1;
 if(m->pending&&++m->reads>=m->ready_after&&m->ready_after>=0){m->pending=0;m->done=1;}
 return m->done;
}
static uint64_t clock_ms(void *ctx){return ((struct model*)ctx)->us/1000;}
static void sleep_us(void *ctx,unsigned us){((struct model*)ctx)->us+=us;}
static struct gpio make_gpio(struct model *m){
 struct gpio g={.out=-1,.in=-1,.set_outputs=outputs,.read_done=done,.clock_ms=clock_ms,.sleep_us=sleep_us,.ctx=m};return g;
}
static void property(const char *dir,const char *name,unsigned value){
 char path[512];snprintf(path,sizeof path,"%s/%s",dir,name);
 unsigned char b[]={value>>24,value>>16,value>>8,value};
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(b,1,4,f)==4);assert(!fclose(f));
}
static void mapping_test(void){
 char temp[]="/tmp/ffttest-gpio-XXXXXX";char *base=mkdtemp(temp);assert(base);
 char platform[256],chip[256],node[256],driver[256],link[256],alias[256];
 snprintf(platform,sizeof platform,"%s/41200000.gpio",base);
 snprintf(chip,sizeof chip,"%s/41200000.gpio/gpiochip7",base);
 snprintf(node,sizeof node,"%s/41200000.gpio/of_node",base);
 snprintf(driver,sizeof driver,"%s/gpio-xilinx",base);
 snprintf(link,sizeof link,"%s/41200000.gpio/driver",base);
 snprintf(alias,sizeof alias,"%s/gpiochip508",base);
 assert(!mkdir(platform,0700)&&!mkdir(chip,0700)&&!mkdir(node,0700)&&!mkdir(driver,0700));
 assert(!symlink(driver,link)&&!symlink(chip,alias));
 property(node,"xlnx,gpio-width",3);property(node,"xlnx,gpio2-width",1);property(node,"xlnx,is-dual",1);
 assert(!gpio_mapping_at(chip,4)&&!gpio_mapping_at(alias,4));
 assert(gpio_mapping_at(alias,32));
 property(node,"xlnx,gpio-width",2);assert(gpio_mapping_at(alias,4));property(node,"xlnx,gpio-width",3);
 assert(!unlink(link));assert(gpio_mapping_at(alias,4));
 char path[512];const char *attrs[]={"xlnx,gpio-width","xlnx,gpio2-width","xlnx,is-dual"};
 for(unsigned i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",node,attrs[i]);assert(!unlink(path));}
 assert(!unlink(alias)&&!rmdir(chip)&&!rmdir(node)&&!rmdir(platform)&&!rmdir(driver)&&!rmdir(base));
}
int main(void){
 mapping_test();
 struct contract c={.mode=1,.setup=1};
 struct model m={.ready_after=1};struct gpio g=make_gpio(&m);
 assert(!fft_configure(&g,&c,5));assert(m.edges==1&&m.done==1&&m.req==0);
 /* Second call cannot mistake previous done=1 for new completion. */
 assert(fft_configure(&g,&c,5));assert(m.edges==1&&m.req==0);
 struct model old={.done=1};g=make_gpio(&old);assert(fft_configure(&g,&c,5));assert(old.edges==0);
 struct model delayed={.ready_after=4};g=make_gpio(&delayed);assert(!fft_configure(&g,&c,10));assert(delayed.edges==1&&delayed.reads==4&&delayed.req==0);
 struct model stuck={.ready_after=-1};g=make_gpio(&stuck);assert(fft_configure(&g,&c,2));assert(stuck.edges==1&&stuck.req==0);
 struct model readerr={.fail_read=1};g=make_gpio(&readerr);assert(fft_configure(&g,&c,2));assert(readerr.edges==0);
 struct model writeerr={.fail_set=1};g=make_gpio(&writeerr);assert(fft_configure(&g,&c,2));assert(writeerr.edges==0);
 struct model cancel={.ready_after=-1,.cancel=1};g=make_gpio(&cancel);assert(fft_configure(&g,&c,2));assert(cancel.req==0);stopped=0;
 struct model pre_cancel={0};g=make_gpio(&pre_cancel);stopped=1;assert(fft_configure(&g,&c,2));assert(pre_cancel.writes==0);stopped=0;
 puts("GPIO tests PASSED: initial-low edge, sticky completion, stale-high rejection, delayed ready, timeout, I/O errors, cancel");
 return 0;
}
