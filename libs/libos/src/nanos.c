#include <unistd.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <sys/time.h>
#include <assert.h>
#include <time.h>
#include "syscall.h"

#if defined(__ISA_X86__)
# define GPR_TYPE "eax"
# define GPR_ARG0 "ebx"
# define GPR_ARG1 "ecx"
# define GPR_ARG2 "edx"
# define SYSCALL  "int $0x80"
# define RET_VAR  _type
#elif defined(__ISA_MIPS32__)
# define GPR_TYPE "v0"
# define GPR_ARG0 "a0"
# define GPR_ARG1 "a1"
# define GPR_ARG2 "a2"
# define SYSCALL  "syscall"
# define RET_VAR  _type
#elif defined(__ISA_RISCV32__)
# define GPR_TYPE "a7"
# define GPR_ARG0 "a0"
# define GPR_ARG1 "a1"
# define GPR_ARG2 "a2"
# define SYSCALL  "ecall"
# define RET_VAR  _arg0
#elif defined(__ISA_AM_NATIVE__)
# define GPR_TYPE "rax"
# define GPR_ARG0 "rdi"
# define GPR_ARG1 "rsi"
# define GPR_ARG2 "rdx"
# define SYSCALL  "call *0x100000"
# define RET_VAR  _type
#else
#error syscall is not supported
#endif

intptr_t _syscall_(intptr_t type, intptr_t a0, intptr_t a1, intptr_t a2) {
  register intptr_t _type asm (GPR_TYPE) = type;
  register intptr_t _arg0 asm (GPR_ARG0) = a0;
  register intptr_t _arg1 asm (GPR_ARG1) = a1;
  register intptr_t _arg2 asm (GPR_ARG2) = a2;
  asm volatile (SYSCALL : "=r" (RET_VAR) : "r"(_type), "r"(_arg0), "r"(_arg1),  "r"(_arg2));
  return RET_VAR;
}

void _exit(int status) {
  _syscall_(SYS_exit, status, 0, 0);
  while (1);
}

int _open(const char *path, int flags, mode_t mode) {
  _exit(SYS_open);
  return 0;
}

int _write(int fd, void *buf, size_t count) {
  _exit(SYS_write);
  return 0;
}

void *_sbrk(intptr_t increment) {
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
