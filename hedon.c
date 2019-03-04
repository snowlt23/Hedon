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

#define error(...) {fprintf(stderr, __VA_ARGS__); fprintf(stderr, " in %s", last_def(globaldefs)->name); exit(1);}

#define WORDSIZE 8

typedef struct {
  struct _Label** data;
  size_t cap;
  size_t count;
} LabelStack;
typedef struct _Label {
  char* name;
  size_t id;
  LabelStack ul;
} Label;
typedef struct {
  LabelStack before;
  LabelStack after;
} WordLabel;

typedef struct {
  char name[256];
  uint8_t* wp;
  WordLabel wl;
  bool immediate;
} Def;
typedef struct {
  Def** data;
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

LabelStack g_before;
LabelStack g_after;
size_t labelid;
Label* labell;
Label* intl;
Label* ptrl;

//
// prototypes
//

void eval_token();

//
// label stack
//

Label* init_label(char* name, size_t id) {
  Label* l = malloc(sizeof(Label));
  l->name = name;
  l->id = id;
  l->ul.data = NULL;
  return l;
}

Label* generate_label(char* name) {
  return init_label(name, labelid++);
}

bool eqlabel(Label* a, Label* b) {
  if (a->id == b->id) return true;
  if (a->ul.data != NULL) {
    for (int i=0; i<a->ul.count; i++) {
      Label* l = a->ul.data[i];
      if (eqlabel(l, b)) return true;
    }
  }
  return false;
}

LabelStack init_labelstack(size_t cap) {
  LabelStack ls;
  ls.data = malloc(sizeof(Label*)* cap);
  ls.cap = cap;
  ls.count = 0;
  return ls;
}

void push_label(LabelStack* ls, Label* l) {
  if (ls->cap <= ls->count) {
    while (ls->cap <= ls->count) ls->cap *= 2;
    ls->data = realloc(ls->data, sizeof(Label*)*ls->cap);
  }
  ls->data[ls->count++] = l;
}

Label* pop_label(LabelStack* ls) {
  return ls->data[--ls->count];
}

WordLabel init_wordlabel(LabelStack b, LabelStack a) {
  WordLabel wl;
  wl.before = b;
  wl.after = a;
  return wl;
}

// union

Label* init_union(char* name, size_t id) {
  Label* l = malloc(sizeof(Label));
  l->name = name;
  l->id = id;
  l->ul = init_labelstack(8);
  return l;
}

bool union_add(Label* ul, Label* l) {
  if (ul->ul.data == NULL) return false;
  push_label(&ul->ul, l);
  return true;
}

//
// Definitions
//

Defs init_defs(size_t cap) {
  Defs defs;
  defs.data = malloc(sizeof(Def*) * cap);
  defs.cap = cap;
  defs.count = 0;
  return defs;
}

void add_def(Defs* defs, Def* def) {
  if (defs->cap <= defs->count) {
    while (defs->cap <= defs->count) defs->cap *= 2;
    defs->data = realloc(defs->data, sizeof(Def*)*defs->cap);
  }
  defs->data[defs->count++] = def;
}

Def* search_def(Defs* defs, char* name) {
  for (int i=0; i<defs->count; i++) {
    if (strcmp(defs->data[i]->name, name) == 0) return defs->data[i];
  }
  return NULL;
}

Def* last_def(Defs defs) {
  return defs.data[defs.count - 1];
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

void write_x(size_t x) {
  write_hex(0x48, 0x83, 0xeb, 0x08); // sub rbx, 8
  write_hex(0x48, 0xb8); write_qword(x); // movabs rax, x
  write_hex(0x48, 0x89, 0x03); // mov [rbx], rax
}

void write_call_word(size_t wp) {
  write_hex(0x49, 0xb8); write_qword(wp); // movabs r8, wp
  write_hex(0x41, 0xff, 0xd0); // call r8
}

//
// interpreter labels
//

void global_push_label(Label* l) {
  push_label(&g_after, l);
}

void global_pop_label(Label* l) {
  if (g_after.count == 0 && !state) {
    // if stack is empty at global state
    error("stack is empty, but expected %s value", l->name);
  }
  if (g_after.count == 0) {
    // pop_label for word argument.
    push_label(&g_before, l);
    return;
  }
  // consume current stack label.
  Label* sl = pop_label(&g_after);
  if (!eqlabel(l, sl)) error("unmatch %s label to %s", sl->name, l->name);
}

void apply_before(LabelStack before) {
  for (int i=0; i<before.count; i++) {
    global_pop_label(before.data[i]);
  }
}

void apply_after(LabelStack after) {
  for (int i=0; i<after.count; i++) {
    global_push_label(after.data[i]);
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

#define BLABEL(...) \
  Label* barr[] = {__VA_ARGS__}; \
  for (int i=0; i<sizeof(barr)/sizeof(Label*); i++) global_pop_label(barr[i]);
#define ALABEL(...) \
  Label* aarr[] = {__VA_ARGS__}; \
  for (int i=0; i<sizeof(aarr)/sizeof(Label*); i++) global_push_label(aarr[i]);
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

void word_label() {
  push_x((size_t)labell);
}

void word_int() {
  push_x((size_t)intl);
}

void word_def() {
  parse_token(); // parse name
  Def* def = malloc(sizeof(Def));
  add_def(&globaldefs, def);
  strcpy(def->name, token);
  def->wp = cp;
  def->immediate = false;
  // save global labelstack
  LabelStack tmpb = g_before;
  LabelStack tmpa = g_after;
  // store word labelstack to global
  g_before = init_labelstack(8);
  g_after = init_labelstack(8);

  state = true;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    if (strlen(token) == 0) error("require end of definition.");
    eval_token();
  }
  write_hex(0xc3); // ret
  state = false;
  def->wl = init_wordlabel(g_before, g_after);
  // restore global labelstack
  g_before = tmpb;
  g_after = tmpa;
}

void word_labelid() {
  global_push_label(intl);
  write_x(labelid++);
}

void word_genlabel() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)init_label(name, id));
}

void word_genunion() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)init_union(name, id));
}

void word_label_imm() {
  global_push_label(labell);
  char* name = last_def(globaldefs)->name;
  size_t id = labelid++;
  write_x((size_t)init_label(name, id));
}

void word_union_imm() {
  global_push_label(labell);
  char* name = last_def(globaldefs)->name;
  size_t id = labelid++;
  write_x((size_t)init_union(name, id));
}

void word_is() {
  Label* ul = (Label*)pop_x();
  Label* l = (Label*)pop_x();
  if (!union_add(ul, l)) error("%s label isn't union", ul->name);
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

void word_wordlabel() {
  Def* def = last_def(globaldefs);
  def->wl.before.count = 0;
  def->wl.after.count = 0;
  for (;;) {
    parse_token();
    if (strcmp(token, "--") == 0) break;
    if (strcmp(token, ")") == 0) return;
    eval_token();
    global_pop_label(labell);
    Label* l = (Label*)pop_x();
    push_label(&def->wl.before, l);
  }
  for (;;) {
    parse_token();
    if (strcmp(token, ")") == 0) break;
    eval_token();
    global_pop_label(labell);
    Label* l = (Label*)pop_x();
    push_label(&def->wl.after, l);
  }
}

void word_dump_label() {
  parse_token();
  Def* def = search_def(&globaldefs, token);
  if (def == NULL) error("undefined %s word in dump-label.", token);
  for (int i=0; i<def->wl.before.count; i++) {
    Label* l = def->wl.before.data[i];
    printf("%s ", l->name);
  }
  printf("--");
  for (int i=0; i<def->wl.after.count; i++) {
    Label* l = def->wl.after.data[i];
    printf(" %s", l->name);
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
  Def* def = search_def(&globaldefs, token);
  if (def != NULL) {
    apply_before(def->wl.before);
    apply_after(def->wl.after);
    if (def->immediate) {
      call_word(def->wp);
    } else if (state) {
      write_call_word((size_t)def->wp);
    } else {
      call_word(def->wp);
    }
    return;
  }

  long x = strtol(token, NULL, 0);
  if (x != 0 || token[0] == '0') {
    global_push_label(intl);
    if (state) {
      write_x(x);
    } else {
      push_x(x);
    }
    return;
  }

  // builtin labels 
  BUILTIN_WORD("Label", word_label, -8, {ALABEL(labell)});
  BUILTIN_WORD("Int", word_int, -8, {ALABEL(labell)});

  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_IMM_WORD("labelid", word_label_imm);
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_IMM_WORD("(", word_wordlabel);
  BUILTIN_IMM_WORD("X", word_X);

  // builtin for label def
  BUILTIN_WORD("labelid", word_labelid, -8, {ALABEL(intl)});
  BUILTIN_WORD("genlabel", word_genlabel, 8, {BLABEL(ptrl, intl); ALABEL(labell)});
  BUILTIN_IMM_WORD("label", word_label_imm);
  BUILTIN_WORD("genunion", word_genunion, 8, {BLABEL(ptrl, intl); ALABEL(labell)});
  BUILTIN_IMM_WORD("union", word_union_imm);
  BUILTIN_WORD("is", word_is, 16, {BLABEL(labell, labell)});

  // builtin words
  BUILTIN_WORD(".", word_dot, 8, {BLABEL(intl)});
  BUILTIN_WORD("cr", word_cr, 0, {});
  BUILTIN_WORD("dump-label", word_dump_label, 0, {});
  BUILTIN_WORD("+", word_add, 8, {BLABEL(intl, intl); ALABEL(intl)});
  BUILTIN_WORD("-", word_sub, 8, {BLABEL(intl, intl); ALABEL(intl)});

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

  g_before = init_labelstack(8);
  g_after = init_labelstack(8);
  labelid = 0;
  labell = generate_label("Label");
  intl = generate_label("Int");
  ptrl = generate_label("Ptr");
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
