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
  char* name;
  size_t id;
  size_t size;
} Type;
typedef struct {
  Type** data;
  size_t cap;
  size_t count;
} TypeStack;
typedef struct {
  TypeStack before;
  TypeStack after;
} WordType;

typedef struct {
  char name[256];
  uint8_t* wp;
  WordType wt;
  bool immediate;
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

TypeStack g_before;
TypeStack g_after;
size_t typeid;
Type* typet;
Type* intt;

//
// prototypes
//

void eval_token();

//
// type stack
//

Type* init_type(char* name, size_t id, size_t size) {
  Type* t = malloc(sizeof(Type));
  t->name = name;
  t->id = id;
  t->size = size;
  return t;
}

Type* generate_type(char* name, size_t size) {
  return init_type(name, typeid++, size);
}

bool eqtype(Type* a, Type* b) {
  return a->id == b->id;
}

TypeStack init_typestack(size_t cap) {
  TypeStack ts;
  ts.data = malloc(sizeof(Type*)* cap);
  ts.cap = cap;
  ts.count = 0;
  return ts;
}

void push_type(TypeStack* ts, Type* t) {
  if (ts->cap < ts->count) {
    while (ts->cap >= ts->count) ts->cap *= 2;
    ts->data = realloc(ts->data, sizeof(Type*)*ts->cap);
  }
  ts->data[ts->count++] = t;
}

Type* pop_type(TypeStack* ts) {
  return ts->data[--ts->count];
}

WordType init_wordtype(TypeStack b, TypeStack a) {
  WordType wt;
  wt.before = b;
  wt.after = a;
  return wt;
}

//
// Definitions
//

Defs init_defs(size_t cap) {
  Defs defs;
  defs.data = malloc(sizeof(Def) * cap);
  defs.cap = cap;
  defs.count = 0;
  return defs;
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

Def* last_def(Defs defs) {
  return &defs.data[defs.count - 1];
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
// interpreter types
//

void global_push_type(Type* t) {
  push_type(&g_after, t);
}

void global_pop_type(Type* t) {
  if (g_after.count == 0 && !state) {
    // if stack is empty at global state
    error("stack is empty, but expected %s value", t->name);
  }
  if (g_after.count == 0) {
    // pop_type for word argument.
    push_type(&g_before, t);
    return;
  }
  // consume current stack type.
  Type* st = pop_type(&g_after);
  if (!eqtype(st, t)) error("unmatch %s type to %s", st->name, t->name);
}

void apply_before(TypeStack before) {
  for (int i=0; i<before.count; i++) {
    global_pop_type(before.data[i]);
  }
}

void apply_after(TypeStack after) {
  for (int i=0; i<after.count; i++) {
    global_push_type(after.data[i]);
  }
}

//
// interpreter
//

void skip_spaces() {
  for (;;) {
    char c = fgetc(fin);
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    ungetc(c, fin);
    break;
  }
}

void parse_token() {
  skip_spaces();
  int i;
  for (i=0; ; i++) {
    if (i >= 256-1) error("hedon word length should be <256.");
    char c = fgetc(fin);
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\0' || c == EOF) break;
    token[i] = c;
  }
  token[i] = '\0';
}

#define BTYPE(...) \
  Type* barr[] = {__VA_ARGS__}; \
  for (int i=0; i<sizeof(barr)/sizeof(Type*); i++) global_pop_type(barr[i]);
#define ATYPE(...) \
  Type* aarr[] = {__VA_ARGS__}; \
  for (int i=0; i<sizeof(aarr)/sizeof(Type*); i++) global_push_type(aarr[i]);
#define BUILTIN_WORD(s, f, stackinc, tdecl) \
  if (strcmp(token, s) == 0) { \
    tdecl; \
    if (state) { \
      write_call_builtin(f); \
      write_stack_increment(stackinc); \
    } else { \
      f(); \
    } \
    return; \
  }
#define BUILTIN_IMM_WORD(s, f) \
  if (strcmp(token, s) == 0) { \
    f(); \
    return; \
  }

void word_type() {
  push_x((size_t)typet);
}

void word_int() {
  push_x((size_t)intt);
}

void word_def() {
  parse_token(); // parse name
  Def def;
  strcpy(def.name, token);
  def.wp = cp;
  def.immediate = false;
  // save global typestack
  TypeStack tmpb = g_before;
  TypeStack tmpa = g_after;
  // store word typestack to global
  g_before = init_typestack(8);
  g_after = init_typestack(8);

  state = true;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    if (strlen(token) == 0) error("require end of definition.");
    eval_token();
  }
  write_hex(0xc3); // ret
  state = false;
  def.wt = init_wordtype(g_before, g_after);
  add_def(&globaldefs, def);
  // restore global typestack
  g_before = tmpb;
  g_after = tmpa;
}

void word_immediate() {
  Def* def = last_def(globaldefs);
  def->immediate = true;
}

void word_X() {
  parse_token(); // parse op code
  long x = strtol(token, NULL, 0);
  if (x == 0 && token[0] != '0') error("X syntax requires integer literal.");
  write_hex(x);
}

void word_wordtype() {
  Def* def = last_def(globaldefs);
  for (;;) {
    parse_token();
    if (strcmp(token, "--") == 0) break;
    if (strcmp(token, ")") == 0) return;
    eval_token();
    global_pop_type(typet);
    Type* t = (Type*)pop_x();
    push_type(&def->wt.before, t);
  }
  for (;;) {
    parse_token();
    if (strcmp(token, ")") == 0) break;
    eval_token();
    global_pop_type(typet);
    Type* t = (Type*)pop_x();
    push_type(&def->wt.after, t);
  }
}

void word_dump_type() {
  parse_token();
  Def def = search_def(&globaldefs, token);
  if (def.name[0] == '\0') error("undefined %s word in dump-type.", token);
  for (int i=0; i<def.wt.before.count; i++) {
    Type* t = def.wt.before.data[i];
    printf("%s ", t->name);
  }
  printf("--");
  for (int i=0; i<def.wt.after.count; i++) {
    Type* t = def.wt.after.data[i];
    printf(" %s", t->name);
  }
}

void word_dot() {
  size_t x = pop_x();
  printf("%zd", x);
}
void word_cr() {
  printf("\n");
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
    apply_before(def.wt.before);
    apply_after(def.wt.after);
    if (def.immediate) {
      call_word(def.wp);
    } else if (state) {
      write_hex(0x48, 0xb8); write_qword((size_t)def.wp); // movabs rax, wp
      write_hex(0xff, 0xd0); // call rax
    } else {
      call_word(def.wp);
    }
    return;
  }

  long x = strtol(token, NULL, 0);
  if (x != 0 || token[0] == '0') {
    global_push_type(intt);
    if (state) {
      write_hex(0x48, 0x83, 0xeb, 0x08); // sub rbx, 8
      write_hex(0x48, 0xb8); write_qword(x); // movabs rax, x
      write_hex(0x48, 0x89, 0x03); // mov [rbx], rax
    } else {
      push_x(x);
    }
    return;
  }

  // builtin types
  BUILTIN_WORD("Type", word_type, -8, {ATYPE(typet)});
  BUILTIN_WORD("Int", word_int, -8, {ATYPE(typet)});

  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_IMM_WORD("(", word_wordtype);
  BUILTIN_IMM_WORD("X", word_X);

  // builtin words
  BUILTIN_WORD(".", word_dot, 8, {BTYPE(intt)});
  BUILTIN_WORD("cr", word_cr, 0, {});
  BUILTIN_WORD("dump-type", word_dump_type, 0, {});
  BUILTIN_WORD("+", word_add, 8, {BTYPE(intt, intt); ATYPE(intt)});
  BUILTIN_WORD("-", word_sub, 8, {BTYPE(intt, intt); ATYPE(intt)});

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

  globaldefs = init_defs(1024);
  localdefs = init_defs(1024);

  g_before = init_typestack(8);
  g_after = init_typestack(8);
  typeid = 0;
  typet = generate_type("Type", 8);
  intt = generate_type("Int", 8);
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
