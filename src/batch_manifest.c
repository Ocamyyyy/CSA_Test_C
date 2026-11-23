/* Batch metadata adapter: dataset identity/quantization is distinct from the
 * verified FPGA contract. No sample, scale factor or source file is changed. */
#include "ffttest.h"
#include <string.h>

static const struct pair *field(const struct json *j,const char *k){
 for(unsigned i=0;i<j->n;i++)if(!strcmp(j->p[i].key,k))return &j->p[i];
 return NULL;
}
static int text_is(const struct json *j,const char *key,const char *expected){
 const struct pair *p=field(j,key);return p&&p->type==1&&!strcmp(p->value,expected);
}
static int put(struct json *j,const char *key,const char *value,int type){
 if(j->n>=64||strlen(key)>=sizeof j->p[0].key||strlen(value)>=sizeof j->p[0].value)return -1;
 struct pair *p=&j->p[j->n++];strcpy(p->key,key);strcpy(p->value,value);p->type=type;return 0;
}
static int mode_ok(const struct json *j){
 const struct pair *p=field(j,"mode_sel");if(!p)return 0;
 uint64_t v;return (!jnum(j,"mode_sel",&v)&&v==1)||
  (p->type==1&&(!strcmp(p->value,"0b01")||!strcmp(p->value,"01")));
}
int normalize_batch_manifest(const struct json *hardware,const struct json *m,unsigned frames,struct json *single){
 if(!text_is(m,"schema","ffttest-batch-v1"))return fail("Batch manifest schema must be ffttest-batch-v1");
 uint64_t n,bytes;
 if(jnum(m,"frame_count",&n)||n!=frames||jnum(m,"frame_bytes",&bytes)||bytes!=FRAME)
  return fail("Batch manifest frame_count/frame_bytes mismatch");
 if(jnum(m,"nfft",&n)||n!=2048)return fail("Batch nfft must be integer 2048");
 if(field(m,"total_bytes")&&(jnum(m,"total_bytes",&n)||n!=(uint64_t)frames*FRAME))return fail("Batch total_bytes mismatch");
 if(!text_is(m,"contract_id",jget(hardware,"contract_id"))||!*jget(hardware,"contract_id"))
  return fail("Batch contract_id must identify hardware contract '%s'; put the Batch data identity in dataset_id (no automatic ID substitution)",jget(hardware,"contract_id"));
 const struct pair *dataset=field(m,"dataset_id");
 if(dataset){
  if(dataset->type!=1||!*dataset->value)return fail("Batch dataset_id must be a nonempty string");
  for(const unsigned char *p=(const unsigned char *)dataset->value;*p;p++)if(*p<32||*p==127)return fail("Batch dataset_id contains control characters");
 }
 if(batch_json_columns(m,frames))return fail("Batch selected_columns must contain frame_count distinct positive integers in frame order");
 if(field(m,"mode_sel")&&!mode_ok(m))return fail("Batch mode_sel must be integer 1 or string 0b01/01");
 double scale;
 if(field(m,"scale_factor")&&(batch_json_real(m,"scale_factor",&scale)||scale<=0))return fail("Batch dataset scale_factor must be a finite positive JSON number");
 const struct pair *readback=field(m,"binary_readback_exact");
 if(readback&&(readback->type!=2||strcmp(readback->value,"true")))return fail("Batch binary_readback_exact must be boolean true");
 int matlab=field(m,"component_type")||field(m,"fft_direction")||field(m,"fft_output_order")||field(m,"fft_output_scale");
 if(!matlab){
  /* Existing canonical Batch schema keeps all prior hardware comparisons. */
  *single=*m;
  for(unsigned i=0;i<single->n;i++)if(!strcmp(single->p[i].key,"schema"))strcpy(single->p[i].value,"ffttest-manifest-v1");
  if(jnum(m,"bytes_per_sample",&n)||n!=4)return fail("Batch bytes_per_sample must be integer 4");
 }else{
  if(!dataset)return fail("Batch MATLAB manifest requires dataset_id; contract_id is reserved for hardware identity");
  const struct pair *quant=field(m,"quantization");
  if(quant&&(quant->type!=1||!*quant->value))return fail("Batch MATLAB quantization must describe dataset quantization as a string");
  const char *strings[][2]={
   {"component_type","signed int16 I/Q"},{"word_layout","bits[31:16]=Q, bits[15:0]=I"},
   {"byte_order","little-endian"},{"fft_direction","forward"},{"fft_output_order","natural"},
   {"config_word","0x0D550B"},{"frame_layout","frame-major contiguous; no header or padding; 8192 bytes per frame"},
   {"sample_order","2048 natural input samples per frame"}
  };
  for(unsigned i=0;i<sizeof strings/sizeof strings[0];i++)
   if(!text_is(m,strings[i][0],strings[i][1]))return fail("Batch MATLAB field %s must be '%s'",strings[i][0],strings[i][1]);
  if(!field(m,"mode_sel")||!mode_ok(m))return fail("Batch MATLAB mode_sel missing or not 01");
  if(batch_json_real(m,"fft_output_scale",&scale)||scale!=1.0/2048.0)return fail("Batch fft_output_scale must equal 1/2048; dataset scale_factor is a separate quantity");
  const char *aliases[][2]={{"direction","forward"},{"output_order","natural"},{"encoding","signed-int16"},{"iq_order","I-low"}};
  for(unsigned i=0;i<sizeof aliases/sizeof aliases[0];i++)if(field(m,aliases[i][0])&&!text_is(m,aliases[i][0],aliases[i][1]))return fail("conflicting Batch FFT field %s",aliases[i][0]);
  if(field(m,"bytes_per_sample")&&(jnum(m,"bytes_per_sample",&n)||n!=4))return fail("Batch bytes_per_sample mismatch");
  if(field(m,"scaling")&&!text_is(m,"scaling",jget(hardware,"scaling")))return fail("conflicting Batch hardware scaling");
  if(field(m,"fft_quantization")&&!text_is(m,"fft_quantization",jget(hardware,"quantization")))return fail("Batch fft_quantization differs from hardware contract");
  /* MATLAB quantization describes the input dataset (common I/Q scale).
   * XFFT rounding belongs to the verified hardware contract, not that field.
   * Exact word/mode/length/output-scale checks above establish the FFT request. */
  memset(single,0,sizeof *single);
  const char *values[][2]={{"schema","ffttest-manifest-v1"},{"contract_id",jget(m,"contract_id")},
   {"nfft","2048"},{"bytes_per_sample","4"},{"byte_order","little"},{"iq_order","I-low"},
   {"encoding","signed-int16"},{"direction","forward"},{"output_order","natural"},
   {"config_word","0x0D550B"},{"scaling",jget(hardware,"scaling")},{"quantization",jget(hardware,"quantization")}};
  for(unsigned i=0;i<sizeof values/sizeof values[0];i++)if(put(single,values[i][0],values[i][1],i==2||i==3?3:1))return fail("Batch normalized metadata limit exceeded");
 }
 logmsg("Batch metadata: dataset_id=%s selected_columns=%s dataset_scale_factor=%s dataset_quantization=%s (metadata only; no re-quantization)\n",
  dataset?dataset->value:"unspecified",jget(m,"selected_columns"),jget(m,"scale_factor"),matlab?jget(m,"quantization"):jget(m,"dataset_quantization"));
 return 0;
}
