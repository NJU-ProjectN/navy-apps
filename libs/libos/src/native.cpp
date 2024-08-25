#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <dlfcn.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>

//#define MODE_800x600

#define FPS 60
#ifdef MODE_800x600
const int disp_w = 800, disp_h = 600;
#else
const int disp_w = 400, disp_h = 300;
#endif
static int pipe_size = 0;
#define FB_SIZE (disp_w * disp_h * sizeof(uint32_t))

static FILE *(*glibc_fopen)(const char *path, const char *mode) = NULL;
static int (*glibc_open)(const char *path, int flags, ...) = NULL;
static ssize_t (*glibc_read)(int fd, void *buf, size_t count) = NULL;
static ssize_t (*glibc_write)(int fd, const void *buf, size_t count) = NULL;
static int (*glibc_execve)(const char *filename, char *const argv[], char *const envp[]) = NULL;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static int dummy_fd = -1;
static int dispinfo_fd = -1;
static int fb_memfd = -1;
static int evt_fd = -1;
static int sb_fifo[2] = {-1, -1};
static int sbctl_fd = -1;
static uint32_t *fb = NULL;
static char fsimg_path[512] = "";

static inline void get_fsimg_path(char *newpath, const char *path) {
  sprintf(newpath, "%s%s", fsimg_path, path);
}

#define _KEYS(_) \
  _(ESCAPE) _(F1) _(F2) _(F3) _(F4) _(F5) _(F6) _(F7) _(F8) _(F9) _(F10) _(F11) _(F12) \
  _(GRAVE) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9) _(0) _(MINUS) _(EQUALS) _(BACKSPACE) \
  _(TAB) _(Q) _(W) _(E) _(R) _(T) _(Y) _(U) _(I) _(O) _(P) _(LEFTBRACKET) _(RIGHTBRACKET) _(BACKSLASH) \
  _(CAPSLOCK) _(A) _(S) _(D) _(F) _(G) _(H) _(J) _(K) _(L) _(SEMICOLON) _(APOSTROPHE) _(RETURN) \
  _(LSHIFT) _(Z) _(X) _(C) _(V) _(B) _(N) _(M) _(COMMA) _(PERIOD) _(SLASH) _(RSHIFT) \
  _(LCTRL) _(APPLICATION) _(LALT) _(SPACE) _(RALT) _(RCTRL) \
  _(UP) _(DOWN) _(LEFT) _(RIGHT) _(INSERT) _(DELETE) _(HOME) _(END) _(PAGEUP) _(PAGEDOWN)

#define COND(k) \
  if (scancode == SDL_SCANCODE_##k) name = #k;

static void update_screen() {
  // The following SDL functions should be called in the main thread.
  // See https://github.com/libsdl-org/SDL/blob/288aea3b40faf882f26411e6a3fe06329bba2c05/include/SDL_render.h#L45
  SDL_UpdateTexture(texture, NULL, fb, disp_w * sizeof(Uint32));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

#define KEY_QUEUE_LEN 64
static SDL_Event key_queue[KEY_QUEUE_LEN] = {};
static int key_f = 0, key_r = 0;
static SDL_mutex *key_queue_lock = NULL;

static int handle_sdl_event() {
  SDL_Event event;
  while (1) {
    SDL_WaitEvent(&event);

    switch (event.type) {
      case SDL_QUIT: return 0;
      case SDL_USEREVENT: update_screen(); break;
      case SDL_USEREVENT + 1: return event.user.code;
      case SDL_KEYDOWN:
      case SDL_KEYUP:
        SDL_LockMutex(key_queue_lock);
        key_queue[key_r] = event;
        key_r = (key_r + 1) % KEY_QUEUE_LEN;
        assert(key_r != key_f);
        SDL_UnlockMutex(key_queue_lock);
        break;
    }
  }
  return 0;
}

static uint32_t timer_handler(uint32_t interval, void *param) {
  SDL_Event event;
  event.type = SDL_USEREVENT;
  SDL_PushEvent(&event);
  return interval;
}

static void audio_fill(void *userdata, uint8_t *stream, int len) {
  int nread = glibc_read(sb_fifo[0], stream, len);
  if (nread == -1) nread = 0;
  if (nread < len) memset(stream + nread, 0, len - nread);
}

static void open_display() {
  SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER);
#ifdef MODE_800x600
  SDL_CreateWindowAndRenderer(disp_w, disp_h, 0, &window, &renderer);
#else
  SDL_CreateWindowAndRenderer(disp_w * 2, disp_h * 2, 0, &window, &renderer);
#endif
  SDL_SetWindowTitle(window, "Simulated Nanos Application");
  SDL_AddTimer(1000 / FPS, timer_handler, NULL);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, disp_w, disp_h);

  fb_memfd = memfd_create("fb", 0);
  assert(fb_memfd != -1);
  int ret = ftruncate(fb_memfd, FB_SIZE);
  assert(ret == 0);
  fb = (uint32_t *)mmap(NULL, FB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fb_memfd, 0);
  assert(fb != (void *)-1);
  memset(fb, 0, FB_SIZE);
  lseek(fb_memfd, 0, SEEK_SET);
}

static void open_event() {
  key_queue_lock = SDL_CreateMutex();
  evt_fd = dup(dummy_fd);
}

static void open_audio() {
  int ret = pipe2(sb_fifo, O_NONBLOCK);
  assert(ret == 0);
  sbctl_fd = dup(dummy_fd);
  pipe_size = fcntl(sb_fifo[0], F_GETPIPE_SZ);
}

static const char* redirect_path(char *newpath, const char *path) {
  get_fsimg_path(newpath, path);
  if (0 == access(newpath, 0)) {
    fprintf(stderr, "Redirecting file open: %s -> %s\n", path, newpath);
    return newpath;
  }
  return path;
}

typedef struct {
  int argc;
  char **argv;
  char **envp;
} Mainargs;
static uint8_t main_backup_byte = 0;
extern "C" int main(int argc, char *argv[], char *envp[]);

static void set_sigtrap_handler(void (*handler)(int, siginfo_t *, void *)) {
  struct sigaction s;
  memset(&s, 0, sizeof(s));
  if (handler) {
    s.sa_flags = SA_SIGINFO;
    s.sa_sigaction = handler;
  } else {
    s.sa_flags = 0;
    s.sa_handler = SIG_DFL;
  }
  int ret = sigaction(SIGTRAP, &s, NULL);
  assert(ret == 0);
}

static void* app_thread_wrapper(void *arg) {
  Mainargs *args = (Mainargs *)arg;
  int ret = main(args->argc, args->argv, args->envp);

  // pass the exit code to the main thread
  SDL_Event event;
  event.type = SDL_USEREVENT + 1;
  event.user.code = ret;
  SDL_PushEvent(&event);

  return NULL;
}

static int main_thread(int argc, char *argv[], char *envp[]) {
  uint8_t *p = (uint8_t *)(void *)main;
  *p = main_backup_byte;
  int ret = mprotect((void *)((uintptr_t)main & ~0xffful), 4096, PROT_READ | PROT_EXEC);
  assert(ret == 0);

  set_sigtrap_handler(NULL);

  pthread_t tid;
  Mainargs args = { .argc = argc, .argv = argv, .envp = envp };
  // call the real main in the app thread
  ret = pthread_create(&tid, NULL, app_thread_wrapper, &args);
  assert(ret == 0);

  // handle SDL events in the main thread
  ret = handle_sdl_event();
  pthread_kill(tid, SIGTERM);
  return ret; // this will return to glibc
}

static void sigtrap_handler(int sig, siginfo_t *info, void *ucontext) {
  ((ucontext_t *)ucontext)->uc_mcontext.gregs[REG_RIP] = (uintptr_t)main_thread;
}

static void hijack_main() {
  int ret = mprotect((void *)((uintptr_t)main & ~0xffful), 4096, PROT_READ | PROT_WRITE | PROT_EXEC);
  assert(ret == 0);
  uint8_t *p = (uint8_t *)(void *)main;
  main_backup_byte = *p;
  *p = 0xcc;  // int3 instruction in x86

  set_sigtrap_handler(sigtrap_handler);
}

extern "C" FILE *fopen(const char *path, const char *mode);
extern "C" int open(const char *path, int flags, ...);
extern "C" ssize_t read(int fd, void *buf, size_t count);
extern "C" ssize_t write(int fd, const void *buf, size_t count);
extern "C" int execve(const char *filename, char *const argv[], char *const envp[]);

FILE *fopen(const char *path, const char *mode) {
  char newpath[512];
  if (glibc_fopen == NULL) {
    glibc_fopen = (FILE*(*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    assert(glibc_fopen != NULL);
  }
  return glibc_fopen(redirect_path(newpath, path), mode);
}

int open(const char *path, int flags, ...) {
  if (strcmp(path, "/proc/dispinfo") == 0) {
    return dispinfo_fd;
  } else if (strcmp(path, "/dev/events") == 0) {
    return evt_fd;
  } else if (strcmp(path, "/dev/fb") == 0) {
    return fb_memfd;
  } else if (strcmp(path, "/dev/sb") == 0) {
    return sb_fifo[1];
  } else if (strcmp(path, "/dev/sbctl") == 0) {
    return sbctl_fd;
  } else {
    char newpath[512];
    return glibc_open(redirect_path(newpath, path), flags);
  }
}

ssize_t read(int fd, void *buf, size_t count) {
  if (fd == dispinfo_fd) {
    // This does not strictly conform to `navy-apps/README.md`.
    // But it should be enough for real usage. Modify it if necessary.
    return snprintf((char *)buf, count, "WIDTH: %d\nHEIGHT: %d\n", disp_w, disp_h);
  } else if (fd == evt_fd) {
    int has_key = 0;
    SDL_Event ev = {};
    SDL_LockMutex(key_queue_lock);
    if (key_f != key_r) {
      ev = key_queue[key_f];
      key_f = (key_f + 1) % KEY_QUEUE_LEN;
      has_key = 1;
    }
    SDL_UnlockMutex(key_queue_lock);

    if (has_key) {
      SDL_Keysym k = ev.key.keysym;
      int keydown = ev.key.type == SDL_KEYDOWN;
      int scancode = k.scancode;

      const char *name = NULL;
      _KEYS(COND);
      if (name) return snprintf((char *)buf, count, "k%c %s\n", keydown ? 'd' : 'u', name);
    }
    return 0;
  } else if (fd == sbctl_fd) {
    // return the free space of sb_fifo
    int used;
    ioctl(sb_fifo[0], FIONREAD, &used);
    int free = pipe_size - used;
    return snprintf((char *)buf, count, "%d", free);
  }
  return glibc_read(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
  if (fd == sbctl_fd) {
    // open audio
    const int *args = (const int *)buf;
    assert(count >= sizeof(int) * 3);
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec spec = {0};
    spec.freq = args[0];
    spec.channels = args[1];
    spec.samples = args[2];
    spec.userdata = NULL;
    spec.callback = audio_fill;
    SDL_OpenAudio(&spec, NULL);
    SDL_PauseAudio(0);
    return count;
  }
  return glibc_write(fd, buf, count);
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
  char newpath[512];
  glibc_execve(redirect_path(newpath, filename), argv, envp);
  return -1;
}

struct Init {
  Init() {
    glibc_fopen = (FILE*(*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    assert(glibc_fopen != NULL);
    glibc_open = (int(*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
    assert(glibc_open != NULL);
    glibc_read = (ssize_t (*)(int fd, void *buf, size_t count))dlsym(RTLD_NEXT, "read");
    assert(glibc_read != NULL);
    glibc_write = (ssize_t (*)(int fd, const void *buf, size_t count))dlsym(RTLD_NEXT, "write");
    assert(glibc_write != NULL);
    glibc_execve = (int(*)(const char*, char *const [], char *const []))dlsym(RTLD_NEXT, "execve");
    assert(glibc_execve != NULL);

    dummy_fd = memfd_create("dummy", 0);
    assert(dummy_fd != -1);
    dispinfo_fd = dummy_fd;

    char *navyhome = getenv("NAVY_HOME");
    assert(navyhome);
    sprintf(fsimg_path, "%s/fsimg", navyhome);

    char newpath[512];
    get_fsimg_path(newpath, "/bin");
    setenv("PATH", newpath, 1); // overwrite the current PATH

    SDL_Init(0);
    if (!getenv("NWM_APP")) {
      open_display();
      open_event();
      hijack_main();
    }
    open_audio();
  }
  ~Init() {
  }
};

Init init;
