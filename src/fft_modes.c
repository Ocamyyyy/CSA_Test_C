#include "fft_modes.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const struct fft_mode_spec modes[]={
 {"S2","FFT","0x0D550B","0x6AA","fft(x_quant,2048)/2048",2048,8192,1,11,1.0/2048.0},
 {"S5","FFT","0x15550C","0xAAA","fft(x_quant,4096)/4096",4096,16384,3,12,1.0/4096.0},
 {"S7","IFFT","0x15540C","0xAAA","ifft(x_quant,4096)",4096,16384,2,12,1.0/4096.0},
 {"S10","IFFT","0x0D540B","0x6AA","ifft(x_quant,2048)",2048,8192,0,11,1.0/2048.0}};
const struct fft_mode_spec *fft_mode_find(const char *name){
 if(!name)return NULL;
 for(unsigned i=0;i<sizeof modes/sizeof modes[0];i++)if(!strcmp(name,modes[i].stage))return &modes[i];
 return NULL;
}
static int eq(const struct json *j,const char *k,const char *v){return !strcmp(jget(j,k),v);}
static int num(const struct json *j,const char *k,uint64_t expected){uint64_t v;return !jnum(j,k,&v)&&v==expected;}
static const struct pair *field(const struct json *j,const char *k){for(unsigned i=0;i<j->n;i++)if(!strcmp(j->p[i].key,k))return &j->p[i];return NULL;}
static int boolean(const struct json *j,const char *k,int expected){const struct pair *p=field(j,k);return p&&p->type==2&&!strcmp(p->value,expected?"true":"false");}
static int contract(const char *path,int deployment,struct contract *c){
 struct json j;if(load_json(path,&j))return -1;memset(c,0,sizeof *c);
 const char *fixed[][2]={{"schema","ffttest-fft-modes-contract-v1"},{"protocol_status","VERIFIED"},
  {"byte_order","little"},{"iq_order","I-low"},{"encoding","signed-int16"},{"output_order","natural"},
  {"rounding","convergent"},{"scaling","scaled"},
  {"s2_config_word","0x0D550B"},{"s5_config_word","0x15550C"},{"s7_config_word","0x15540C"},{"s10_config_word","0x0D540B"},
  {"handshake","request-high-done-low-release-done-high"},{"sync_semantics","upstream-4.5.3"}};
 for(unsigned i=0;i<sizeof fixed/sizeof fixed[0];i++)if(!eq(&j,fixed[i][0],fixed[i][1]))return fail("FFT mode contract %s must be %s",fixed[i][0],fixed[i][1]);
 if(!*jget(&j,"contract_id")||!*jget(&j,"evidence"))return fail("FFT mode contract identity/evidence missing");
 if(!num(&j,"s2_mode_sel",1)||!num(&j,"s2_samples",2048)||!num(&j,"s5_mode_sel",3)||!num(&j,"s5_samples",4096)||!num(&j,"s7_mode_sel",2)||!num(&j,"s7_samples",4096)||
    !num(&j,"s10_mode_sel",0)||!num(&j,"s10_samples",2048)||
    !num(&j,"dma_base",DMA_BASE)||!num(&j,"fft_gpio_base",0x41200000)||!num(&j,"input_switch_base",INPUT_SWITCH_BASE)||
    !num(&j,"output_switch_base",OUTPUT_SWITCH_BASE)||!num(&j,"bytes_per_sample",4)||!num(&j,"max_samples",4096)||
    !num(&j,"btt_bits",15)||!num(&j,"address_bits",32)||!num(&j,"alignment",4)||
    !num(&j,"mode0_line",0)||!num(&j,"mode1_line",1)||!num(&j,"req_line",2)||!num(&j,"done_line",3)||
    !boolean(&j,"micro_dma",0))return fail("FFT mode DMA/GPIO contract mismatch");
 uint64_t setup;if(jnum(&j,"setup_us",&setup)||!setup||setup>1000000||
    jnum(&j,"dma_window_base",&c->window_base)||jnum(&j,"dma_window_size",&c->window_size)||
    !c->window_size||c->window_base>UINT64_MAX-(c->window_size-1))return fail("FFT mode invalid setup/address window");
 if(deployment){const char *gates[][2]={{"status","VERIFIED"},{"exclusive","true"},{"dma_address_identity","true"},
   {"target_gpio_mapping","VERIFIED"},{"deployment_identity","VERIFIED"}};
  for(unsigned i=0;i<sizeof gates/sizeof gates[0];i++)if(!eq(&j,gates[i][0],gates[i][1]))return fail("FFT mode contract %s must be %s",gates[i][0],gates[i][1]);}
 c->btt=15;c->addr_bits=32;c->align=4;c->m0=0;c->m1=1;c->req=2;c->done=3;c->setup=(unsigned)setup;return 0;
}
static int manifest(const char *path,const struct fft_mode_spec *s,char input_sha[65]){
 struct json m,source,config,validation;uint64_t z;char ref_sha[65];double scale;
 if(load_batch_json(path,&m))return -1;
 if(!strcmp(s->stage,"S2")&&!strcmp(jget(&m,"schema"),"radarsat1-fft-m2-chain-v1")){
  if(!eq(&m,"status","REFERENCE_READY_BOARD_NOT_RUN")||!num(&m,"sample_count",2048)||
     batch_json_object(&m,"fft",&config)||!num(&config,"fft_length",2048)||
     !eq(&config,"mode_sel_binary","01")||!eq(&config,"config_tdata_hex","0x0D550B")||
     !eq(&config,"direction","forward FFT")||!eq(&config,"scale_schedule_hex","0x6AA")||
     !num(&config,"total_right_shift_bits",11)||batch_json_real(&config,"output_scale",&scale)||scale!=1.0/2048.0||
     !eq(&config,"output_order","natural")||!eq(&config,"rounding","XFFT convergent rounding")||
     !num(&config,"input_component_bits",16)||!num(&config,"output_component_bits",16)||!num(&config,"word_bits",32)||
     !eq(&config,"word_layout","[31:16]=signed int16 Q, [15:0]=signed int16 I")||
     !eq(&config,"file_byte_order","little-endian uint32")||!num(&config,"frame_bytes",8192)||
     batch_json_file(&m,"fft_input.bin",&z,input_sha)||z!=8192)
   return fail("S2 FFT metadata/files mismatch in FFT-M2 reference manifest");
  return 0;
 }
 if(!eq(&m,"schema","radarsat1-csa-fft-mode-v1")||!eq(&m,"status","REFERENCE_READY_BOARD_NOT_RUN")||
    !eq(&m,"mode",s->stage)||!num(&m,"sample_count",s->samples))return fail("FFT mode manifest identity/sample_count mismatch");
 if(batch_json_object(&m,"source",&source)||!boolean(&source,"no_additional_fftshift",1))return fail("FFT mode source must require no additional fftshift");
 char mode[3]={(char)('0'+((s->mode_sel>>1)&1)),(char)('0'+(s->mode_sel&1)),0};
 if(batch_json_object(&m,"config",&config)||!num(&config,"fft_length",s->samples)||!eq(&config,"direction",s->direction)||
    !eq(&config,"mode_sel_binary",mode)||!eq(&config,"config_tdata_hex",s->config_word)||
    !eq(&config,"scaling_schedule_hex",s->scale_schedule)||!num(&config,"total_right_shift_bits",s->shift)||
    batch_json_real(&config,"total_scale",&scale)||scale!=s->scale||!eq(&config,"expected_matlab_operation",s->operation)||
    !eq(&config,"output_order","natural")||!eq(&config,"rounding","convergent rounding inside XFFT")||
    !num(&config,"input_component_bits",16)||!num(&config,"output_component_bits",16)||!num(&config,"word_bits",32)||
    !eq(&config,"word_layout","[31:16]=signed int16 Q, [15:0]=signed int16 I")||
    !eq(&config,"file_byte_order","little-endian uint32")||!num(&config,"frame_bytes",s->bytes))return fail("FFT mode config metadata mismatch");
 if(batch_json_object(&m,"validation",&validation)||!eq(&validation,"board_output_file","actual.bin")||
    !eq(&validation,"board_status","NOT_RUN")||!boolean(&validation,"binary_readback_exact",1))return fail("FFT mode validation metadata mismatch");
 if(batch_json_file(&m,"input.bin",&z,input_sha)||z!=s->bytes||batch_json_file(&m,"reference.mat",&z,ref_sha)||!z)
  return fail("FFT mode files metadata mismatch");
 return 0;
}
int fft_mode_validate(const struct fft_mode_options *o,const struct fft_mode_spec **spec,struct contract *c,int *input){
 *input=-1;*spec=fft_mode_find(o->stage);if(!*spec)return fail("--fft-stage must be S2, S5, S7 or S10");
 char expected[65],actual[65];if(manifest(o->manifest,*spec,expected)||contract(o->contract,o->require_deployment,c))return -1;
 c->mode=(*spec)->mode_sel;
 if((*spec)->bytes>((1u<<c->btt)-1))return fail("FFT mode frame exceeds DMA BTT");
 if(!strcmp(o->input,o->output))return fail("FFT mode input/output paths must be distinct");
 if(o->require_deployment){struct stat st;if(!lstat(o->output,&st)||errno!=ENOENT)return fail("FFT mode output must be a new file: %s",o->output);}
 *input=open(o->input,O_RDONLY|O_CLOEXEC);if(*input<0)return fail("open FFT mode input %s: %s",o->input,strerror(errno));
 struct stat st;if(fstat(*input,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)(*spec)->bytes){close(*input);*input=-1;return fail("FFT mode input must be a regular file of exactly %u bytes",(*spec)->bytes);}
 if(sha256_fd(*input,actual)||strcmp(actual,expected)){close(*input);*input=-1;return fail("FFT mode input SHA-256 differs from manifest");}
 logmsg("FFT mode offline validation PASS: stage=%s samples=%u bytes=%u mode_sel=%u config=%s direction=%s scale=%s; hardware not accessed\n",
  (*spec)->stage,(*spec)->samples,(*spec)->bytes,(*spec)->mode_sel,(*spec)->config_word,(*spec)->direction,(*spec)->scale_schedule);return 0;
}
int fft_mode_execute(const struct fft_mode_options *o,const struct fft_mode_spec *s,const struct contract *c,int input){
 logmsg("FFT mode run: one DMA frame, stage=%s samples=%u bytes=%u (not split)\n",s->stage,s->samples,s->bytes);
 return fft_packet_stage(o->output,s->bytes,o->timeout,c,input);
}
