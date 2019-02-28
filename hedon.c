#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

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

#define WORDSIZE 8

typedef struct {
  char name[256];
  uint8_t* wp;
} Def;
typedef struct {
  Def* data;
  size_t cap;
  size_t count;
} Defs;

FILE* fin;
char token[256];
uint8_t* cp;
uint8_t* dp;
bool state;
Defs globaldefs;
Defs localdefs;

//
// prototypes
//

void eval_token();

//
// Definitions
//

void init_defs(Defs* defs, size_t cap) {
  defs->data = malloc(sizeof(Def) * cap);
  defs->cap = cap;
  defs->count = 0;
}
void add_def(Defs* defs, Def def) {
  if (defs->cap < defs->count) {
    while (defs->cap >= defs->count) defs->cap *= 2;
    defs->data = realloc(defs->data, sizeof(Def)*defs->cap);
  }
  defs->data[defs->count++] = def;
}
Def search_def(Defs* defs, char* name) {
  for (int i=0; i<defs->count; i++) {
    if (strcmp(defs->data[i].name, name) == 0) return defs->data[i];
  }
  return (Def){"\0"};
}

//
// codegen utilities
//

void write_to_cp(uint8_t* buf, size_t n) {
  memcpy(cp, buf, n);
  cp += n;
}

#define CONCAT(a, b) a ## b
#define write_hex2(id, ...) \
  uint8_t id[] = {__VA_ARGS__}; \
  write_to_cp(id, sizeof(id));
#define write_hex1(ln, ...) write_hex2(CONCAT(_hex, ln), __VA_ARGS__)
#define write_hex(...) write_hex1(__LINE__, __VA_ARGS__)

#define inasm(a, ...) asm volatile(".intel_syntax noprefix;" a "; .att_syntax;" : __VA_ARGS__)

#define take_byte(x, n) ((x >> (8*n)) & 0xff)

void write_lendian(size_t x, int n) {
  uint8_t buf[64] = {};
  for (int i=0; i<n; i++) {
    buf[i] = take_byte(x, i);
  }
  write_to_cp(buf, n);
}

void write_qword(size_t x) {
  write_lendian(x, 8);
}

void push_x(size_t x) {
  dp -= 8;
  *(size_t*)dp = x;
}
size_t pop_x() {
  size_t x = *(size_t*)dp;
  dp += 8;
  return x;
}

void call_word(uint8_t* wp) {
  uint8_t* rbx;
  inasm("mov rbx, %1; mov rax, %2; call rax; mov %0, rbx;", "=r"(rbx) : "r"(dp), "r"(wp) : "rbx");
  dp = rbx;
}

void write_call_builtin(void* p) {
  write_hex(0x48, 0xb8); write_qword((size_t)&dp); // movabs rax, &dp;
  write_hex(0x48, 0x89, 0x18); // mov [rax], rbx
  write_hex(0x48, 0xb8); write_qword((size_t)p); // movabs rax, p
  write_hex(0xff, 0xd0); // call rax
}

void write_stack_increment(int inc) {
  write_hex(0x48, 0x81, 0xc3); write_lendian(inc, 4); // add rbx, inc
}

//
// interpreter
//

void skip_spaces() {
  for (;;) {
    char c = fgetc(fin);
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      ungetc(c, fin);
      break;
    }
  }
}

void parse_token() {
  skip_spaces();
  int i;
  for (i=0; i<256; i++) {
    char c = fgetc(fin);
    if (c == ' ' || c == '\0' || c == EOF) break;
    if (c == '\n') {i--; continue;}
    token[i] = c;
  }
  token[i] = '\0';
}

#define BUILTIN_WORD(s, f, stackinc) \
  if (strcmp(token, s) == 0) { \
    if (state) { \
      write_call_builtin(f); \
      write_stack_increment(stackinc); \
    } else { \
      f(); \
    } \
    return; \
  }

void word_def() {
  parse_token(); // parse name
  Def def;
  strcpy(def.name, token);
  def.wp = cp;
  add_def(&globaldefs, def);

  state = true;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    if (strlen(token) == 0) error("require end of definition.");
    eval_token();
  }
  write_hex(0xc3); // ret
  state = false;
}

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
  Def def = search_def(&globaldefs, token);
  if (def.name[0] != '\0') {
    call_word(def.wp);
    return;
  }

  long x = strtol(token, NULL, 10);
  if (x != 0 || token[0] == '0') {
    if (state) {
      write_hex(0x48, 0x83, 0xeb, 0x08); // sub rbx, 8
      write_hex(0x48, 0xb8); write_qword(x); // movabs rax, x
      write_hex(0x48, 0x89, 0x03); // mov [rbx], rax
    } else {
      push_x(x);
    }
    return;
  }

  BUILTIN_WORD(":", word_def, 0);
  BUILTIN_WORD("+", word_add, 8);
  BUILTIN_WORD("-", word_sub, 8);

  error("undefined %s word.", token);
}

//
// main
//

void startup(size_t cpsize, size_t dpsize) {
  fin = stdin;
  cp = (uint8_t*)jit_memalloc(cpsize);
  dp = (uint8_t*)malloc(dpsize) + dpsize;
  state = false;
  init_defs(&globaldefs, 1024);
  init_defs(&localdefs, 1024);
}

int main() {
  startup(1024*1024, 1024*10);

  for (;;) {
    parse_token();
    if (strlen(token) == 0) break;
    eval_token();
  }

  printf("%zd", pop_x());
  return 0;
}
