#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef WIN32
  #include <windows.h>
  #define jit_memalloc(size) VirtualAlloc(0, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)
  #define jit_memfree(p, size) VirtualFree(p, size, MEM_DECOMMIT)
#else
  #include <sys/mman.h>
  #include <dlfcn.h>
  #define jit_memalloc(size) mmap(NULL, size, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0)
  #define jit_memfree(p, size) munmap(p, size)
#endif

#define error(...) {fprintf(stderr, __VA_ARGS__); exit(1);}

FILE* fin;
char token[256];
uint8_t* cp;
uint8_t* dp;
// Defs globaldefs;
// Defs localdefs;

void push_x(size_t x) {
  dp -= 8;
  *(size_t*)dp = x;
}
size_t pop_x() {
  size_t x = *(size_t*)dp;
  dp += 8;
  return x;
}

void parse_token() {
  int i;
  for (i=0; i<256; i++) {
    char c = fgetc(fin);
    if (c == ' ' || c == '\0' || c == EOF) break;
    if (c == '\n') {i--; continue;}
    token[i] = c;
  }
  token[i] = '\0';
}

#define BUILTIN_WORD(s, f) if (strcmp(token, s) == 0) {f(); return;}

void word_add() {
  size_t b = pop_x();
  size_t a = pop_x();
  push_x(a + b);
}
void word_sub() {
  size_t b = pop_x();
  size_t a = pop_x();
  push_x(a - b);
}

void eval_token() {
  long x = strtol(token, NULL, 10);
  if (token[0] == '0') {
    push_x(0);
    return;
  }
  if (x != 0) {
    push_x(x);
    return;
  }

  BUILTIN_WORD("+", word_add);
  BUILTIN_WORD("-", word_sub);

  error("undefined %s word.", token);
}

void startup(size_t dpsize) {
  fin = stdin;
  dp = (uint8_t*)malloc(dpsize) + dpsize;
}

int main() {
  startup(1024);

  for (;;) {
    parse_token();
    if (strlen(token) == 0) break;
    eval_token();
  }

  printf("%zd", pop_x());
  return 0;
}
