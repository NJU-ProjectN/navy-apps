#include <unistd.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <sys/time.h>
#include <assert.h>
#include <time.h>
#include "syscall.h"

intptr_t _syscall_(intptr_t type, intptr_t a0, intptr_t a1, intptr_t a2){
  intptr_t ret = -1;
#if defined(__ISA_X86__)
  asm volatile("int $0x80": "=a"(ret): "a"(type), "b"(a0), "c"(a1), "d"(a2));
#elif defined(__ISA_MIPS32__)
  asm volatile("move $a0, %1;"
               "move $a1, %2;"
               "move $a2, %3;"
               "move $a3, %4;"
               "syscall;"
               "move %0, $v0": "=r"(ret) : "r"(type), "r"(a0), "r"(a1), "r"(a2): "a0", "a1", "a2", "a3", "v0");
#elif defined(__ISA_RISCV32__)
  asm volatile("mv a0, %1;"
               "mv a1, %2;"
               "mv a2, %3;"
               "mv a3, %4;"
               "ecall;"
               "move %0, a0": "=r"(ret) : "r"(type), "r"(a0), "r"(a1), "r"(a2): "a0", "a1", "a2", "a3");
#elif defined(__ISA_AM_NATIVE__)
  asm volatile("call *0x100000": "=a"(ret): "a"(type), "S"(a0), "d"(a1), "c"(a2));
#else
#error _syscall_ is not implemented
#endif
  return ret;
}

void _exit(int status) {
  _syscall_(SYS_exit, status, 0, 0);
  while (1);
}

int _open(const char *path, int flags, mode_t mode) {
  _exit(SYS_open);
  return 0;
}

int _write(int fd, void *buf, size_t count){
  _exit(SYS_write);
  return 0;
}

void *_sbrk(intptr_t increment){
  return (void *)-1;
}

int _read(int fd, void *buf, size_t count) {
  _exit(SYS_read);
  return 0;
}

int _close(int fd) {
  _exit(SYS_close);
  return 0;
}

off_t _lseek(int fd, off_t offset, int whence) {
  _exit(SYS_lseek);
  return 0;
}

int _execve(const char *fname, char * const argv[], char *const envp[]) {
  _exit(SYS_execve);
  return 0;
}

// The code below is not used by Nanos-lite.
// But to pass linking, they are defined as dummy functions

int _fstat(int fd, struct stat *buf) {
  return 0;
}

int _kill(int pid, int sig) {
  _exit(-SYS_kill);
  return -1;
}

pid_t _getpid() {
  _exit(-SYS_getpid);
  return 1;
}

pid_t _fork() {
  assert(0);
  return -1;
}

int _link(const char *d, const char *n) {
  assert(0);
  return -1;
}

int _unlink(const char *n) {
  assert(0);
  return -1;
}

pid_t _wait(int *status) {
  assert(0);
  return -1;
}

clock_t _times(void *buf) {
  assert(0);
  return 0;
}

int _gettimeofday(struct timeval *tv) {
  assert(0);
  tv->tv_sec = 0;
  tv->tv_usec = 0;
  return 0;
}
