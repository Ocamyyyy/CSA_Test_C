#include "m2.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
static void prop(const char *node,const char *key,unsigned value){
 char path[512];snprintf(path,sizeof path,"%s/%s",node,key);FILE *f=fopen(path,"wb");assert(f);
 unsigned char b[]={value>>24,value>>16,value>>8,value};assert(fwrite(b,1,4,f)==4);assert(!fclose(f));
}
static void mapping(void){
 char temp[]="/tmp/ffttest-m2-gpio-XXXXXX";char *base=mkdtemp(temp);assert(base);
 char platform[256],node[256],driver[256],link[256],chip[256],alias[256];
 snprintf(platform,sizeof platform,"%s/41210000.gpio",base);snprintf(node,sizeof node,"%s/41210000.gpio/of_node",base);
 snprintf(driver,sizeof driver,"%s/gpio-xilinx",base);snprintf(link,sizeof link,"%s/41210000.gpio/driver",base);
 snprintf(chip,sizeof chip,"%s/41210000.gpio/gpiochip123",base);snprintf(alias,sizeof alias,"%s/gpiochip777",base);
 assert(!mkdir(platform,0700)&&!mkdir(node,0700)&&!mkdir(driver,0700)&&!mkdir(chip,0700));
 assert(!symlink(driver,link)&&!symlink(chip,alias));
 prop(node,"xlnx,gpio-width",32);prop(node,"xlnx,gpio2-width",32);prop(node,"xlnx,is-dual",1);
 assert(m2_gpio_mapping_at(alias,64));assert(!m2_gpio_mapping_at(alias,32));
 prop(node,"xlnx,gpio2-width",16);assert(!m2_gpio_mapping_at(alias,64));prop(node,"xlnx,gpio2-width",32);
 assert(!unlink(link));assert(!m2_gpio_mapping_at(alias,64));
 const char *keys[]={"xlnx,gpio-width","xlnx,gpio2-width","xlnx,is-dual"};
 for(unsigned i=0;i<3;i++){char p[512];snprintf(p,sizeof p,"%s/%s",node,keys[i]);assert(!unlink(p));}
 assert(!unlink(alias)&&!rmdir(node)&&!rmdir(driver)&&!rmdir(chip)&&!rmdir(platform)&&!rmdir(base));
}
int main(void){
 mapping();
 struct contract c={.addr_bits=32,.window_base=0,.window_size=1073741824};
 struct ubuf u={.phys=0x30000000,.size=128*1024*1024};uint64_t off;size_t length;
 const unsigned bytes[]={4,28,8192,16384};const unsigned pages[]={4096,65536};
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<2;j++){
  assert(!m2_buffer_layout(&c,&u,pages[j],bytes[i],&off,&length));
  assert(off>=bytes[i]+pages[j]&&!(off%pages[j])&&length>=off+bytes[i]&&length<=u.size);
 }
 assert(m2_buffer_layout(&c,&u,4096,0,&off,&length));
 assert(m2_buffer_layout(&c,&u,4096,16388,&off,&length));
 assert(m2_buffer_layout(&c,&u,4096,7,&off,&length));
 assert(m2_buffer_layout(&c,&u,0,4,&off,&length));
 assert(m2_buffer_layout(&c,&u,4000,4,&off,&length));
 u.phys++;assert(m2_buffer_layout(&c,&u,4096,4,&off,&length));u.phys--;
 u.size=8192;assert(m2_buffer_layout(&c,&u,4096,4,&off,&length));u.size=128*1024*1024;
 u.phys=0x40000000;assert(m2_buffer_layout(&c,&u,4096,4,&off,&length));
 u.phys=UINT64_MAX-4095;assert(m2_buffer_layout(&c,&u,4096,4,&off,&length));
 u.phys=0x30000000;c.window_base=UINT64_MAX-7;c.window_size=100;
 assert(m2_buffer_layout(&c,&u,4096,4,&off,&length));
 puts("M2 variable-length/page/alignment/window/overflow layout tests PASS");return 0;
}
