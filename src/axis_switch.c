#include "m2.h"
#include <string.h>
#include <unistd.h>
#define UPDATE 2u
#define DISABLE 0x80000000u
/* PG085 AXI-Lite routing: CTRL=0, MI0=0x40, MI1=0x44.
 * REG_UPDATE soft-resets the switch, never the M2/FFT datapath itself. */
static int commit(struct dma *d,unsigned timeout){
 dw(d,0,UPDATE);uint64_t end=now_ms()+timeout;
 do{if(!(dr(d,0)&UPDATE))return 0;usleep(100);}while(now_ms()<end);
 return fail("AXIS Switch REG_UPDATE did not clear");
}
static int apply(struct axis_switch *s,const uint32_t *values,unsigned timeout){
 if((dr(&s->input,0)|dr(&s->output,0))&UPDATE)return fail("AXIS Switch commit already pending");
 dw(&s->input,0x40,values[0]);dw(&s->input,0x44,values[1]);dw(&s->output,0x40,values[2]);
 if(commit(&s->input,timeout)||commit(&s->output,timeout))return -1;
 if(dr(&s->input,0x40)!=values[0]||dr(&s->input,0x44)!=values[1]||dr(&s->output,0x40)!=values[2])return fail("AXIS Switch route readback mismatch");
 return 0;
}
int axis_switch_open(struct axis_switch *s,int fd,int writable){
 memset(s,0,sizeof *s);s->input.fd=s->output.fd=-1;
 if(mmio_map_shared(&s->input,fd,INPUT_SWITCH_BASE,writable)||mmio_map_shared(&s->output,fd,OUTPUT_SWITCH_BASE,writable))return -1;
 return 0;
}
void axis_switch_close(struct axis_switch *s){dma_close(&s->input);dma_close(&s->output);}
void axis_switch_dump(struct axis_switch *s){
 logmsg("Switch input CR=%08x MI0=%08x MI1=%08x; output CR=%08x MI0=%08x\n",dr(&s->input,0),dr(&s->input,0x40),dr(&s->input,0x44),dr(&s->output,0),dr(&s->output,0x40));
}
int axis_switch_route_m2(struct axis_switch *s,unsigned timeout){
 if(stopped)return fail("Switch route interrupted");
 s->saved[0]=dr(&s->input,0x40);s->saved[1]=dr(&s->input,0x44);s->saved[2]=dr(&s->output,0x40);s->snapshot=1;
 s->changed=1;const uint32_t route[]={DISABLE,0,1};
 if(apply(s,route,timeout))return -1;
 logmsg("M2 Switch route committed/read back: input MI0 disabled, MI1<-SI0; output MI0<-SI1\n");return 0;
}
int axis_switch_route_fft(struct axis_switch *s,unsigned timeout){
 if(stopped)return fail("Switch route interrupted");
 s->saved[0]=dr(&s->input,0x40);s->saved[1]=dr(&s->input,0x44);s->saved[2]=dr(&s->output,0x40);s->snapshot=1;
 s->changed=1;const uint32_t route[]={0,DISABLE,0};
 if(apply(s,route,timeout))return -1;
 logmsg("FFT Switch route committed/read back: input MI0<-SI0, MI1 disabled; output MI0<-SI0\n");return 0;
}
int axis_switch_restore(struct axis_switch *s,unsigned timeout){
 if(!s->snapshot||!s->changed)return 0;
 if(apply(s,s->saved,timeout))return -1;
 s->changed=0;logmsg("Original Switch routes restored/read back\n");return 0;
}
