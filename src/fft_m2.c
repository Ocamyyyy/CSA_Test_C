#include "fft_m2.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int eq(const struct json *j,const char *k,const char *v){return !strcmp(jget(j,k),v);}
static int num(const struct json *j,const char *k,uint64_t expected){uint64_t v;return !jnum(j,k,&v)&&v==expected;}
static const struct pair *field(const struct json *j,const char *k){
 for(unsigned i=0;i<j->n;i++)if(!strcmp(j->p[i].key,k))return &j->p[i];
 return NULL;
}
static int boolean(const struct json *j,const char *k,int expected){
 const struct pair *p=field(j,k);return p&&p->type==2&&!strcmp(p->value,expected?"true":"false");
}
static int fft_contract(const char *path,int deployment,struct contract *c){
 struct json j;if(load_json(path,&j))return -1;memset(c,0,sizeof *c);
 const char *fixed[][2]={{"schema","ffttest-contract-v1"},{"static_hardware_status","VERIFIED"},
  {"byte_order","little"},{"iq_order","I-low"},{"encoding","signed-int16"},{"direction","forward"},
  {"scaling","SCALE_SCH=0x6AA; total shift=11; gain=1/2048"},
  {"quantization","XFFT convergent_rounding; scaled signed16; no external conversion; OVFLO disabled"},
  {"output_order","natural"},{"config_word","0x0D550B"},
  {"handshake","request-high-done-low-release-done-high"},{"tlast","frame-2048"},
  {"sync_semantics","upstream-4.5.3"}};
 for(unsigned i=0;i<sizeof fixed/sizeof fixed[0];i++)if(!eq(&j,fixed[i][0],fixed[i][1]))return fail("FFT chain contract %s must be %s",fixed[i][0],fixed[i][1]);
 if(!*jget(&j,"contract_id")||!*jget(&j,"evidence"))return fail("FFT chain contract identity/evidence missing");
 if(!num(&j,"nfft",2048)||!num(&j,"bytes_per_sample",4)||!num(&j,"mode_sel",1)||!num(&j,"btt_bits",15)||
    !num(&j,"address_bits",32)||!num(&j,"alignment",4)||!num(&j,"mode0_line",0)||!num(&j,"mode1_line",1)||
    !num(&j,"req_line",2)||!num(&j,"done_line",3))return fail("FFT chain fixed numeric contract mismatch");
 uint64_t setup;if(jnum(&j,"setup_us",&setup)||!setup||setup>1000000||
    jnum(&j,"dma_window_base",&c->window_base)||jnum(&j,"dma_window_size",&c->window_size)||
    !c->window_size||c->window_base>UINT64_MAX-(c->window_size-1))return fail("FFT chain invalid setup/address window");
 if(deployment){
  const char *gates[][2]={{"status","VERIFIED"},{"exclusive","true"},{"dma_address_identity","true"},
   {"target_gpio_mapping","VERIFIED"},{"deployment_identity","VERIFIED"}};
  for(unsigned i=0;i<sizeof gates/sizeof gates[0];i++)if(!eq(&j,gates[i][0],gates[i][1]))return fail("FFT chain contract %s must be %s",gates[i][0],gates[i][1]);
 }
 c->mode=1;c->btt=15;c->addr_bits=32;c->align=4;c->m0=0;c->m1=1;c->req=2;c->done=3;c->setup=(unsigned)setup;
 return 0;
}
static int chain_manifest(const char *path,char input_sha[65],char coeff_sha[65]){
 struct json m,stage,freq,fft,m2,format,validation;uint64_t z;char sha[65];double scale;
 if(load_batch_json(path,&m))return -1;
 if(!eq(&m,"schema","radarsat1-fft-m2-chain-v1")||!eq(&m,"status","REFERENCE_READY_BOARD_NOT_RUN")||
    !*jget(&m,"test_id")||!num(&m,"sample_count",2048))return fail("FFT-M2 chain manifest identity/status/sample_count mismatch");
 if(batch_json_object(&m,"stage_mapping",&stage)||
    !eq(&stage,"fft_input_stage","stages.s_rd(:,2017)")||!eq(&stage,"fft_output_stage","stages.S_RD(:,2017)")||
    !eq(&stage,"m2_coefficient_stage","stages.phase1(:,2017)")||!eq(&stage,"m2_output_stage","stages.S_RD_1(:,2017)")||
    !eq(&stage,"frequency_order","natural FFT order, no fftshift"))return fail("FFT-M2 stage mapping mismatch");
 if(batch_json_object(&m,"frequency_coordinates",&freq)||!eq(&freq,"order","natural FFT bin order")||
    !boolean(&freq,"fftshift_between_fft_and_m2",0))return fail("FFT-M2 frequency order/fftshift mismatch");
 if(batch_json_object(&m,"fft",&fft)||!num(&fft,"fft_length",2048)||!eq(&fft,"mode_sel_binary","01")||
    !eq(&fft,"config_tdata_hex","0x0D550B")||!eq(&fft,"direction","forward FFT")||
    !eq(&fft,"scale_schedule_hex","0x6AA")||!num(&fft,"total_right_shift_bits",11)||
    batch_json_real(&fft,"output_scale",&scale)||scale!=1.0/2048.0||!eq(&fft,"output_order","natural")||
    !eq(&fft,"rounding","XFFT convergent rounding")||!num(&fft,"input_component_bits",16)||
    !num(&fft,"output_component_bits",16)||!num(&fft,"word_bits",32)||!num(&fft,"frame_bytes",FRAME)||
    !eq(&fft,"word_layout","[31:16]=signed int16 Q, [15:0]=signed int16 I")||
    !eq(&fft,"file_byte_order","little-endian uint32"))return fail("FFT-M2 FFT metadata mismatch");
 if(batch_json_object(&m,"m2",&m2)||!num(&m2,"vector_len",2048)||
    !eq(&m2,"protocol","load N coefficients, then process N FFT output samples")||
    !eq(&m2,"data_format","signed int16 I/Q")||!eq(&m2,"coefficient_format","signed Q1.15 I/Q")||
    !eq(&m2,"arithmetic","four signed 16x16 multiplies, signed 33-bit accumulate, divide by 2^15, round-to-nearest ties-to-even, signed int16 saturation")||
    !eq(&m2,"final_bit_exact_rule","Compute m2_fixed_reference from the signed int16 samples read from board fft_actual.bin; never substitute S_RD_double or fft_reference for the actual M2 input."))return fail("FFT-M2 M2 metadata mismatch");
 if(batch_json_object(&m,"file_format",&format)||!eq(&format,"byte_order","little-endian")||
    !num(&format,"word_bits",32)||!num(&format,"frame_bytes",FRAME)||
    !eq(&format,"i_field","[15:0] signed int16")||!eq(&format,"q_field","[31:16] signed int16"))return fail("FFT-M2 file format mismatch");
 if(batch_json_object(&m,"validation",&validation)||!eq(&validation,"board_status","NOT_RUN")||
    !eq(&validation,"m2_requirement","bit-exact against reference recomputed from fft_actual.bin")||
    !boolean(&validation,"generator_binary_readback_exact",1)||!boolean(&validation,"integer_model_crosscheck",1))
  return fail("FFT-M2 validation metadata mismatch");
 if(batch_json_file(&m,"fft_input.bin",&z,input_sha)||z!=FRAME||
    batch_json_file(&m,"m2_coeff.bin",&z,coeff_sha)||z!=FRAME)return fail("FFT-M2 input/coefficient metadata must declare 8192 bytes and SHA-256");
 if(batch_json_file(&m,"fft_reference.mat",&z,sha)||!z||batch_json_file(&m,"fft_m2_reference.mat",&z,sha)||!z)
  return fail("FFT-M2 MATLAB reference metadata missing");
 if(strcmp(jget(&m,"expected_board_outputs"),"[\"fft_actual.bin\",\"fft_m2_actual.bin\"]"))
  return fail("FFT-M2 expected output names/order mismatch");
 logmsg("FFT-M2 chain manifest PASS: dataset=%s; natural FFT output is passed raw to M2; no fftshift/requantization\n",jget(&m,"test_id"));
 return 0;
}
static int packet(const char *path,int *fd){
 *fd=open(path,O_RDONLY|O_CLOEXEC);if(*fd<0)return fail("open %s: %s",path,strerror(errno));
 struct stat st;if(fstat(*fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=FRAME){close(*fd);*fd=-1;return fail("%s must be a regular file of exactly 8192 bytes",path);}return 0;
}
static int output_paths(const struct fft_m2_options *o){
 if(!strcmp(o->input,o->coeff)||!strcmp(o->fft_output,o->output)||!strcmp(o->fft_output,o->input)||!strcmp(o->fft_output,o->coeff)||
    !strcmp(o->output,o->input)||!strcmp(o->output,o->coeff))return fail("FFT-M2 input/coefficient/output paths must be distinct");
 if(o->require_deployment){struct stat st;
  if(!lstat(o->fft_output,&st)||errno!=ENOENT)return fail("FFT output must be a new file: %s",o->fft_output);
  if(!lstat(o->output,&st)||errno!=ENOENT)return fail("FFT-M2 output must be a new file: %s",o->output);
 }
 return 0;
}
int fft_m2_validate(const struct fft_m2_options *o,struct contract *fc,struct contract *mc,int *input,int *coeff){
 *input=*coeff=-1;char input_sha[65],coeff_sha[65],actual[65];
 if(chain_manifest(o->manifest,input_sha,coeff_sha)||fft_contract(o->fft_contract,o->require_deployment,fc)||
    m2_validate_contract(o->m2_contract,2048,o->require_deployment,mc))return -1;
 if(fc->btt!=mc->btt||fc->addr_bits!=mc->addr_bits||fc->align!=mc->align||
    fc->window_base!=mc->window_base||fc->window_size!=mc->window_size)
  return fail("FFT and M2 contracts disagree on the shared DMA/address window");
 if(output_paths(o)||packet(o->input,input))return -1;
 if(packet(o->coeff,coeff)){close(*input);*input=-1;return -1;}
 if(sha256_fd(*input,actual)||strcmp(actual,input_sha)||sha256_fd(*coeff,actual)||strcmp(actual,coeff_sha)){
  close(*input);close(*coeff);*input=*coeff=-1;return fail("FFT-M2 input/coefficient SHA-256 differs from manifest");
 }
 logmsg("FFT-M2 offline validation PASS: FFT input=8192, M2 coefficient=8192, two contracts checked; hardware not accessed\n");return 0;
}
static int read_fd(int fd,unsigned char *data,unsigned bytes){
 size_t n=0;while(n<bytes){ssize_t z=pread(fd,data+n,bytes-n,(off_t)n);if(z<0&&errno==EINTR)continue;if(z<=0)return fail("FFT input changed/short read");n+=(size_t)z;}
 unsigned char extra;return pread(fd,&extra,1,bytes)==0?0:fail("FFT input length changed");
}
static int write_fd(int fd,const unsigned char *data,unsigned bytes){
 size_t n=0;while(n<bytes){ssize_t z=write(fd,data+n,bytes-n);if(z<0&&errno==EINTR)continue;if(z<=0)return fail("FFT output write failed: %s",strerror(errno));n+=(size_t)z;}
 return fsync(fd)?fail("FFT output fsync failed: %s",strerror(errno)):0;
}
int fft_packet_stage(const char *output,unsigned bytes,unsigned timeout,const struct contract *c,int input){
 unsigned char data[16384];int rc=1,out=-1,dmapped=0,umapped=0,gused=0,swopened=0,touched=0,owned=0,safe=1,complete=0;
 uint64_t off=0;struct dma d={.fd=-1};struct ubuf u={.fd=-1};struct gpio g={.out=-1,.in=-1};
 struct axis_switch sw={.input={.fd=-1},.output={.fd=-1}};
 if((bytes!=8192&&bytes!=16384)||read_fd(input,data,bytes))goto cleanup;
 out=open(output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);if(out<0){fail("new FFT output required: %s",strerror(errno));goto cleanup;}
 if(dma_unbound()||mmio_no_driver(INPUT_SWITCH_BASE)||mmio_no_driver(OUTPUT_SWITCH_BASE)||ubuf_info(&u))goto cleanup;
 long page=sysconf(_SC_PAGESIZE);size_t span;if(page<=0||dma_buffer_layout(c,&u,(uint64_t)page,bytes,&off,&span))goto cleanup;
 umapped=1;if(ubuf_open(&u,span))goto cleanup;
 if(gpio_discover(&g)||!g.mapping_verified){fail("FFT GPIO live mapping unverified");goto cleanup;}
 dmapped=1;if(dma_open(&d,1))goto cleanup;
 for(unsigned ch=0;ch<=0x30;ch+=0x30){uint32_t s=dr(&d,ch+4);if((s&8)||!(s&3)){fail("FFT chain DMA active/SG, refusing takeover");goto cleanup;}}
 gused=1;if(gpio_acquire(&g,c))goto cleanup;
 touched=1;if(dma_reset(&d,timeout))goto cleanup;
 swopened=1;if(axis_switch_open(&sw,d.fd,1)||axis_switch_route_fft(&sw,timeout))goto cleanup;
 if(fft_configure(&g,c,timeout))goto cleanup;
 ubuf_write_bytes(u.map,data,bytes);ubuf_zero_bytes((char*)u.map+off,bytes);owned=1;
 if(ubuf_sync(0,bytes,1,0)||ubuf_sync(off,bytes,0,0)||stopped)goto cleanup;
 if(dma_packet(&d,u.phys,u.phys+off,c->addr_bits,bytes,timeout,1))goto cleanup;
 complete=1;
 if(ubuf_sync(0,bytes,1,1)||ubuf_sync(off,bytes,0,1))goto cleanup;
 owned=0;
 ubuf_read_bytes(data,(char*)u.map+off,bytes);if(write_fd(out,data,bytes))goto cleanup;
 logmsg("FFT stage saved one raw %u-byte natural-order signed16 frame to %s\n",bytes,output);rc=0;
cleanup:
 if(dmapped&&d.regs&&rc&&touched){logmsg("FFT failure snapshot BEFORE DMA-only cleanup:\n");dma_dump(&d);safe=!dma_abort(&d,timeout);dma_dump(&d);}
 if(owned&&safe){if(ubuf_sync(0,bytes,1,1)||ubuf_sync(off,bytes,0,1))rc=1;}
 if(owned&&!safe)logmsg("QUARANTINE udmabuf0: FFT DMA stop unproven; no CPU reuse\n");
 if(swopened&&sw.changed){
  if(safe&&complete){if(axis_switch_restore(&sw,timeout)){fail("FFT routes NOT restored; do not start M2/FFT");rc=1;}}
  else {fail("FFT stage not safely complete: routes NOT restored; operator intervention required");rc=1;}
 }
 if(swopened)axis_switch_close(&sw);
 if(gused)gpio_release(&g);
 if(umapped)ubuf_close(&u);
 if(dmapped)dma_close(&d);
 if(out>=0&&close(out)){fail("FFT output close failed");rc=1;}return rc;
}
int fft_chain_stage(const struct fft_m2_options *o,const struct contract *c,int input){
 return fft_packet_stage(o->fft_output,FRAME,o->timeout,c,input);
}
