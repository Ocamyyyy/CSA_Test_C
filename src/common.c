#include "ffttest.h"
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
volatile sig_atomic_t stopped;
FILE *logfile;
void logmsg(const char *fmt, ...) {
 va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); fflush(stdout);
 if(logfile){ va_start(a,fmt); vfprintf(logfile,fmt,a); va_end(a); fflush(logfile); }
}
int fail(const char *fmt, ...) {
 char s[1024]; va_list a; va_start(a,fmt); vsnprintf(s,sizeof s,fmt,a); va_end(a);
 logmsg("ERROR: %s\n",s); return -1;
}
uint64_t now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
uint64_t now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int read_text(const char *p,char *b,size_t n){
 FILE *f=fopen(p,"r"); if(!f)return -1;
 size_t z=fread(b,1,n-1,f); int e=ferror(f); fclose(f); b[z]=0;
 while(z && isspace((unsigned char)b[z-1]))b[--z]=0;
 return e?-1:0;
}
int write_text(const char *p,const char *s){
 FILE *f=fopen(p,"w"); if(!f)return fail("write %s: %s",p,strerror(errno));
 int a=fputs(s,f), b=fclose(f); if(a<0||b)return fail("write %s failed",p); return 0;
}
static int number(const char *s,int base,uint64_t *v){
 char *e; if(!s||!*s||*s=='-'||*s=='+'||isspace((unsigned char)*s))return -1;
 errno=0; unsigned long long x=strtoull(s,&e,base); if(errno||*e)return -1; *v=x; return 0;
}
int read_num(const char *p,uint64_t *v){char b[128];return read_text(p,b,sizeof b)||number(b,0,v)?-1:0;}
/* Intentionally small strict flat-JSON schema: no nested objects, escapes or floating point. */
static void ws(const char **s){while(isspace((unsigned char)**s))++*s;}
static int string(const char **s,char *b,size_t n){
 if(*(*s)++!='"')return -1;
 size_t i=0; while(**s && **s!='"'){unsigned char c=*(*s)++;if(c<32||c=='\\'||i+1>=n)return -1;b[i++]=c;}
 if(**s!='"')return -1;
 ++*s;b[i]=0;return 0;
}
int parse_json(const char *s,struct json *j){
 memset(j,0,sizeof *j);ws(&s);if(*s++!='{')return -1;ws(&s);
 if(*s=='}'){s++;ws(&s);return *s?-1:0;}
 for(;;){
  if(j->n==64)return -1;
  struct pair *p=&j->p[j->n];if(string(&s,p->key,sizeof p->key))return -1;
  for(unsigned i=0;i<j->n;i++)if(!strcmp(p->key,j->p[i].key))return -1;
  ws(&s);if(*s++!=':')return -1;ws(&s);
  if(*s=='"'){p->type=1;if(string(&s,p->value,sizeof p->value))return -1;}
  else {size_t n=0;while(*s && *s!=',' && *s!='}'&&!isspace((unsigned char)*s)){
    if(n+1>=sizeof p->value)return -1;
    p->value[n++]=*s++;}p->value[n]=0;
    uint64_t v;if(!strcmp(p->value,"true")||!strcmp(p->value,"false"))p->type=2;
    else {p->type=3;if(number(p->value,10,&v)||(n>1&&p->value[0]=='0'))return -1;}
  }
  ++j->n;ws(&s);if(*s=='}'){s++;ws(&s);return *s?-1:0;}
  if(*s++!=',')return -1;
  ws(&s);
 }
}
const char *jget(const struct json *j,const char *k){for(unsigned i=0;i<j->n;i++)if(!strcmp(k,j->p[i].key))return j->p[i].value;return "";}
int jnum(const struct json *j,const char *k,uint64_t *v){
 for(unsigned i=0;i<j->n;i++)if(!strcmp(k,j->p[i].key))return j->p[i].type!=3?-1:number(j->p[i].value,10,v);
 return -1;
}
int load_json(const char *p,struct json *j){
 char b[32769]; FILE *f=fopen(p,"rb");if(!f)return fail("open %s: %s",p,strerror(errno));
 size_t n=fread(b,1,sizeof b,f);int e=ferror(f);fclose(f);
 if(e||n>=sizeof b||memchr(b,0,n))return fail("invalid/oversized JSON %s",p);
 b[n]=0;if(parse_json(b,j))return fail("invalid flat JSON %s",p);return 0;
}
static int eq(const struct json *j,const char *k,const char *v){return !strcmp(jget(j,k),v);}
int validate_contract(const struct json *c,const struct json *m,unsigned mode,struct contract *o){
 const char *fixed[][2]={{"schema","ffttest-contract-v1"},{"status","VERIFIED"},{"handshake","rising-edge-sticky-done-initial-low"},
 {"tlast","frame-2048"},{"encoding","signed-int16"},{"sync_semantics","upstream-4.5.3"},
 {"target_gpio_mapping","VERIFIED"},{"deployment_identity","VERIFIED"},{"exclusive","true"},{"dma_address_identity","true"},{"micro_dma","false"}};
 for(unsigned i=0;i<sizeof fixed/sizeof fixed[0];i++)if(!eq(c,fixed[i][0],fixed[i][1]))return fail("contract %s must be %s",fixed[i][0],fixed[i][1]);
 const char *same[]={"contract_id","nfft","bytes_per_sample","byte_order","iq_order","encoding","direction","scaling","quantization","output_order","config_word"};
 if(!eq(m,"schema","ffttest-manifest-v1"))return fail("manifest schema mismatch");
 for(unsigned i=0;i<sizeof same/sizeof same[0];i++){
  const char *v=jget(c,same[i]);if(!*v||strstr(v,"UNVERIFIED")||strcmp(v,jget(m,same[i])))return fail("unverified/mismatching %s",same[i]);
 }
 if(!*jget(c,"evidence")||strstr(jget(c,"evidence"),"UNVERIFIED"))return fail("missing reviewed hardware evidence");
 if(!eq(c,"nfft","2048")||!eq(c,"bytes_per_sample","4")||mode!=2048)return fail("only explicit 2048 x 4 supported");
 if(!(eq(c,"byte_order","little")||eq(c,"byte_order","big"))||!(eq(c,"iq_order","I-low")||eq(c,"iq_order","Q-low"))||
 !(eq(c,"direction","forward")||eq(c,"direction","inverse"))||!(eq(c,"output_order","natural")||eq(c,"output_order","bit-reversed")))return fail("unsupported data/FFT enum");
 struct {const char *k;unsigned *v;unsigned lo,hi;} ns[]={
 {"mode_sel",&o->mode,0,3},{"btt_bits",&o->btt,14,26},{"address_bits",&o->addr_bits,32,64},
 {"alignment",&o->align,4,65536},{"mode0_line",&o->m0,0,1023},{"mode1_line",&o->m1,0,1023},
 {"req_line",&o->req,0,1023},{"done_line",&o->done,0,1023},{"setup_us",&o->setup,1,1000000}};
 for(unsigned i=0;i<sizeof ns/sizeof ns[0];i++){uint64_t v;if(jnum(c,ns[i].k,&v)||v<ns[i].lo||v>ns[i].hi)return fail("invalid contract %s",ns[i].k);*ns[i].v=v;}
 unsigned a[]={o->m0,o->m1,o->req,o->done};for(unsigned i=0;i<4;i++)for(unsigned j=0;j<i;j++)if(a[i]==a[j])return fail("GPIO lines overlap");
 if(jnum(c,"dma_window_base",&o->window_base)||jnum(c,"dma_window_size",&o->window_size)||
    !o->window_size||o->window_base>UINT64_MAX-(o->window_size-1))return fail("invalid DMA address window");
 if(o->align&(o->align-1))return fail("alignment must be power of two");
 return 0;
}
int layout(uint64_t phys,uint64_t size,uint64_t page,unsigned align,unsigned bits,unsigned addr,uint64_t *off){
 if(!page||(page&(page-1))||!align||(align&(align-1))||bits<1||bits>26||addr<32||addr>64)return -1;
 uint64_t unit=page>align?page:align;
 if(unit>UINT64_MAX-FRAME)return -1;
 /* At least one whole page gap between input and output. */
 uint64_t out=((FRAME+unit-1)/unit+1)*unit;
 if(FRAME>((1ULL<<bits)-1)||phys%align||FRAME%align||out>size||FRAME>size-out||out<FRAME||size<FRAME)return -1;
 if(phys>UINT64_MAX-out||phys+out>UINT64_MAX-(FRAME-1))return -1;
 uint64_t last=phys+out+FRAME-1;if(addr<64&&last>=(1ULL<<addr))return -1;
 *off=out;return 0;
}
int dma_window_valid(uint64_t base,uint64_t size,uint64_t phys,uint64_t off){
 if(!size||base>UINT64_MAX-(size-1)||phys<base||phys>UINT64_MAX-off)return -1;
 uint64_t rel=phys-base;
 if(rel>=size||off>size-rel||FRAME>size-rel-off)return -1;
 return 0;
}
int exact_input(const char *p,unsigned char *b){
 FILE *f=fopen(p,"rb");if(!f)return fail("input %s: %s",p,strerror(errno));
 struct stat st;if(fstat(fileno(f),&st)||!S_ISREG(st.st_mode)||st.st_size!=FRAME){fclose(f);return fail("input must be regular file of exactly 8192 bytes");}
 size_t n=fread(b,1,FRAME,f);int extra=fgetc(f),err=ferror(f);fclose(f);
 return n==FRAME&&extra==EOF&&!err?0:fail("input changed/truncated during read");
}
uint32_t pack_iq(int16_t i,int16_t q,int ilow){return ilow?(uint16_t)i|((uint32_t)(uint16_t)q<<16):(uint16_t)q|((uint32_t)(uint16_t)i<<16);}
void unpack_iq(uint32_t w,int ilow,int16_t *i,int16_t *q){*i=(int16_t)(ilow?w:w>>16);*q=(int16_t)(ilow?w>>16:w);}
