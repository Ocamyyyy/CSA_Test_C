#include "m2.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

static int equal(const struct json *j,const char *k,const char *v){return !strcmp(jget(j,k),v);}
static int integer(const struct json *j,const char *k,uint64_t expected){uint64_t v;return !jnum(j,k,&v)&&v==expected;}
static const struct pair *field(const struct json *j,const char *k){
 for(unsigned i=0;i<j->n;i++)if(!strcmp(j->p[i].key,k))return &j->p[i];
 return NULL;
}
static int boolean_true(const struct json *j,const char *k){
 const struct pair *p=field(j,k);return p&&p->type==2&&!strcmp(p->value,"true");
}
static int matlab_manifest(const struct json *m,const struct json *hw,unsigned samples){
 struct json format,quant,validation;uint64_t bytes=(uint64_t)samples*4,z;char sha[65];double scale;
 if(!equal(m,"schema","m2-phase-axis-test-v1"))return fail("M2 manifest schema mismatch");
 if(!*jget(m,"test_id"))return fail("MATLAB M2 manifest test_id is required");
 if(!integer(m,"sample_count",samples)||!integer(m,"maximum_supported_samples",4096)||
    !integer(m,"vector_len_coefficient",samples)||!integer(m,"vector_len_data",samples))
  return fail("MATLAB M2 manifest sample/vector length mismatch");
 if(!equal(m,"protocol_order","load N coefficients, then stream N data samples"))
  return fail("MATLAB M2 protocol order mismatch");
 if(strcmp(jget(m,"arithmetic"),"four signed 16x16 multiplies; signed 33-bit accumulators; divide by 2^15; round-to-nearest ties-to-even; signed int16 saturation")||
    strcmp(jget(hw,"arithmetic"),"complex-mul-s33-rshift15-rne-sat16"))
  return fail("MATLAB M2 arithmetic differs from hardware contract");
 if(batch_json_object(m,"format",&format)||!integer(&format,"word_bits",32)||
    !equal(&format,"byte_order","little-endian")||
    !equal(&format,"i_field","[15:0] signed int16")||!equal(&format,"q_field","[31:16] signed int16"))
  return fail("MATLAB M2 packed-word format mismatch");
 if(batch_json_object(m,"quantization",&quant)||
    !equal(&quant,"data","signed int16, round-to-nearest ties-to-even, saturate")||
    !equal(&quant,"coefficient","signed Q1.15, round-to-nearest ties-to-even, saturate")||
    !equal(&quant,"positive_one","32767/32768 (raw 32767)")||
    batch_json_real(&quant,"data_scale",&scale)||scale<=0.0)
  return fail("MATLAB M2 quantization metadata mismatch");
 if(batch_json_object(m,"validation",&validation)||
    !boolean_true(&validation,"binary_readback_bit_exact")||
    !boolean_true(&validation,"independent_integer_reference_bit_exact")||
    !boolean_true(&validation,"file_lengths_ok"))
  return fail("MATLAB M2 validation flags must be boolean true");
 const char *names[]={"m2_input.bin","m2_coeff.bin","m2_expected.bin"};
 for(unsigned i=0;i<sizeof names/sizeof names[0];i++)
  if(batch_json_file(m,names[i],&z,sha)||z!=bytes)
   return fail("MATLAB M2 files metadata for %s must be %" PRIu64 " bytes with SHA-256",names[i],bytes);
 logmsg("M2 MATLAB dataset=%s schema=m2-phase-axis-test-v1; protocol bound to contract=%s (dataset identity is not a hardware contract ID)\n",
        jget(m,"test_id"),jget(hw,"contract_id"));
 return 0;
}
static int open_packet(const char *path,unsigned bytes){
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0){fail("M2 input %s: %s",path,strerror(errno));return -1;}
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)bytes){close(fd);fail("M2 %s must be a regular file of exactly %u bytes",path,bytes);return -1;}
 return fd;
}
static int validate_hardware(const struct json *hw,unsigned samples,int require_deployment,struct contract *c){
 memset(c,0,sizeof *c);
 if(!samples||samples>4096)return fail("M2 --samples must be 1..4096");
 const char *fixed[][2]={{"schema","ffttest-m2-contract-v1"},{"protocol_status","VERIFIED"},
  {"sync_semantics","upstream-4.5.3"},
  {"operation_protocol","start-edge-load-process-v1"},{"gpio_offset_model","gpio-xilinx-packed-32+32"},
  {"switch_register_protocol","pg085-axi-lite"},{"arithmetic","complex-mul-s33-rshift15-rne-sat16"},
  {"byte_order","little"},{"encoding","signed-int16"},{"iq_order","I-low"},{"coeff_format","Q1.15"},
  {"tlast","last-sample"}};
 for(unsigned i=0;i<sizeof fixed/sizeof fixed[0];i++)if(!equal(hw,fixed[i][0],fixed[i][1]))return fail("M2 contract %s must be %s",fixed[i][0],fixed[i][1]);
 if(!*jget(hw,"contract_id")||!*jget(hw,"evidence")||strstr(jget(hw,"evidence"),"UNVERIFIED"))return fail("M2 requires reviewed contract identity/evidence");
 if(require_deployment){
  const char *deployment[][2]={{"status","VERIFIED"},{"exclusive","true"},{"dma_address_identity","true"},
   {"deployment_identity","VERIFIED"},{"target_gpio_mapping","VERIFIED"}};
  for(unsigned i=0;i<sizeof deployment/sizeof deployment[0];i++)
   if(!equal(hw,deployment[i][0],deployment[i][1]))return fail("M2 contract %s must be %s",deployment[i][0],deployment[i][1]);
 }
 struct {const char *key;uint64_t value;} required[]={
  {"dma_base",DMA_BASE},{"fft_gpio_base",0x41200000},{"m2_gpio_base",M2_GPIO_BASE},
  {"input_switch_base",INPUT_SWITCH_BASE},{"output_switch_base",OUTPUT_SWITCH_BASE},
  {"gpio_width",32},{"gpio2_width",32},{"bytes_per_sample",4},{"max_samples",4096},{"tkeep",15}};
 for(unsigned i=0;i<sizeof required/sizeof required[0];i++)if(!integer(hw,required[i].key,required[i].value))return fail("M2 hardware field %s conflicts with supplied RTL contract",required[i].key);
 unsigned bytes=samples*4;uint64_t bits,addr,align;
 if(jnum(hw,"btt_bits",&bits)||bits<8||bits>26||bytes>((1ULL<<bits)-1)||
    jnum(hw,"address_bits",&addr)||addr<32||addr>64||jnum(hw,"alignment",&align)||align!=4||
    jnum(hw,"dma_window_base",&c->window_base)||jnum(hw,"dma_window_size",&c->window_size)||
    !c->window_size||c->window_base>UINT64_MAX-(c->window_size-1))return fail("M2 invalid BTT/address/alignment/window; packet length=%u",bytes);
 c->btt=bits;c->addr_bits=addr;c->align=align;return 0;
}
int m2_validate_contract(const char *path,unsigned samples,int require_deployment,struct contract *c){
 struct json hw;if(load_json(path,&hw))return -1;
 return validate_hardware(&hw,samples,require_deployment,c);
}
int m2_validate(const struct m2_options *o,struct contract *c,int *input,int *coeff){
 *input=*coeff=-1;
 struct json hw,m;
 if(load_json(o->contract,&hw)||load_batch_json(o->manifest,&m)||
    validate_hardware(&hw,o->samples,o->require_deployment,c))return -1;
 unsigned bytes=o->samples*4;
 if(equal(&m,"schema","ffttest-m2-manifest-v1")){
  const char *same[]={"contract_id","byte_order","encoding","iq_order","coeff_format","arithmetic","tlast"};
  for(unsigned i=0;i<sizeof same/sizeof same[0];i++)if(!equal(&m,same[i],jget(&hw,same[i])))return fail("M2 manifest %s differs from hardware contract",same[i]);
  if(!integer(&m,"samples",o->samples)||!integer(&m,"bytes_per_sample",4)||!integer(&m,"input_bytes",bytes)||
     !integer(&m,"coeff_bytes",bytes)||!integer(&m,"output_bytes",bytes)||!integer(&m,"tkeep",15))return fail("M2 sample/byte/TKEEP metadata mismatch");
 }else if(matlab_manifest(&m,&hw,o->samples))return -1;
 *input=open_packet(o->input,bytes);if(*input<0)return -1;
 *coeff=open_packet(o->coeff,bytes);if(*coeff<0){close(*input);*input=-1;return -1;}
 logmsg("M2 offline validation PASS: contract=%s samples=%u input/coeff/output=%u bytes, metadata only\n",jget(&hw,"contract_id"),o->samples,bytes);
 return 0;
}
int m2_buffer_layout(const struct contract *c,const struct ubuf *u,uint64_t page,unsigned bytes,uint64_t *off,size_t *maplen){
 if(!c->window_size||c->window_base>UINT64_MAX-(c->window_size-1))return fail("invalid M2 DMA window");
 if(!page||(page&(page-1))||page>1048576||!bytes||bytes>16384||(bytes&3)||u->phys%4)return fail("invalid M2 buffer layout inputs");
 uint64_t out=((bytes+page-1)/page+1)*page,span=(out+bytes+page-1)/page*page;
 if(span>u->size||span>SIZE_MAX||out<bytes||u->phys>UINT64_MAX-(out+bytes-1))return fail("M2 buffers overlap/overflow/exceed udmabuf");
 uint64_t last=u->phys+out+bytes-1;
 if(c->addr_bits<32||c->addr_bits>64||(c->addr_bits<64&&last>=(1ULL<<c->addr_bits))||
    u->phys<c->window_base||u->phys-c->window_base>=c->window_size||
    out+bytes>c->window_size-(u->phys-c->window_base))return fail("M2 buffers outside DMA address window");
 *off=out;*maplen=(size_t)span;return 0;
}
static int read_packet(int fd,unsigned char *data,unsigned bytes){
 unsigned n=0;while(n<bytes){
  if(stopped)return fail("M2 interrupted while reading file");
  ssize_t z=pread(fd,data+n,bytes-n,n);if(z<0&&errno==EINTR)continue;
  if(z<=0)return fail("M2 file changed/short read");
  n+=(unsigned)z;
 }
 unsigned char extra;if(pread(fd,&extra,1,bytes)!=0)return fail("M2 file length changed during read");return 0;
}
static int save_packet(int fd,const unsigned char *data,unsigned bytes){
 unsigned n=0;while(n<bytes){if(stopped)return fail("M2 output interrupted");ssize_t z=write(fd,data+n,bytes-n);if(z<0&&errno==EINTR)continue;
  if(z<=0)return fail("M2 output write failed: %s",strerror(errno));
  n+=(unsigned)z;}
 return fsync(fd)?fail("M2 output fsync failed"):0;
}
int m2_execute(const struct m2_options *o,const struct contract *c,int input,int coeff){
 unsigned bytes=o->samples*4;uint64_t start=now_us(),load_us=0,process_us=0,off=0;size_t maplen=0;
 int rc=1,output=-1,dmapped=0,umapped=0,gopened=0,swopened=0,touched=0,txowned=0,rxowned=0,safe=1;
 struct dma d={.fd=-1};struct ubuf u={.fd=-1};struct m2_gpio g={.output=-1,.input=-1};
 struct axis_switch sw={.input={.fd=-1},.output={.fd=-1}};uint32_t status=0;unsigned char data[16384];
 output=open(o->output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);
 if(output<0){fail("new M2 output required: %s",strerror(errno));goto cleanup;}
 if(dma_unbound()||mmio_no_driver(INPUT_SWITCH_BASE)||mmio_no_driver(OUTPUT_SWITCH_BASE)||ubuf_info(&u))goto cleanup;
 long page=sysconf(_SC_PAGESIZE);if(page<=0||m2_buffer_layout(c,&u,(uint64_t)page,bytes,&off,&maplen))goto cleanup;
 logmsg("M2 buffers input=0x%" PRIx64 " output=0x%" PRIx64 " offset=%" PRIu64 " bytes=%u map=%zu\n",u.phys,u.phys+off,off,bytes,maplen);
 umapped=1;if(ubuf_open(&u,maplen))goto cleanup;
 dmapped=1;if(dma_open(&d,1))goto cleanup;
 dma_dump(&d);
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t s=dr(&d,ch+4);if((s&8)||!(s&3)){fail("M2 DMA active/SG, refusing takeover");goto cleanup;}}
 if(m2_gpio_discover(&g))goto cleanup;
 gopened=1;if(m2_gpio_open(&g,0)||m2_gpio_read(&g,&status))goto cleanup;
 m2_status_dump("initial",status);if(status&M2_BUSY){fail("M2 initially busy; no routing/control changes allowed");goto cleanup;}
 if(m2_gpio_open(&g,1))goto cleanup;
 touched=1;if(dma_reset(&d,o->timeout))goto cleanup;
 swopened=1;if(axis_switch_open(&sw,d.fd,1))goto cleanup;
 axis_switch_dump(&sw);if(axis_switch_route_m2(&sw,o->timeout))goto cleanup;
 if(m2_gpio_write(&g,(o->samples<<2)|2)||m2_clear_error(&g,o->timeout))goto cleanup;
 if(read_packet(coeff,data,bytes))goto cleanup;
 ubuf_write_bytes(u.map,data,bytes);txowned=1;if(ubuf_sync(0,bytes,1,0))goto cleanup;
 if(m2_start(&g,o->samples,1,o->timeout))goto cleanup;
 uint64_t t=now_us();int packet_rc=dma_packet(&d,u.phys,0,c->addr_bits,bytes,o->timeout,0);load_us=now_us()-t;if(packet_rc)goto cleanup;
 if(m2_wait_done(&g,1,o->timeout,&status))goto cleanup;
 if(ubuf_sync(0,bytes,1,1))goto cleanup;
 txowned=0;
 /* Coefficients are now valid: NO M2 clear/reset or route commit here. */
 if(read_packet(input,data,bytes))goto cleanup;
 ubuf_write_bytes(u.map,data,bytes);ubuf_zero_bytes((char*)u.map+off,bytes);
 txowned=1;if(ubuf_sync(0,bytes,1,0))goto cleanup;
 rxowned=1;if(ubuf_sync(off,bytes,0,0))goto cleanup;
 if(m2_start(&g,o->samples,0,o->timeout))goto cleanup;
 t=now_us();packet_rc=dma_packet(&d,u.phys,u.phys+off,c->addr_bits,bytes,o->timeout,1);process_us=now_us()-t;if(packet_rc)goto cleanup;
 if(m2_wait_done(&g,0,o->timeout,&status))goto cleanup;
 if(ubuf_sync(0,bytes,1,1))goto cleanup;
 txowned=0;
 if(ubuf_sync(off,bytes,0,1))goto cleanup;
 rxowned=0;
 ubuf_read_bytes(data,(char*)u.map+off,bytes);
 if(save_packet(output,data,bytes))goto cleanup;
 logmsg("M2 raw output saved: %u bytes; sat_i_nonzero=%u sat_q_nonzero=%u (saturation is reported, not silently corrected)\n",bytes,(status>>7)&1,(status>>8)&1);
 rc=0;
 cleanup:
 if(dmapped&&d.regs&&rc&&touched){
  logmsg("M2 failure snapshot BEFORE DMA-only cleanup:\n");dma_dump(&d);
  if(gopened&&!m2_gpio_read(&g,&status))m2_status_dump("failure",status);
  safe=!dma_abort(&d,o->timeout);dma_dump(&d);
 }
 if(safe){
  if(txowned&&ubuf_sync(0,bytes,1,1))rc=1;
  if(rxowned&&ubuf_sync(off,bytes,0,1))rc=1;
 }else logmsg("QUARANTINE udmabuf0: DMA stop unproven; no CPU sync or Switch restoration; do not reuse\n");
 if(swopened&&sw.changed){
  if(safe&&gopened&&!m2_gpio_read(&g,&status)&&!(status&(M2_BUSY|M2_ERROR))){
   if(axis_switch_restore(&sw,o->timeout)){fail("M2 routes NOT restored; do not run FFT until routing is verified");rc=1;}
  }else {fail("M2 state not quiescent/known: routes NOT restored; operator must establish safe state before FFT reuse");rc=1;}
 }
 if(swopened)axis_switch_close(&sw);
 if(gopened)m2_gpio_close(&g);
 if(umapped)ubuf_close(&u);
 if(dmapped)dma_close(&d);
 if(output>=0&&close(output)){fail("M2 output close failed");rc=1;}
 logmsg("M2 result=%s load_dma_us=%" PRIu64 " process_dma_us=%" PRIu64 " total_us=%" PRIu64 " (software phases, not core-only latency)\n",rc?"FAIL":"OK",load_us,process_us,now_us()-start);
 return rc;
}
int m2_inspect(void){
 int bad=0,dmapped=0,swopened=0,gopened=0;struct dma d={.fd=-1};struct ubuf u;
 struct axis_switch sw={.input={.fd=-1},.output={.fd=-1}};struct m2_gpio g={.input=-1,.output=-1};uint32_t status;
 if(ubuf_info(&u))bad=1;
 if(dma_unbound())bad=1;
 if(mmio_no_driver(INPUT_SWITCH_BASE)||mmio_no_driver(OUTPUT_SWITCH_BASE))bad=1;
 dmapped=1;if(dma_open(&d,0))bad=1;else{
  dma_dump(&d);swopened=1;if(axis_switch_open(&sw,d.fd,0))bad=1;else axis_switch_dump(&sw);
 }
 if(m2_gpio_discover(&g))bad=1;else{
  gopened=1;if(m2_gpio_open(&g,0)||m2_gpio_read(&g,&status))bad=1;else m2_status_dump("inspect",status);
 }
 if(gopened)m2_gpio_close(&g);
 if(swopened)axis_switch_close(&sw);
 if(dmapped)dma_close(&d);
 logmsg("M2 inspect: no DMA/Switch/control writes; temporary ch2 input line request only\n");return bad;
}
