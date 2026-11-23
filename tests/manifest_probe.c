/* OFFLINE ONLY: no execution or hardware-access path is called. */
#include "ffttest.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char **argv){
 struct json j;
 if(argc==3&&!strcmp(argv[1],"parse"))return load_batch_json(argv[2],&j)?1:0;
 if(argc==3&&!strcmp(argv[1],"legacy"))return load_json(argv[2],&j)?1:0;
 if(argc==6&&!strcmp(argv[1],"validate")){
  struct batch_options o={argv[4],"unused-output.bin",argv[2],argv[3],"unused-stats.jsonl",(unsigned)atoi(argv[5]),2048,2000,1};
  struct contract c;int fd=-1;int rc=batch_validate(&o,&c,&fd);
  if(fd>=0)close(fd);
  if(!rc)puts("OFFLINE Batch metadata/input validation PASS; hardware not accessed");
  return rc?1:0;
 }
 return 2;
}
