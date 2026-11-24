/* Bounded standard-JSON reader used ONLY for Batch metadata.
 * The legacy hardware/single-frame parser and its rejection rules are unchanged.
 * Types: 1 string, 2 boolean, 3 unsigned integer, 4 other JSON number,
 *        5 array, 6 null, 7 object. Compound metadata is retained verbatim.
 */
#include "ffttest.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

struct reader {const char *p;};
static void space(struct reader *r){while(*r->p==' '||*r->p=='\n'||*r->p=='\r'||*r->p=='\t')r->p++;}
static int append(char *b,size_t cap,size_t *n,unsigned c){
 if(!c)return -1; /* embedded NUL cannot be represented by the existing API */
 if(*n+1>=cap)return -1;
 b[(*n)++]=(char)c;return 0;
}
static int hex4(struct reader *r,unsigned *v){
 *v=0;for(unsigned i=0;i<4;i++){
  unsigned char c=*r->p;if(!c)return -1;r->p++;
  unsigned x;if(c>='0'&&c<='9')x=c-'0';else if(c>='a'&&c<='f')x=c-'a'+10;
  else if(c>='A'&&c<='F')x=c-'A'+10;else return -1;
  *v=(*v<<4)|x;
 }return 0;
}
static int utf8(char *b,size_t cap,size_t *n,unsigned c){
 if(c<0x80)return append(b,cap,n,c);
 if(c<0x800)return append(b,cap,n,0xc0|(c>>6))||append(b,cap,n,0x80|(c&63));
 if(c<0x10000)return append(b,cap,n,0xe0|(c>>12))||append(b,cap,n,0x80|((c>>6)&63))||append(b,cap,n,0x80|(c&63));
 return append(b,cap,n,0xf0|(c>>18))||append(b,cap,n,0x80|((c>>12)&63))||append(b,cap,n,0x80|((c>>6)&63))||append(b,cap,n,0x80|(c&63));
}
static int string(struct reader *r,char *b,size_t cap){
 if(*r->p!='"')return -1;
 r->p++;size_t n=0;
 while(*r->p&&*r->p!='"'){
  unsigned c=(unsigned char)*r->p++;if(c<32)return -1;
  if(c>=0x80){
   unsigned tail,cp,min;
   if(c>=0xc2&&c<=0xdf){tail=1;cp=c&31;min=0x80;}
   else if(c>=0xe0&&c<=0xef){tail=2;cp=c&15;min=0x800;}
   else if(c>=0xf0&&c<=0xf4){tail=3;cp=c&7;min=0x10000;}
   else return -1;
   if(append(b,cap,&n,c))return -1;
   for(unsigned i=0;i<tail;i++){
    unsigned next=(unsigned char)*r->p;if((next&0xc0)!=0x80)return -1;
    r->p++;cp=(cp<<6)|(next&63);if(append(b,cap,&n,next))return -1;
   }
   if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return -1;
   continue;
  }
  if(c=='\\'){
   c=(unsigned char)*r->p;if(!c)return -1;r->p++;
   if(c=='u'){
    if(hex4(r,&c))return -1;
    if(c>=0xd800&&c<=0xdbff){unsigned lo;
     if(*r->p!='\\'||r->p[1]!='u')return -1;
     r->p+=2;if(hex4(r,&lo)||lo<0xdc00||lo>0xdfff)return -1;
     c=0x10000+((c-0xd800)<<10)+(lo-0xdc00);
    }else if(c>=0xdc00&&c<=0xdfff)return -1;
    if(utf8(b,cap,&n,c))return -1;
    continue;
   }
   switch(c){case '"':case '\\':case '/':break;case 'b':c='\b';break;case 'f':c='\f';break;
    case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;default:return -1;}
  }
  if(append(b,cap,&n,c))return -1;
 }
 if(*r->p!='"')return -1;
 r->p++;b[n]=0;return 0;
}
static int digit(char c){return c>='0'&&c<='9';}
static int number(struct reader *r,int *type){
 const char *start=r->p;int unsigned_int=1;
 if(*r->p=='-'){unsigned_int=0;r->p++;}
 if(*r->p=='0')r->p++;
 else {if(*r->p<'1'||*r->p>'9')return -1;while(digit(*r->p))r->p++;}
 if(*r->p=='.'){unsigned_int=0;r->p++;if(!digit(*r->p))return -1;while(digit(*r->p))r->p++;}
 if(*r->p=='e'||*r->p=='E'){unsigned_int=0;r->p++;if(*r->p=='+'||*r->p=='-')r->p++;
  if(!digit(*r->p))return -1;
  while(digit(*r->p))r->p++;
 }
 *type=4;
 if(unsigned_int){char *end;errno=0;(void)strtoull(start,&end,10);if(!errno&&end==r->p)*type=3;}
 return 0;
}
static int raw_value(char *out,size_t cap,const char *start,const char *end){
 size_t n=0;int quoted=0,escape=0;
 for(const char *p=start;p<end;p++){
  unsigned char c=(unsigned char)*p;
  if(!quoted&&(c==' '||c=='\t'||c=='\r'||c=='\n'))continue;
  if(n+1>=cap)return -1;
  out[n++]=(char)c;
  if(escape)escape=0;
  else if(quoted&&c=='\\')escape=1;
  else if(c=='"')quoted=!quoted;
 }
 out[n]=0;return 0;
}
static int value(struct reader *,unsigned,int *);
static int object(struct reader *r,unsigned depth,struct json *top){
 if(depth>16||*r->p!='{')return -1;
 r->p++;space(r);char keys[64][64];unsigned count=0;
 if(*r->p=='}'){r->p++;return 0;}
 for(;;){
  if(count==64||string(r,keys[count],sizeof keys[count]))return -1;
  for(unsigned i=0;i<count;i++)if(!strcmp(keys[i],keys[count]))return -1;
  space(r);if(*r->p!=':')return -1;r->p++;space(r);
  const char *start=r->p;int type;
  if(value(r,depth+1,&type))return -1;
  if(top){
   struct pair *p=&top->p[count];strcpy(p->key,keys[count]);p->type=type;
   if(type==1){struct reader copy={start};if(string(&copy,p->value,sizeof p->value))return -1;}
   else if(raw_value(p->value,sizeof p->value,start,r->p))return -1;
   top->n=count+1;
  }
  count++;space(r);if(*r->p=='}'){r->p++;return 0;}
  if(*r->p!=',')return -1;
  r->p++;space(r);
 }
}
static int value(struct reader *r,unsigned depth,int *type){
 if(depth>16)return -1;
 if(*r->p=='"'){char b[512];*type=1;return string(r,b,sizeof b);}
 if(*r->p=='{'){*type=7;return object(r,depth,NULL);}
 if(*r->p=='['){
  *type=5;r->p++;space(r);if(*r->p==']'){r->p++;return 0;}
  for(;;){int t;if(value(r,depth+1,&t))return -1;space(r);if(*r->p==']'){r->p++;return 0;}
   if(*r->p!=',')return -1;
   r->p++;space(r);
  }
 }
 if(!strncmp(r->p,"true",4)){*type=2;r->p+=4;return 0;}
 if(!strncmp(r->p,"false",5)){*type=2;r->p+=5;return 0;}
 if(!strncmp(r->p,"null",4)){*type=6;r->p+=4;return 0;}
 return number(r,type);
}
int parse_batch_json(const char *text,struct json *j){
 memset(j,0,sizeof *j);struct reader r={text};space(&r);
 if(object(&r,0,j))return -1;
 space(&r);return *r.p?-1:0;
}
int load_batch_json(const char *path,struct json *j){
 char text[32769];FILE *f=fopen(path,"rb");if(!f)return fail("open Batch manifest %s: %s",path,strerror(errno));
 size_t n=fread(text,1,sizeof text,f);int bad=ferror(f);fclose(f);
 if(bad||n>=sizeof text||memchr(text,0,n))return fail("invalid/oversized Batch JSON %s",path);
 text[n]=0;if(parse_batch_json(text,j))return fail("invalid Batch JSON %s (standard JSON required; limits: 32KiB, 64 keys, 2047 bytes/value, depth 16)",path);
 return 0;
}
static const struct pair *field(const struct json *j,const char *key){
 for(unsigned i=0;i<j->n;i++)if(!strcmp(j->p[i].key,key))return &j->p[i];
 return NULL;
}
int batch_json_real(const struct json *j,const char *key,double *v){
 const struct pair *p=field(j,key);if(!p||(p->type!=3&&p->type!=4))return -1;
 char *end;errno=0;double d=strtod(p->value,&end);if(errno||*end||!isfinite(d))return -1;*v=d;return 0;
}
int batch_json_object(const struct json *j,const char *key,struct json *out){
 const struct pair *p=field(j,key);
 return !p||p->type!=7||parse_batch_json(p->value,out)?-1:0;
}
static int sha256_text(const char *s){
 if(strlen(s)!=64)return -1;
 for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return -1;
 return 0;
}
int batch_json_file(const struct json *j,const char *name,uint64_t *bytes,char sha[65]){
 const struct pair *p=field(j,"files");if(!p||p->type!=5)return -1;
 struct reader r={p->value};if(*r.p++!='[')return -1;space(&r);unsigned found=0;
 if(*r.p==']')return -1;
 for(;;){
  struct json item;if(object(&r,1,&item))return -1;
  const struct pair *n=field(&item,"name"),*h=field(&item,"sha256");uint64_t z;
  if(!n||n->type!=1||!h||h->type!=1||sha256_text(h->value)||jnum(&item,"bytes",&z))return -1;
  if(!strcmp(n->value,name)){if(found++)return -1;*bytes=z;strcpy(sha,h->value);}
  space(&r);if(*r.p==']'){r.p++;space(&r);return !*r.p&&found==1?0:-1;}
  if(*r.p!=',')return -1;
  r.p++;space(&r);
 }
}
int batch_json_columns(const struct json *j,unsigned frames){
 const struct pair *p=field(j,"selected_columns");if(!p)return 0;
 if(p->type!=5)return -1;
 struct reader r={p->value};r.p++;space(&r);uint64_t ids[64];unsigned n=0;
 for(;;){
  if(n>=frames||n>=64)return -1;
  const char *start=r.p;int type;if(number(&r,&type)||type!=3)return -1;
  char *end;errno=0;uint64_t id=strtoull(start,&end,10);if(errno||end!=r.p||!id)return -1;
  for(unsigned i=0;i<n;i++)if(ids[i]==id)return -1;
  ids[n++]=id;space(&r);if(*r.p==']'){r.p++;space(&r);return n==frames&&!*r.p?0:-1;}
  if(*r.p!=',')return -1;
  r.p++;space(&r);
 }
}
