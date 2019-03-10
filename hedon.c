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

#define ierror(...) {fprintf(stderr, __VA_ARGS__); exit(1);}
#define error(...) {fprintf(stderr, __VA_ARGS__); fprintf(stderr, " in %s", last_def(globaldefs)->name); exit(1);}

#define spill_globaltype \
  TypeStack* tmpb = g_in; \
  TypeStack* tmpa = g_out; \
  g_in = init_typestack(8); \
  g_out = init_typestack(8);
#define restore_globaltype \
  g_in = tmpb; \
  g_out = tmpa;

#define WORDSIZE 8

typedef struct {
  struct _Type** data;
  size_t cap;
  size_t count;
} TypeStack;
typedef enum {
  TYPE_SINGLE,
  TYPE_UNION,
  TYPE_PARAM,
  TYPE_WRAP,
} TypeKind;
typedef struct _Type {
  TypeKind kind;
  char* name;
  size_t id;
  TypeStack* params;
} Type;

typedef struct {
  struct _Def** data;
  size_t cap;
  size_t count;
} Defs;
typedef struct {
  struct _Def* def;
  bool inout;
} Eff;
typedef struct {
  Eff* data;
  size_t cap;
  size_t count;
} Effs;
typedef struct {
  TypeStack* in;
  TypeStack* out;
} EffFreeze;
typedef struct _Def {
  char name[256];
  char* bufferaddr;
  uint8_t* wp;
  size_t codesize;
  Effs effects;
  EffFreeze* freeze;
  bool immediate;
  Defs traitwords;
  bool template;
} Def;

char* buffer;
char token[256];
uint8_t* cp;
uint8_t* sp;
uint8_t* dp;
bool state;
bool writestate;
bool tapplystate;

Defs globaldefs;
Defs localdefs;

TypeStack* i_in;
TypeStack* i_out;
TypeStack* g_in;
TypeStack* g_out;
size_t typeid;
Type* typet;
Type* intt;
Type* ptrt;

//
// prototypes
//

void parse_token();
void eval_token();

//
// type stack
//

Type* init_type(char* name, size_t id) {
  Type* t = malloc(sizeof(Type));
  t->kind = TYPE_SINGLE;
  t->name = name;
  t->id = id;
  t->params = NULL;
  return t;
}

Type* dup_type(Type* t) {
  Type* newt = malloc(sizeof(Type));
  *newt = *t;
  return newt;
}

Type* generate_type(char* name) {
  return init_type(name, typeid++);
}

bool eqtype(Type* a, Type* b) {
  if (a->id == b->id) return true;
  if (a->kind == TYPE_PARAM) return true;
  if (b->kind == TYPE_PARAM) return true;
  if (a->kind == TYPE_UNION) {
    for (int i=0; i<a->params->count; i++) {
      if (eqtype(a->params->data[i], b)) return true;
    }
  }
  return false;
}

TypeStack* init_typestack(size_t cap) {
  TypeStack* ls = malloc(sizeof(TypeStack));
  ls->data = malloc(sizeof(Type*)*cap);
  ls->cap = cap;
  ls->count = 0;
  return ls;
}

bool eq_typestack(TypeStack* a, TypeStack* b) {
  if (a->count != b->count) return false;
  for (int i=0; i<a->count; i++) {
    if (!eqtype(a->data[i], b->data[i])) return false;
  }
  return true;
}

TypeStack* dup_typestack(TypeStack* ts) {
  TypeStack* newts = init_typestack(ts->cap);
  memcpy(newts->data, ts->data, sizeof(Type*)*ts->count);
  newts->count = ts->count;
  return newts;
}

TypeStack* rev_typestack(TypeStack* ts) {
  TypeStack* newts = dup_typestack(ts);
  for (int i=0; i<ts->count; i++) {
    newts->data[i] = ts->data[ts->count-i-1];
  }
  return newts;
}

void dump_typestack(FILE* f, TypeStack* ts) {
  if (ts->count != 0) {
    Type* l = ts->data[0];
    fprintf(f, "%s", l->name);
  }
  for (int i=1; i<ts->count; i++) {
    Type* l = ts->data[i];
    fprintf(f, " %s", l->name);
  }
}

void push_type(TypeStack* ls, Type* l) {
  if (ls->cap <= ls->count) {
    while (ls->cap <= ls->count) ls->cap *= 2;
    ls->data = realloc(ls->data, sizeof(Type*)*ls->cap);
  }
  ls->data[ls->count++] = l;
}

Type* pop_type(TypeStack* ls) {
  return ls->data[--ls->count];
}

// union

Type* init_uniontype(char* name, size_t id) {
  Type* t = init_type(name, id);
  t->kind = TYPE_UNION;
  t->params = init_typestack(8);
  return t;
}

Type* init_paramtype(char* name, size_t id) {
  Type* t = init_type(name, id);
  t->kind = TYPE_PARAM;
  return t;
}

Type* init_wraptype(char* name, size_t id) {
  Type* t = init_type(name, id);
  t->kind = TYPE_WRAP;
  t->params = init_typestack(8);
  return t;
}

//
// Eff
//

Eff init_eff(Def* def, bool inout) {
  Eff eff;
  eff.def = def;
  eff.inout = inout;
  return eff;
}

Effs init_effs(size_t cap) {
  Effs effs;
  effs.data = malloc(sizeof(Eff)*cap);
  effs.cap = cap;
  effs.count = 0;
  return effs;
}

void add_eff(Effs* effs, Eff eff) {
  if (effs->cap <= effs->count) {
    while (effs->cap <= effs->count) effs->cap *= 2;
    effs->data = realloc(effs->data, sizeof(Eff)*effs->cap);
  }
  effs->data[effs->count++] = eff;
}

//
// Freeze
//

EffFreeze* freeze(TypeStack* b, TypeStack* a) {
  EffFreeze* fr = malloc(sizeof(EffFreeze));
  fr->in = rev_typestack(b);
  fr->out = dup_typestack(a);
  return fr;
}

bool can_freeze(TypeStack* ts) {
  for (int i=0; i<ts->count; i++) {
    if (ts->data[i]->kind != TYPE_SINGLE) return false;
  }
  return true;
}

//
// Definitions
//

Defs init_defs(size_t cap);

Def* init_def() {
  Def* def = malloc(sizeof(Def));
  def->effects = init_effs(16);
  def->freeze = NULL;
  def->bufferaddr = buffer;
  def->immediate = NULL;
  def->traitwords.data = NULL;
  def->template = false;
  return def;
}

bool is_trait(Def* def) {
  return def->traitwords.data != NULL;
}

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
  if (!writestate) return;
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
  sp -= 8;
  *(size_t*)sp = x;
}
size_t pop_x() {
  size_t x = *(size_t*)sp;
  sp += 8;
  return x;
}

void call_word(uint8_t* wp) {
  uint8_t* rbx;
  inasm("mov rbx, %1; mov rax, %2; call rax; mov %0, rbx;", "=r"(rbx) : "r"(sp), "r"(wp) : "rbx");
  sp = rbx;
}

void write_call_builtin(void* p) {
  write_hex(0x48, 0xb8); write_qword((size_t)&sp); // movabs rax, &sp;
  write_hex(0x48, 0x89, 0x18); // mov [rax], rbx
  write_hex(0x48, 0xb8); write_qword((size_t)p); // movabs rax, p
  write_hex(0xff, 0xd0); // call rax
}

void write_stack_increment(int inc) {
  write_hex(0x48, 0x81, 0xc3); write_lendian(-inc, 4); // add rbx, inc
}

size_t* write_x(size_t x) {
  write_hex(0x48, 0x83, 0xeb, 0x08); // sub rbx, 8
  write_hex(0x48, 0xb8); // movabs rax, x
  size_t* fixup = (size_t*)cp; write_qword(x);
  write_hex(0x48, 0x89, 0x03); // mov [rbx], rax
  return fixup;
}

void write_call_word(size_t wp) {
  write_hex(0x49, 0xb8); write_qword(wp); // movabs r8, wp
  write_hex(0x41, 0xff, 0xd0); // call r8
}

void expand_word(Def* def) {
  char* tmpbuf = buffer;
  buffer = def->bufferaddr;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    eval_token();
  }
  buffer = tmpbuf;
}

//
// interpreter types
//

void imm_push_type(Type* l) {
  push_type(i_out, l);
}

void global_push_type(Type* l) {
  if (state) push_type(g_out, l);
  else imm_push_type(l);
}

bool imm_pop_type(Type* l) {
  if (i_out->count == 0) {
    // if stack is empty at global state
    error("stack is empty, but expected %s value", l->name);
  }
  Type* sl = pop_type(i_out);
  if (!eqtype(l, sl)) error("unmatch %s type to %s", sl->name, l->name);
  return false;
}
bool global_pop_type(Type* l) {
  if (state) {
    if (g_out->count == 0) {
      // pop_type for word argument.
      // TODO: if uniontype,
      push_type(g_in, l);
      return true;
    }
    // consume current stack type.
    Type* sl = pop_type(g_out);
    if (!eqtype(l, sl)) error("unmatch %s type to %s", sl->name, l->name);
    if (l->kind == TYPE_PARAM) *l = *sl;
    if (sl->kind == TYPE_PARAM) *sl = *l;
    return false;
  } else {
    return imm_pop_type(l);
  }
}

void apply_in(TypeStack* in) {
  for (int i=0; i<in->count; i++) {
    global_pop_type(in->data[in->count-i-1]);
  }
}

void apply_out(TypeStack* out) {
  for (int i=0; i<out->count; i++) {
    global_push_type(out->data[i]);
  }
}

bool eq_in(TypeStack* a, TypeStack* in) {
  if (a->count < in->count) return false;
  for (int i=0; i<in->count; i++) {
    Type* l = a->data[a->count-i-1];
    Type* r = in->data[i];
    if (l->id == r->id) return true;
  }
  return false;
}

//
// interpreter
//

char bgetc() {
  char c = *buffer;
  buffer++;
  return c;
}
void bungetc() {
  buffer--;
}

void skip_spaces() {
  for (;;) {
    char c = bgetc();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    bungetc();
    break;
  }
}

void parse_token() {
  skip_spaces();
  int i;
  for (i=0; ; i++) {
    if (i >= 256-1) error("hedon word length should be <256.");
    char c = bgetc();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\0' || c == EOF) break;
    token[i] = c;
  }
  token[i] = '\0';
}

Def* solve_trait_word(Defs defs) {
  for (int i=0; i<defs.count; i++) {
    Def* def = defs.data[i];
    if (def->freeze == NULL) error("%s impl isn't freezing", def->name);
    if (eq_in(g_out, def->freeze->in)) return def;
  }
  return NULL;
}

void apply_effects(Def* def) {
  Def* wdef = last_def(globaldefs);
  if (def->freeze != NULL) {
    apply_in(def->freeze->in);
    apply_out(def->freeze->out);
    goto defer_eff;
  }
  for (int i=0; i<def->effects.count; i++) {
    Eff eff = def->effects.data[i];
    tapplystate = eff.inout;
    call_word(eff.def->wp);
  }
defer_eff:
  if (!writestate) {
    for (int i=0; i<def->effects.count; i++) {
      Eff eff = def->effects.data[i];
      add_eff(&wdef->effects, eff);
    }
  }
}

void add_int_effect() {
  if (writestate) return;
  Def* eff = search_def(&globaldefs, "Int");
  if (eff == NULL) error("undefined Int word");
  Def* wdef = last_def(globaldefs);
  add_eff(&wdef->effects, init_eff(eff, false));
}

#define BUILTIN_EFF(arr, d, inout, ...) \
  Def* d = last_def(globaldefs); \
  char* arr[] = {__VA_ARGS__}; \
  tapplystate = inout; \
  for (int i=0; i<sizeof(arr)/sizeof(char*); i++) { \
    Def* eff = search_def(&globaldefs, arr[i]); \
    if (eff == NULL) error("undefined %s eff-word", arr[i]); \
    call_word(eff->wp); \
    add_eff(&d->effects, init_eff(eff, inout)); \
  }
#define IN_EFF(...) BUILTIN_EFF(inarr, indef, true, __VA_ARGS__)
#define OUT_EFF(...) BUILTIN_EFF(outarr, outdef, false, __VA_ARGS__)
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
  Def* def = init_def();
  add_def(&globaldefs, def);
  strcpy(def->name, token);
  def->wp = cp;

  spill_globaltype;

  size_t p = (size_t)cp;
  state = true;
  // type
  writestate = false;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    if (strlen(token) == 0) error("require end of definition.");
    eval_token();
  }
  if (can_freeze(g_in) && can_freeze(g_out)) def->freeze = freeze(g_in, g_out);

  // store inferred typestack for write
  g_out = rev_typestack(g_in);
  g_in = init_typestack(8);

  // write with inferred type
  buffer = def->bufferaddr;
  writestate = true;
  for (;;) {
    parse_token();
    if (strcmp(token, ";") == 0) break;
    if (strlen(token) == 0) error("require end of definition.");
    eval_token();
  }
  write_hex(0xc3); // ret
  state = false;
  def->codesize = (size_t)cp - p;

  restore_globaltype;
}

void word_typeid() {
  write_x(typeid++);
}

void word_gentype() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)init_type(name, id));
}

void word_newtype() {
  char* name = last_def(globaldefs)->name;
  size_t id = typeid++;
  write_x((size_t)init_type(name, id));
}

void word_uniontype() {
  char* name = last_def(globaldefs)->name;
  size_t id = typeid++;
  write_x((size_t)init_uniontype(name, id));
}

void word_paramtype_in() {
  char* name = (char*)pop_x();
  push_x((size_t)init_paramtype(name, typeid++));
}

void word_paramtype() {
  char* name = last_def(globaldefs)->name;
  write_x((size_t)name);
  write_call_builtin(word_paramtype_in);
  // write_stack_increment(8);
}

void word_is() {
  Type* ut = (Type*)pop_x();
  Type* t = (Type*)pop_x();
  if (ut->kind != TYPE_UNION) error("%s type isn't union", ut->name);
  push_type(ut->params, t);
}

void word_eff_apply() {
  Type* t = (Type*)pop_x();
  if (tapplystate) global_pop_type(t);
  else global_push_type(t);
}

void word_eff_push() {
  Type* t = (Type*)pop_x();
  global_push_type(t);
}

void word_eff_drop() {
  Type* t = (Type*)pop_x();
  global_pop_type(t);
}

void word_eff_dup() {
  Type* t = (Type*)pop_x();
  global_pop_type(t);
  global_push_type(t);
  global_push_type(t);
}

void word_eff_save() {
  push_x((size_t)dup_typestack(g_in));
  push_x((size_t)dup_typestack(g_out));
}
void word_eff_load() {
  TypeStack* out = (TypeStack*)pop_x();
  TypeStack* in = (TypeStack*)pop_x();
  g_in = dup_typestack(in);
  g_out = dup_typestack(out);
}
void word_eff_check() {
  TypeStack* rout = (TypeStack*)pop_x();
  TypeStack* rin = (TypeStack*)pop_x();
  TypeStack* iout = (TypeStack*)pop_x();
  TypeStack* iin = (TypeStack*)pop_x();
  if (!eq_typestack(iin, rin)) {
    dump_typestack(stderr, iin);
    fprintf(stderr, " <-> ");
    dump_typestack(stderr, rin);
    error(" unmatch in-effect");
  }
  if (!eq_typestack(iout, rout)) {
    dump_typestack(stderr, iout);
    fprintf(stderr, " <-> ");
    dump_typestack(stderr, rout);
    error(" unmatch out-effect");
  }
}

void word_immediate() {
  Def* def = last_def(globaldefs);
  def->immediate = true;
}

void word_trait() {
  Def* def = last_def(globaldefs);
  def->traitwords = init_defs(32);
}

void word_impl() {
  Def* wdef = last_def(globaldefs);
  parse_token();
  Def* tdef = search_def(&globaldefs, token);
  if (tdef == NULL) error("undefined %s word", token);
  if (!is_trait(tdef)) error("couldn't impl %s to %s, that not trait", wdef->name, tdef->name);
  add_def(&tdef->traitwords, wdef);
}

void word_X() {
  parse_token(); // parse op code
  long x = strtol(token, NULL, 0);
  if (x == 0 && token[0] != '0') error("X syntax requires integer literal.");
  write_hex(x);
}

void word_create() {
  parse_token();
  Def* def = init_def();
  add_def(&globaldefs, def);
  OUT_EFF("Pointer"); 
  strcpy(def->name, token);
  def->wp = cp;
  def->codesize = (size_t)cp;
  write_x((size_t)dp);
  write_hex(0xc3); // ret
  def->codesize = (size_t)cp - def->codesize;
}

void word_does_in() {
  cp--;
  Def* def = (Def*)pop_x();
  uint8_t* pp = (uint8_t*)pop_x();
  for (uint8_t* p = pp; p<=def->wp+def->codesize; p++) {
    write_hex((size_t)*p);
  }
  last_def(globaldefs)->codesize += -1 + def->wp+def->codesize - pp;
}

void word_does() {
  size_t* fixup = write_x(0);
  write_x((size_t)last_def(globaldefs));
  write_call_builtin(word_does_in);
  write_stack_increment(-8);
  write_hex(0xc3); // ret
  *fixup = (size_t)cp;
}

void word_force_effects() {
  Def* def = last_def(globaldefs);
  def->freeze = NULL;
  Def* beffs[1024];
  Def* aeffs[1024];
  int bi = 0;
  int ai = 0;
  for (;;) {
    parse_token();
    if (strcmp(token, "--") == 0) break;
    if (strcmp(token, ")") == 0) goto defer_inout;
    Def* eff = search_def(&globaldefs, token);
    if (eff == NULL) error("undefined %s word", token);
    beffs[bi] = eff;
    bi++;
  }
  for (;;) {
    parse_token();
    if (strcmp(token, ")") == 0) goto defer_inout;
    Def* eff = search_def(&globaldefs, token);
    if (eff == NULL) error("undefined %s word", token);
    aeffs[ai] = eff;
    ai++;
  }
defer_inout:
  def->effects.count = 0;
  for (int i=0; i<bi; i++) {
    add_eff(&def->effects, init_eff(beffs[bi-i-1], true));
  }
  for (int i=0; i<ai; i++) {
    add_eff(&def->effects, init_eff(aeffs[i], false));
  }

  spill_globaltype;
  state = true;
  apply_effects(def);
  if (can_freeze(g_in) && can_freeze(g_out)) def->freeze = freeze(g_in, g_out);
  state = false;
  restore_globaltype;
}

void word_dump_tstack() {
  dump_typestack(stdout, rev_typestack(g_in));
  printf(" -- ");
  dump_typestack(stdout, g_out);
}

void word_dump_type() {
  parse_token();
  Def* def = search_def(&globaldefs, token);
  if (def == NULL) error("undefined %s word in dump-type", token);
  if (def->freeze != NULL) {
    dump_typestack(stdout, def->freeze->in);
    printf(" -- ");
    dump_typestack(stdout, def->freeze->out);
  } else {
    spill_globaltype;
    state = true;
    apply_effects(def);
    dump_typestack(stdout, rev_typestack(g_in));
    printf(" -- ");
    dump_typestack(stdout, g_out);
    state = false;
    restore_globaltype;
  }
}

void word_dump_effect() {
  parse_token();
  Def* def = search_def(&globaldefs, token);
  if (def == NULL) error("undefined %s word in dump-effect", token);
  for (int i=0; i<def->effects.count; i++) {
    if (def->effects.data[i].inout) {
      printf("%s:in ", def->effects.data[i].def->name);
    } else {
      printf("%s:out ", def->effects.data[i].def->name);
    }
  }
}

void word_dump_code() {
  parse_token();
  Def* def = search_def(&globaldefs, token);
  if (def == NULL) error("undefined %s word in dump-code", token);
  for (int i=0; i<def->codesize; i++) {
    printf("%02x", def->wp[i]);
  }
}

void word_strlit() {
  global_push_type(intt);
  size_t p = (size_t)dp;
  for (;;) {
    char c = bgetc();
    if (c == '"') break;
    if (c == '\\') c = bgetc();
    *dp = c;
    dp++;
  }
  write_x(p);
}

void word_rem() {
  for (;;) {
    if (bgetc() == '\n') break;
  }
}

void word_parse_token() {
  parse_token();
}

void word_token() {
  push_x((size_t)token);
}

void word_search_word() {
  char* name = (char*)pop_x();
  push_x((size_t)search_def(&globaldefs, name));
}

void word_word_code() {
  Def* def = (Def*)pop_x();
  push_x((size_t)def->wp);
}

void word_dp() {
  push_x((size_t)&dp);
}

void word_cp() {
  push_x((size_t)&cp);
}

void word_dot() {
  size_t x = pop_x();
  printf("%zd", x);
  fflush(stdout);
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
    if (def->immediate) {
      apply_effects(def);
      call_word(def->wp);
    } else if (def->template) {
      expand_word(def);
    // } else if (is_trait(def) && !state) {
    //   error("trait can't call on toplevel in currently");
    } else if (is_trait(def) && !writestate) {
      apply_effects(def);
    } else if (is_trait(def) && writestate) {
      Def* solved = solve_trait_word(def->traitwords);
      if (solved == NULL) {
        if (last_def(globaldefs)->freeze != NULL) error("unresolved %s trait word", def->name);
        last_def(globaldefs)->template = true;
      } else {
        write_call_word((size_t)solved->wp);
      }
      apply_effects(def);
    } else if (state) {
      apply_effects(def);
      write_call_word((size_t)def->wp);
    } else {
      apply_effects(def);
      call_word(def->wp);
    }
    return;
  }

  long x = strtol(token, NULL, 0);
  if (x != 0 || token[0] == '0') {
    global_push_type(intt);
    if (state) {
      add_int_effect();
      write_x(x);
    } else {
      push_x(x);
    }
    return;
  }

  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_IMM_WORD("trait", word_trait);
  BUILTIN_WORD("impl>", word_impl, 0, {});
  BUILTIN_IMM_WORD("!(", word_force_effects);
  BUILTIN_IMM_WORD("X", word_X);
  BUILTIN_WORD("create", word_create, 0, {});

  // builtin for type def
  // BUILTIN_WORD("typeid", word_typeid, 8, {ATYPE(intt)});
  // BUILTIN_WORD("gentype", word_gentype, -8, {BTYPE(ptrt, intt); ATYPE(typet)});
  BUILTIN_WORD("builtin.Type.t", word_type, 8, {});
  BUILTIN_WORD("builtin.Int.t", word_int, 8, {});
  BUILTIN_WORD("builtin.newtype", word_newtype, 8, {});
  BUILTIN_WORD("builtin.uniontype", word_uniontype, 8, {});
  BUILTIN_WORD("builtin.paramtype", word_paramtype, 8, {});
  BUILTIN_WORD("is", word_is, -16, {IN_EFF("Type", "Type")});

  // builtin twords
  BUILTIN_WORD("builtin.eff.push", word_eff_push, -8, {});
  BUILTIN_WORD("builtin.eff.drop", word_eff_drop, -8, {});
  BUILTIN_WORD("builtin.eff.dup", word_eff_dup, -8, {});
  BUILTIN_WORD("builtin.eff.apply", word_eff_apply, -8, {});
  BUILTIN_WORD("builtin.eff.save", word_eff_save, 16, {});
  BUILTIN_WORD("builtin.eff.load", word_eff_load, -16, {});
  BUILTIN_WORD("builtin.eff.check", word_eff_check, -32, {});

  // builtin words
  BUILTIN_IMM_WORD("does>", word_does);
  BUILTIN_IMM_WORD("s\"", word_strlit);
  BUILTIN_IMM_WORD("rem", word_rem);
  BUILTIN_WORD("parse-token", word_parse_token, 0, {});
  BUILTIN_WORD("token", word_token, 8, {OUT_EFF("Int")});
  BUILTIN_WORD("search-word", word_search_word, 0, {IN_EFF("Int"); OUT_EFF("Int")});
  BUILTIN_WORD("word-code", word_word_code, 0, {IN_EFF("Int"); OUT_EFF("Int")});
  BUILTIN_WORD("builtin.dp", word_dp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD("builtin.cp", word_cp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD(".", word_dot, -8, {IN_EFF("Int")});
  BUILTIN_WORD("cr", word_cr, 0, {});
  BUILTIN_WORD("dump-tstack", word_dump_tstack, 0, {});
  BUILTIN_WORD("dump-type", word_dump_type, 0, {});
  BUILTIN_WORD("dump-effect", word_dump_effect, 0, {});
  BUILTIN_WORD("dump-code", word_dump_code, 0, {});

  error("undefined %s word.", token);
}

//
// main
//

void startup(size_t buffersize, size_t cpsize, size_t spsize, size_t dpsize) {
  buffer = malloc(buffersize);
  cp = (uint8_t*)jit_memalloc(cpsize);
  sp = (uint8_t*)malloc(spsize) + spsize;
  dp = (uint8_t*)malloc(dpsize);
  state = false;
  writestate = true;

  globaldefs = init_defs(1024);
  localdefs = init_defs(1024);

  i_in = init_typestack(8);
  i_out = init_typestack(8);
  g_in = init_typestack(8);
  g_out = init_typestack(8);
  typeid = 0;
  typet = generate_type("Type");
  intt = generate_type("Int");
  ptrt = generate_type("Ptr");
}

void read_buffer(FILE* f) {
  char* bufferstart = buffer;
  for (;;) {
    char c = fgetc(f);
    if (c == EOF) break;
    *buffer = c;
    buffer++;
  }
  buffer = bufferstart;
}

void eval_file(FILE* f) {
  char* tmpbuf = buffer;
  buffer = malloc(1024*1024);
  read_buffer(f);
  for (;;) {
    parse_token();
    if (strlen(token) == 0) break;
    eval_token();
  }
  buffer = tmpbuf;
}

int main(int argc, char** argv) {
  startup(1024*1024, 1024*1024, 1024*10, 1024*10);

  for (int i=1; i<argc; i++) {
    if (strcmp(argv[i], "-l") == 0) {
      i++;
      if (i >= argc) ierror("require filename argument by -l option");
      eval_file(fopen(argv[i], "r"));
    } else if (strcmp(argv[i], "-c") == 0) {
      return 0;
    } else {
      ierror("unknown cmdline argument: %s", argv[i]);
    }
  }

  read_buffer(stdin);
  for (;;) {
    parse_token();
    if (strlen(token) == 0) break;
    eval_token();
  }

  return 0;
}
