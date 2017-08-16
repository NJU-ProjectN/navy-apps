#include <stdio.h>

int main(){
  FILE *fp = fopen("/dev/events", "r");
  while(1){
    char buf[256];
    char *p = buf, ch;
    while((ch = fgetc(fp)) != -1){
      *p ++ = ch;
      if(ch == '\n') {
        *p = '\0';
        break;
      }
    }
    printf("receive event: %s", buf);
  }

  fclose(fp);
  return 0;
}

