#include <stdint.h>

#define SYS_none 0
extern int _syscall_(int, intptr_t, intptr_t, intptr_t);

int main() {
  // this system call will trap into OS but do nothing
  _syscall_(SYS_none, 0, 0, 0);
  return 0;
}
