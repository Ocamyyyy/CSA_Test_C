#include "ffttest.h"
#include <assert.h>
#include <string.h>
int main(void){
 struct json j;double v;
 assert(!parse_json("{\"binary_readback_exact\":true}",&j));
 assert(parse_json("{\"selected_columns\":[2017,2048]}",&j));
 assert(parse_json("{\"scale_factor\":185.1384772515982}",&j));
 assert(!parse_batch_json("{\"selected_columns\": [ 2017, 2048 ], \"binary_readback_exact\":true,\"scale_factor\":185.1384772515982}",&j));
 assert(!strcmp(jget(&j,"selected_columns"),"[2017,2048]"));
 assert(!batch_json_columns(&j,2));assert(batch_json_columns(&j,16));
 assert(!batch_json_real(&j,"scale_factor",&v)&&v>185&&v<186);
 const char *good[]={"{}","{\"a\":null}","{\"a\":-0.5e+2}","{\"a\":1E-4}",
  "{\"a\":[true,null,{\"b\":\"space and \\\"quotes\\\"\"}]}",
  "{\"a\":\"\\u4e2d\\u6587\\ud83d\\ude80\\n\\t\\/\\\\\"}"};
 for(unsigned i=0;i<sizeof good/sizeof good[0];i++)assert(!parse_batch_json(good[i],&j));
 assert(!parse_batch_json("{\"a\":\"\\u4e2d\"}",&j)&&!strcmp(jget(&j,"a"),"中"));
 const char *bad[]={"", "{", "{\"a\":01}","{\"a\":+1}","{\"a\":0b01}","{\"a\":0x0D550B}",
  "{\"a\":NaN}","{\"a\":Infinity}","{\"a\":1.}","{\"a\":1e}","{\"a\":trueX}",
  "{\"a\":[1,]}","{\"a\":1,}","{\"a\":1,\"\\u0061\":2}","{\"a\":{\"b\":1,\"b\":2}}",
  "{\"a\":\"\\ud800\"}","{\"a\":\"\\udc00\"}","{\"a\":\"\\u0000\"}","{\"a\":\"\\q\"}","{}junk"};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)assert(parse_batch_json(bad[i],&j));
 assert(!parse_batch_json("{\"a\":\"中文\"}",&j));
 assert(parse_batch_json("{\"a\":\"\xff\"}",&j));
 assert(parse_batch_json("{\"a\":\"\xc0\xaf\"}",&j));
 assert(parse_batch_json("{\"a\":\"\xed\xa0\x80\"}",&j));
 const char *badcols[]={"[1,1]","[1.0,2]","[-1,2]","[true,2]","[0,2]","[1]","[1,2,3]","[]"};
 for(unsigned i=0;i<sizeof badcols/sizeof badcols[0];i++){
  char text[128];snprintf(text,sizeof text,"{\"selected_columns\":%s}",badcols[i]);
  assert(!parse_batch_json(text,&j)&&batch_json_columns(&j,2));
 }
 char deep[256];size_t n=0;memcpy(deep,"{\"a\":",5);n=5;
 for(unsigned i=0;i<32;i++)deep[n++]='[';
 deep[n++]='0';for(unsigned i=0;i<32;i++)deep[n++]=']';deep[n++]='}';deep[n]=0;
 assert(parse_batch_json(deep,&j));
 /* Deterministic malformed-input exercise; sanitizers check parser bounds. */
 unsigned seed=7;const char chars[]="{}[]\"\\:,truefalsnul0123456789-.eE+ \n\t";
 for(unsigned trial=0;trial<20000;trial++){
  char text[128];seed=seed*1664525u+1013904223u;unsigned len=seed%127;
  for(unsigned i=0;i<len;i++){seed=seed*1664525u+1013904223u;text[i]=chars[seed%(sizeof chars-1)];}
  text[len]=0;(void)parse_batch_json(text,&j);
 }
 puts("Batch JSON tests PASS: legacy behavior retained; standard types/escapes, grammar, duplicate keys, columns, bounds");
 return 0;
}
