#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>

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

#define debug(...) {fprintf(stderr, "L%d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");}
#define ierror(...) {fprintf(stderr, __VA_ARGS__); exit(1);}

#define spill_typestack(spill) \
  Stack* itmpin ## spill = imm_typein; \
  Stack* itmpout ## spill = imm_typeout; \
  Stack* ctmpin ## spill = comp_typein; \
  Stack* ctmpout ## spill = comp_typeout; \
  imm_typein = new_stack(); \
  imm_typeout = new_stack(); \
  comp_typein = new_stack(); \
  comp_typeout = new_stack();
#define restore_typestack(spill) \
  imm_typein = itmpin ## spill; \
  imm_typeout = itmpout ## spill; \
  comp_typein = ctmpin ## spill; \
  comp_typeout = ctmpout ## spill;

#define spill_codegenstate(spill) bool spill = codegenstate
#define restore_codegenstate(spill) codegenstate = spill

#define IN_FOR(arr) for (int i=sizeof(arr)/sizeof(char*)-1; i>=0; i--)
#define OUT_FOR(arr) for (int i=0; i<sizeof(arr)/sizeof(char*); i++)
#define BUILTIN_EFF(arr, d, kind, fr, ...) \
  Def* d = last_def(); \
  char* arr[] = {__VA_ARGS__}; \
  fr { \
    Def* eff = search_def(arr[i]); \
    if (eff == NULL) error("undefined %s eff-word", arr[i]); \
    call_word(eff->wp); \
    apply_effects(eff); \
    if (kind == EFF_IMM) continue; \
    global_pop_type(typet); \
    Type* t = (Type*)pop_x(); \
    if (kind == EFF_IN) global_pop_type(t); \
    else global_push_type(t); \
    push(d->effects, new_eff(eff, kind)); \
  }
#define IN_EFF(...) BUILTIN_EFF(inarr, indef, EFF_IN, IN_FOR(inarr), __VA_ARGS__)
#define OUT_EFF(...) BUILTIN_EFF(outarr, outdef, EFF_OUT, OUT_FOR(outarr), __VA_ARGS__)
#define BUILTIN_WORD(s, f, stackinc, tdecl) \
  if (strcmp(token->name, s) == 0) { \
    tdecl; \
    if (state) { \
      write_call_builtin(f); \
      write_stack_increment(stackinc); \
    } else { \
      f(); \
    } \
    return true; \
  }
#define BUILTIN_IMM_WORD(s, f) \
  if (strcmp(token->name, s) == 0) { \
    f(); \
    return true; \
  }
#define BUILTIN_PARSE_WORD(s, f) \
  if (strcmp(t->name, s) == 0) { \
    t = f(); \
    if (t == NULL) return parse_token(); \
    else return t; \
  }

#define CELLSIZE 8
#define DEFAULT_STACKSIZE (CELLSIZE*1024)

typedef struct {
  uint8_t* s;
  uint8_t* p;
  size_t cap;
} Stack;

typedef enum {
  TOKEN_NAME,
  TOKEN_QUOT,
} TokenKind;
typedef struct {
  TokenKind kind;
  union {
    char* name;
    Stack* quot;
  };
} Token;

typedef enum {
  TYPE_SINGLE,
  TYPE_PARAM,
  TYPE_UNION,
  TYPE_REF,
} TypeKind;
typedef struct _Type {
  TypeKind kind;
  char* name;
  size_t id;
  struct _Type* ref;
  Stack* types;
} Type;

typedef enum {
  EFF_IN,
  EFF_OUT,
  EFF_IMM
} EffKind;
typedef struct {
  EffKind kind;
  struct _Def* def;
} Eff;
typedef struct _Def {
  char name[256];
  Token* quot;
  uint8_t* wp;
  size_t codesize;
  Stack* effects;
  Stack* freezein;
  Stack* freezeout;
  bool immediate;
  bool polymorphic;
  Stack* traitwords;
} Def;

typedef struct {
  char name[256];
  Stack* words;
} Vocab;

typedef struct {
  Stack* in;
  Stack* out;
} EffSave;

char* buffer;
uint8_t* dp;
uint8_t* cp;
Stack* data;
bool state;
bool codegenstate;
Token* intoken;
Stack* inquot;
int quotpos;
Vocab* globaldefs;
Stack* searchlist;
Stack* definitions;

Stack* imm_typein;
Stack* imm_typeout;
Stack* comp_typein;
Stack* comp_typeout;
size_t typeidcnt;
Type* quott;
Type* typet;
Type* intt;
Type* cstrt;

//
// prototypes
//

void call_word_asm(uint8_t** spp, uint8_t* wp);

void push_x(size_t x);
void imm_push_type(Type* t);
void imm_apply_effects(Def* def);
void call_word(uint8_t* wp);
Def* last_def();
Token* parse_token();
Def* search_def(char* name);
void print_quot(Stack* q);
void print_token(Token* t);
char* format_typestack(Stack* s);
void dump_typestack(FILE* f, Stack* s);
void typing_quot(Stack* t);
void codegen_quot(Stack* t);
void eval_quot(Stack* t);
void eval_token(Token* t);
void imm_eval_token(Token* t);

//
// Util
//

char* vformat(char* fmt, va_list ap) {
  char buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  return strdup(buf);
}

char* format(char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* s = vformat(fmt, ap);
  va_end(ap);
  return s;
}

void error(char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* s = vformat(fmt, ap);
  s = format("error in %s: %s at %s", last_def()->name, s, intoken->name);
  va_end(ap);
  Def* def = search_def("trap.error");
  if (def != NULL) {
    imm_push_type(cstrt);
    push_x((size_t)s);
    imm_apply_effects(def);
    call_word(def->wp);
  } else {
    fprintf(stderr, "%s", s);
    exit(1);
  }
}

//
// Stack
//

Stack* new_stack_cap(size_t size) {
  Stack* s = malloc(sizeof(Stack));
  s->s = malloc(size);
  s->p = s->s + size;
  s->cap = size;
  return s;
}

Stack* new_stack() {
  return new_stack_cap(DEFAULT_STACKSIZE);
}

Stack* dup_stack(Stack* s) {
  Stack* dups = new_stack_cap(s->cap);
  memcpy(dups->s, s->s, s->cap);
  dups->p -= s->s+s->cap - s->p;
  return dups;
}

#define pushT(s, T, v) \
  s->p -= sizeof(T); \
  *(T*)s->p = v;
#define popT(s, T) \
  (s->p += sizeof(T), *(T*)(s->p - sizeof(T)))

void push(Stack* s, void* v) {
  if (!(s->s <= s->p)) error("stack overflow");
  pushT(s, void*, v);
}
void* pop(Stack* s) {
  if (!(s->s+s->cap >= s->p)) error("stack underflow");
  return popT(s, void*);
}

#define stacklenT(s, T) ((s->s+s->cap - s->p) / sizeof(T))
#define getT(s, T, i) (*(T*)(s->s+s->cap - (i+1)*sizeof(T)))

size_t stacklen(Stack* s) {
  return stacklenT(s, void*);
}
void* get(Stack* s, size_t i) {
  return getT(s, void*, i);
}

Stack* rev_stack(Stack* s) {
  Stack* rev = new_stack_cap(s->cap);
  for (int i=stacklen(s)-1; i>=0; i--) {
    push(rev, get(s, i));
  }
  return rev;
}

//
// Token
//

Token* new_token(TokenKind kind) {
  Token* t = malloc(sizeof(Token));
  t->kind = kind;
  return t;
}

Token* new_token_name(char* name) {
  Token* t = new_token(TOKEN_NAME);
  t->name = name;
  return t;
}

Token* new_token_quot(Stack* s) {
  Token* t = new_token(TOKEN_QUOT);
  t->quot = s;
  return t;
}

//
// Type
//

Type* new_type(char* name, size_t id) {
  Type* t = malloc(sizeof(Type));
  t->kind = TYPE_SINGLE;
  t->name = name;
  t->id = id;
  t->ref = NULL;
  t->types = NULL;
  return t;
}

Type* dup_type(Type* t) {
  Type* newt = malloc(sizeof(Type));
  *newt = *t;
  return newt;
}

Type* generate_type(char* name) {
  return new_type(name, typeidcnt++);
}

Type* init_paramtype(char* name, size_t id) {
  Type* t = new_type(name, id);
  t->kind = TYPE_PARAM;
  return t;
}

Type* init_uniontype(char* name, size_t id) {
  Type* t = new_type(name, id);
  t->kind = TYPE_UNION;
  t->types = new_stack();
  return t;
}

Type* init_reftype(Type* ref) {
  Type* t = new_type(ref->name, ref->id);
  t->kind = TYPE_REF;
  t->ref = ref;
  return t;
}

Type* init_paramtype_ref(char* name, size_t id) {
  return init_reftype(init_paramtype(name, id));
}

int typeid(Type* t) {
  if (t->kind == TYPE_REF) return typeid(t->ref);
  return t->id;
}

char* typename(Type* t) {
  if (t->kind == TYPE_REF) return typename(t->ref);
  return t->name;
}

bool eqtype(Type* a, Type* b) {
  if (typeid(a) == typeid(b)) return true;
  if (a->kind == TYPE_PARAM) return true;
  if (b->kind == TYPE_PARAM) return true;
  if (a->kind == TYPE_REF) return eqtype(a->ref, b);
  if (b->kind == TYPE_REF) return eqtype(a, b->ref);
  if (a->kind == TYPE_UNION) {
    for (int i=0; i<stacklen(a->types); i++) {
      if (eqtype(get(a->types, i), b)) return true;
    }
  }
  if (b->kind == TYPE_UNION) {
    for (int i=0; i<stacklen(b->types); i++) {
      if (eqtype(a, get(b->types, i))) return true;
    }
  }
  return false;
}

bool eq_typestack(Stack* a, Stack* b) {
  if (stacklen(a) != stacklen(b)) return false;
  for (int i=0; i<stacklen(a); i++) {
    if (!eqtype(get(a, i), get(b, i))) return false;
  }
  return true;
}

//
// Eff
//

Eff* new_eff(Def* def, EffKind kind) {
  Eff* eff = malloc(sizeof(Eff));
  eff->def = def;
  eff->kind = kind;
  return eff;
}

bool is_polytype(Type* t) {
  if (t->kind == TYPE_REF) return is_polytype(t->ref);
  return t->kind != TYPE_SINGLE;
}

bool is_in_polytype(Type* t) {
  if (t->kind == TYPE_REF) return is_polytype(t->ref);
  return t->kind != TYPE_SINGLE && t->kind != TYPE_UNION;
}

bool is_polymorphic(Stack* s) {
  for (int i=0; i<stacklen(s); i++) {
    Type* t = get(s, i);
    if (is_polytype(t)) return true;
  }
  return false;
}

bool is_in_polymorphic(Stack* s) {
  for (int i=0; i<stacklen(s); i++) {
    Type* t = get(s, i);
    if (is_in_polytype(t)) return true;
  }
  return false;
}

Stack* freeze(Stack* s) {
  return dup_stack(s);
}

EffSave* save_inout(Stack* in, Stack* out) {
  EffSave* save = malloc(sizeof(EffSave));
  save->in = in;
  save->out = out;
  return save;
}

//
// Definitions
//

Def* new_def() {
  Def* def = malloc(sizeof(Def));
  def->effects = new_stack();
  def->immediate = NULL;
  def->traitwords = NULL;
  def->polymorphic = false;
  return def;
}

bool is_trait(Def* def) {
  return def->traitwords != NULL;
}

Def* search_def(char* s) {
  for (int i=stacklen(searchlist)-1; i>=0; i--) {
    Vocab* v = get(searchlist, i);
    for (int j=stacklen(v->words)-1; j>=0; j--) {
      Def* def = get(v->words, j);
      if (strcmp(def->name, s) == 0) return def;
    }
  }
  return NULL;
}

Def* last_def() {
  Vocab* v = get(searchlist, stacklen(searchlist)-1);
  return get(v->words, stacklen(v->words)-1);
}

//
// Vocabulary
//

Vocab* new_vocab(char* name) {
  Vocab* v = malloc(sizeof(Vocab));
  strncpy(v->name, name, 256);
  v->words = new_stack(CELLSIZE);
  return v;
}

Vocab* last_vocab() {
  return get(searchlist, stacklen(searchlist)-1);
}

void add_def(Def* def) {
  push(last_vocab()->words, def);
}

//
// codegen utilities
//

void write_to_cp(uint8_t* buf, size_t n) {
  if (!codegenstate) return;
  memcpy(cp, buf, n);
  cp += n;
}

#define CONCAT(a, b) a ## b
#define write_hex2(id, ...) \
  uint8_t id[] = {__VA_ARGS__}; \
  write_to_cp(id, sizeof(id));
#define write_hex1(ln, ...) write_hex2(CONCAT(_hex, ln), __VA_ARGS__)
#define write_hex(...) write_hex1(__LINE__, __VA_ARGS__)

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
  pushT(data, size_t, x);
}
size_t pop_x() {
  return popT(data, size_t);
}

void call_word(uint8_t* wp) {
  call_word_asm(&data->p, wp);
}

void write_call_builtin(void* p) {
  write_hex(0x48, 0xb8); write_qword((size_t)&data->p); // movabs rax, &sp;
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
  write_hex(0x49, 0xba); write_qword(wp); // movabs r10, wp
  write_hex(0x41, 0xff, 0xd2); // call r10
}

//
// interpreter types
//

void imm_push_type(Type* l) {
  push(imm_typeout, l);
}

void global_push_type(Type* l) {
  if (state) push(comp_typeout, l);
  else imm_push_type(l);
}

bool imm_pop_type(Type* l) {
  if (stacklen(imm_typeout) == 0) {
    // if stack is empty at global state
    error("stack is empty, but expected %s value", typename(l));
  }
  Type* sl = pop(imm_typeout);
  if (!eqtype(l, sl)) error("unmatch %s type to %s", typename(sl), typename(l));
  return false;
}

void replace_typeref(Type* t, Type* r) {
  if (t->kind == TYPE_REF && t->ref->kind == TYPE_REF) {
    replace_typeref(t->ref, r);
    return;
  }
  if (typeid(t) == typeid(r)) return;
  t->ref = r;
}

bool global_pop_type(Type* l) {
  if (state) {
    if (stacklen(comp_typeout) == 0) {
      // pop_type for word argument.
      push(comp_typein, l);
      return true;
    }
    // consume current stack type.
    Type* sl = pop(comp_typeout);
    if (!eqtype(l, sl)) error("unmatch %s type to %s", typename(sl), typename(l));
    if (is_polytype(l)) replace_typeref(l, sl);
    if (is_polytype(sl)) replace_typeref(sl, l);
    return false;
  } else {
    return imm_pop_type(l);
  }
}

void apply_in(Def* def, Stack* in) {
  for (int i=0; i<stacklen(in); i++) {
    Type* t = get(in, i);
    global_pop_type(t);
    Def* eff = search_def(t->name);
    if (eff == NULL) error("undefined %s word in apply-freeze", t->name);
    if (!codegenstate && state) push(def->effects, new_eff(eff, EFF_IN));
  }
}

void apply_out(Def* def, Stack* out) {
  for (int i=0; i<stacklen(out); i++) {
    Type* t = get(out, i);
    global_push_type(t);
    Def* eff = search_def(t->name);
    if (eff == NULL) error("undefined %s word in apply-freeze", t->name);
    if (!codegenstate && state) push(def->effects, new_eff(eff, EFF_OUT));
  }
}

bool eq_in(Stack* a, Stack* in) {
  if (stacklen(a) < stacklen(in)) return false;
  for (int i=0; i<stacklen(in); i++) {
    Type* l = get(a, stacklen(a)-i-1);
    Type* r = get(in, i);
    if (eqtype(l, r)) return true;
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

Token* parse_name() {
  skip_spaces();
  char token[256] = {};
  int i;
  bool instrlit = false;
  for (i=0; ; i++) {
    if (i >= 256-1) error("hedon word length should be <256.");
    char c = bgetc();
    if (c == ' ' && !instrlit) break;
    if (c == '\n' || c == '\r' || c == '\t' || c == '\0' || c == EOF) break;
    if (c == '"') instrlit = !instrlit;
    token[i] = c;
  }
  if (strlen(token) == 0) return NULL;
  return new_token_name(strdup(token));
}

Token* parse_quot() {
  Stack* s = new_stack();
  for (;;) {
    Token* t = parse_token();
    if (t->kind == TOKEN_NAME && strcmp(t->name, ";") == 0) break;
    if (t->kind == TOKEN_NAME && strcmp(t->name, "]") == 0) break;
    push(s, t);
  }
  return new_token_quot(s);
}

Token* parse_rem() {
  for (;;) {
    char c = bgetc();
    if (c == '\n') break;
  }
  return NULL;
}

Token* parse_token() {
  Token* t = parse_name();
  if (t == NULL) return NULL;
  BUILTIN_PARSE_WORD("[", parse_quot);
  BUILTIN_PARSE_WORD("rem", parse_rem);
  return t;
}

Def* solve_trait_word(Stack* defs) {
  for (int i=0; i<stacklen(defs); i++) {
    Def* def = get(defs, i);
    if (def->polymorphic) error("%s impl is polymorphic", def->name);
    assert(def->freezein != NULL);
    if (eq_in(comp_typeout, def->freezein)) return def;
  }
  return NULL;
}

void apply_effects(Def* def) {
  Def* wdef = last_def();
  if (!def->polymorphic) {
    assert(def->freezein != NULL);
    assert(def->freezeout != NULL);
    apply_in(wdef, def->freezein);
    apply_out(wdef, def->freezeout);
    return;
  }

  for (int i=0; i<stacklen(def->effects); i++) {
    Eff* eff = get(def->effects, i);
    spill_codegenstate(spill);
    codegenstate = true;
    apply_effects(eff->def);
    restore_codegenstate(spill);
    call_word(eff->def->wp);
    if (eff->kind == EFF_IMM) continue;
    global_pop_type(typet);
    Type* t = (Type*)pop_x();
    if (eff->kind == EFF_IN) global_pop_type(t);
    else global_push_type(t);
  }

  if (!codegenstate) {
    for (int i=0; i<stacklen(def->effects); i++) {
      Eff* eff = get(def->effects, i);
      push(wdef->effects, eff);
    }
  }
}

void imm_apply_effects(Def* def) {
  bool tmpstate = state;
  state = false;
  apply_effects(def);
  state = tmpstate;
}

void add_int_effect() {
  if (codegenstate) return;
  Def* eff = search_def("Int");
  if (eff == NULL) error("undefined Int word");
  Def* wdef = last_def();
  push(wdef->effects, new_eff(eff, EFF_OUT));
}

void add_cstr_effect() {
  if (codegenstate) return;
  Def* eff = search_def("Cstr");
  if (eff == NULL) error("undefined Cstr word");
  Def* wdef = last_def();
  push(wdef->effects, new_eff(eff, EFF_OUT));
}

void word_type_eff() {
  global_push_type(typet);
}

void word_quot() {
  push_x((size_t)quott);
}

void word_type() {
  push_x((size_t)typet);
}

void word_int() {
  push_x((size_t)intt);
}

void word_cstr() {
  push_x((size_t)cstrt);
}

void word_def() {
  Token* n = parse_token(); // parse name
  Def* def = new_def();
  add_def(def);
  strcpy(def->name, n->name);
  def->wp = cp;

  spill_typestack(spill);

  state = true;
  def->quot = parse_quot();

  // type
  typing_quot(def->quot->quot);
  def->freezein = freeze(comp_typein);
  def->freezeout = freeze(comp_typeout);
  def->polymorphic = is_in_polymorphic(comp_typein) || is_polymorphic(comp_typeout);
  // store inferred typestack for write
  comp_typeout = rev_stack(comp_typein);
  comp_typein = new_stack();

  // write with inferred type
  spill_codegenstate(cgspill);
  codegenstate = true;
  write_hex(0x48, 0x83, 0xec, 0x08); // sub rsp, 8
  codegen_quot(def->quot->quot);
  write_hex(0x48, 0x83, 0xc4, 0x08); // add rsp, 8
  write_hex(0xc3); // ret
  restore_codegenstate(cgspill);

  restore_typestack(spill);
  state = false;
}

void word_typeid() {
  write_x(typeidcnt++);
}

void word_gentype() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)new_type(name, id));
}

void word_newtype() {
  char* name = last_def()->name;
  size_t id = typeidcnt++;
  write_x((size_t)new_type(name, id));
}

void word_paramtype_in() {
  char* name = (char*)pop_x();
  push_x((size_t)init_paramtype_ref(name, typeidcnt++));
}

void word_paramtype() {
  char* name = last_def()->name;
  write_x((size_t)name);
  write_call_builtin(word_paramtype_in);
  // write_stack_increment(0);
}

void word_uniontype() {
  char* name = last_def()->name;
  size_t id = typeidcnt++;
  write_x((size_t)init_uniontype(name, id));
}

void word_is() {
  Type* ut = (Type*)pop_x();
  if (ut->types == NULL) error("%s isn't uniontype", ut->name);
  Type* t = (Type*)pop_x();
  push(ut->types, t);
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

void word_eff_save_out() {
  push_x((size_t)dup_stack(comp_typeout));
}
void word_eff_load_out() {
  comp_typeout = (Stack*)pop_x();
}

void word_eff_save() {
  push_x((size_t)save_inout(dup_stack(comp_typein), dup_stack(comp_typeout)));
}

void word_eff_load() {
  EffSave* save = (EffSave*)pop_x();
  comp_typein = save->in;
  comp_typeout = save->out;
}

void word_eff_check() {
  EffSave* b = (EffSave*)pop_x();
  EffSave* a = (EffSave*)pop_x();
  if (!eq_typestack(a->in, b->in)) error("%s <-> %s unmatch in-effect", format_typestack(a->in), format_typestack(b->in));
  if (!eq_typestack(a->out, b->out)) error("%s <-> %s unmatch out-effect", format_typestack(a->out), format_typestack(b->out));
}

void word_immediate() {
  Def* def = last_def();
  def->immediate = true;
}

void word_trait() {
  Def* def = last_def();
  def->traitwords = new_stack();
}

void word_impl() {
  Def* wdef = last_def();
  Token* t = parse_token();
  Def* tdef = search_def(t->name);
  if (tdef == NULL) error("undefined %s word", t->name);
  if (!is_trait(tdef)) error("couldn't impl %s to %s, that not trait", wdef->name, tdef->name);
  push(tdef->traitwords, wdef);
}

void word_X() {
  imm_pop_type(quott);
  Stack* q = (Stack*)pop_x();
  for (int i=0; i<stacklen(q); i++) {
    Token* t = get(q, i);
    long x = strtol(t->name, NULL, 0);
    if (x == 0 && t->name[0] != '0') error("X syntax requires integer literal.");
    write_hex(x);
  }
}

void word_eff_attach() {
  Def* def = last_def();
  def->freezein = NULL;
  def->freezeout = NULL;
  def->polymorphic = true;
  def->effects = new_stack();
  Token* t = parse_token();
  Def* eff = search_def(t->name);
  if (eff == NULL) error("undefined %s word", t->name);
  push(def->effects, new_eff(eff, EFF_IMM));
}

void word_eff_tokenattach() {
  Def* def = last_def();
  def->freezein = NULL;
  def->freezeout = NULL;
  def->polymorphic = true;
  def->effects = new_stack();
  Token* t = (Token*)pop_x();
  Def* eff = search_def(t->name);
  if (eff == NULL) error("undefined %s word", t->name);
  push(def->effects, new_eff(eff, EFF_IMM));
}

void word_freeze_eff(Def* def) {
  spill_typestack(spill);
  state = true;
  apply_effects(def);
  def->freezein = freeze(comp_typein);
  def->freezeout = freeze(comp_typeout);
  def->polymorphic = is_in_polymorphic(comp_typein) || is_polymorphic(comp_typeout);
  state = false;
  restore_typestack(spill);
}

void word_force_effects() {
  Def* def = last_def();
  def->freezein = NULL;
  def->freezeout = NULL;
  def->polymorphic = true;
  def->effects = new_stack();
  EffKind inout = EFF_IN;
  for (;;) {
    Token* t = parse_token();
    if (t->kind == TOKEN_NAME && strcmp(t->name, "--") == 0) {
      def->effects = rev_stack(def->effects);
      inout = EFF_OUT;
      continue;
    }
    if (t->kind == TOKEN_NAME && strcmp(t->name, "]") == 0) break;
    Def* eff = search_def(t->name);
    if (eff == NULL) error("undefined %s word", t->name);
    push(def->effects, new_eff(eff, inout));
  }
  if (inout == EFF_IN) def->effects = rev_stack(def->effects);

  word_freeze_eff(def);
}

char* format_typestack(Stack* s) {
  Type* tfirst = get(s, 0);
  char buf[2048];
  char* p = buf;
  if (stacklen(s) != 0) p += snprintf(p, sizeof(buf)-(p-buf), "%s", typename(tfirst));
  else snprintf(p, sizeof(buf), "<>");
  for (int i=1; i<stacklen(s); i++) {
    Type* t = get(s, i);
    p += snprintf(p, sizeof(buf)-(p-buf), " %s", typename(t));
  }
  return strdup(buf);
}

void word_dump_type() {
  Token* t = parse_token();
  Def* def = search_def(t->name);
  if (def == NULL) error("undefined %s word in dump-type", t->name);
  if (def->freezein == NULL || def->freezeout == NULL) {
    spill_typestack(spill);
    state = true;
    apply_effects(def);
    printf("%s -- %s", format_typestack(rev_stack(comp_typein)), format_typestack(comp_typeout));
    state = false;
    restore_typestack(spill);
  } else {
    printf("%s -- %s", format_typestack(rev_stack(def->freezein)), format_typestack(def->freezeout));
  }
  fflush(stdout);
}

void word_rem() {
  for (;;) {
    if (bgetc() == '\n') break;
  }
}

void word_parse_token() {
  if (quotpos != -1) {
    push_x((size_t)get(inquot, ++quotpos));
  } else {
    // toplevel parse
    push_x((size_t)parse_token());
  }
}

void word_search_word() {
  char* name = (char*)pop_x();
  push_x((size_t)search_def(name));
}

void word_create_word() {
  char* n = (char*)pop_x();
  Def* def = new_def();
  strcpy(def->name, n);
  def->polymorphic = true;
  def->wp = cp;
  bool tmpstate = state;
  state = true;
  write_hex(0xc3); // ret
  state = tmpstate;
  push_x((size_t)def);
}

void word_set_code() {
  Stack* q = (Stack*)pop_x();
  Def* def = (Def*)pop_x();
  def->wp = cp;
  bool tmpstate = state;
  state = true;
  eval_quot(q);
  write_hex(0xc3); // ret
  state = tmpstate;
  push_x((size_t)def);
}

void word_add_impl() {
  char* to = (char*)pop_x();
  Def* def = (Def*)pop_x();
  Def* trait = search_def(to);
  if (trait == NULL) error("undefined %s word in >>impl", to);
  push(trait->traitwords, def);
  push_x((size_t)def);
}

void word_set_eff() {
  Stack* eff = (Stack*)pop_x();
  Def* def = (Def*)pop_x();
  def->effects = eff;
  word_freeze_eff(def);
  push_x((size_t)def);
}

void word_add_to_vocab() {
  Def* def = (Def*)pop_x();
  add_def(def);
}

void word_create_eff() {
  push_x((size_t)new_stack());
}

void word_add_effin() {
  Def* def = (Def*)pop_x();
  Stack* eff = (Stack*)pop_x();
  push(eff, new_eff(def, EFF_IN));
  push_x((size_t)eff);
}

void word_add_effout() {
  Def* def = (Def*)pop_x();
  Stack* eff = (Stack*)pop_x();
  push(eff, new_eff(def, EFF_OUT));
  push_x((size_t)eff);
}

void word_create_vocab() {
  char* name = (char*)pop_x();
  push_x((size_t)new_vocab(name));
}

void word_add_def() {
  Def* def = (Def*)pop_x();
  Vocab* v = (Vocab*)pop_x();
  push(v->words, def);
  push_x((size_t)v);
}

void word_also() {
  Vocab* v = (Vocab*)pop_x();
  push(searchlist, v);
}

void word_previous() {
  push_x((size_t)pop(searchlist));
}

void word_definitions() {
  push(definitions, (Vocab*)pop_x());
}

void word_as_type() {
  Def* def = (Def*)pop_x();
  call_word(def->wp);
}

void word_token_name() {
  Token* t = (Token*)pop_x();
  push_x((size_t)t->name);
}

void print_quot(Stack* q) {
  printf("[ ");
  for (int i=0; i<stacklen(q); i++) {
    Token* t = get(q, i);
    print_token(t);
    printf(" ");
  }
  printf("]");
}
void print_token(Token* t) {
  if (t->kind == TOKEN_NAME) {
    printf("%s", t->name);
  } else {
    print_quot(t->quot);
  }
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
void word_dotc() {
  char x = (char)pop_x();
  putchar(x);
  fflush(stdout);
}
void word_dots() {
  char* x = (char*)pop_x();
  printf("%s", x);
  fflush(stdout);
}
void word_dotq() {
  Stack* x = (Stack*)pop_x();
  print_quot(x);
  fflush(stdout);
}
void word_cr() {
  printf("\n");
  fflush(stdout);
}

void word_op() {
  write_hex((uint8_t)pop_x());
}
void word_fixup_op() {
  if (!codegenstate) return;
  uint8_t* p = (uint8_t*)pop_x();
  uint8_t x = (uint8_t)pop_x();
  *p = x;
}

void word_compile() {
  Token* tmpintoken = intoken;
  Stack* q = (Stack*)pop_x();
  eval_quot(q);
  intoken = tmpintoken;
}

void word_postcompile() {
  imm_pop_type(quott);
  Stack* q = (Stack*)pop_x();
  write_x((size_t)q);
  write_call_builtin(word_compile);
  write_stack_increment(-8);
}

void word_literal() {
  size_t x = pop_x();
  char buf[256] = {};
  snprintf(buf, 256, "%zd", x);
  Token* t = new_token_name(strdup(buf));
  push_x((size_t)t);
}

void word_quot_to_token() {
  Stack* q = (Stack*)pop_x();
  push_x((size_t)new_token_quot(q));
}

void word_new_quot() {
  Stack* q = new_stack();
  push_x((size_t)q);
}

void word_push() {
  Stack* q = (Stack*)pop_x();
  Token* t = (Token*)pop_x();
  push(q, t);
}

void word_combine() {
  Stack* b = (Stack*)pop_x();
  Stack* a = (Stack*)pop_x();
  Stack* q = new_stack();
  for (int i=0; i<stacklen(a); i++) {
    push(q, get(a, i));
  }
  for (int i=0; i<stacklen(b); i++) {
    push(q, get(b, i));
  }
  push_x((size_t)(q));
}

void word_postquot() {
  imm_pop_type(quott);
  global_push_type(quott);
  Stack* q = (Stack*)pop_x();
  write_x((size_t)q);
}

void* dlopen_x(char* libname, size_t flag) {
  return dlopen(libname, flag);
}
void* dlsym_x(void* h, char* symname) {
  return dlsym(h, symname);
}

void word_dlopen() {
  push_x((size_t)&dlopen_x);
}
void word_dlsym() {
  push_x((size_t)&dlsym_x);
}
void word_dlclose() {
  push_x((size_t)&dlclose);
}

size_t call1(size_t a) {
  return a;
}
void word_call1() {
  push_x((size_t)&call1);
}
size_t call2(size_t a, size_t b) {
  return a - b;
}
void word_call2() {
  push_x((size_t)&call2);
}
size_t call6(size_t a, size_t b, size_t c, size_t d, size_t e, size_t f) {
  return a - b - c - d - e - f;
}
void word_call6() {
  push_x((size_t)&call6);
}

void expand_word(Def* def) {
  eval_quot(def->quot->quot);
}

void typing_quot(Stack* s) {
  spill_codegenstate(spill);
  codegenstate = false;
  eval_quot(s);
  restore_codegenstate(spill);
}

void codegen_quot(Stack* s) {
  spill_codegenstate(spill);
  codegenstate = true;
  eval_quot(s);
  restore_codegenstate(spill);
}

void eval_quot(Stack* s) {
  Stack* tmpinquot = inquot;
  int tmpquotpos = quotpos;
  inquot = s;
  for (quotpos=0; quotpos<stacklen(inquot); quotpos++) {
    Token* t = get(inquot, quotpos);
    eval_token(t);
  }
  quotpos = tmpquotpos;
  inquot = tmpinquot;
}

void eval_def(Def* def) {
  if (def->immediate) {
    imm_apply_effects(def);
    call_word(def->wp);
  } else if (is_trait(def) && !state) {
    error("trait can't call on toplevel in currently"); // TODO: trait-call on toplevel
  } else if (is_trait(def) && !codegenstate) {
    apply_effects(def);
  } else if (is_trait(def) && codegenstate) {
    Def* solved = solve_trait_word(def->traitwords);
    if (solved == NULL) {
      if (!last_def()->polymorphic) error("unresolved %s trait word", def->name);
    } else {
      write_call_word((size_t)solved->wp);
    }
    apply_effects(def);
  } else if (def->polymorphic) {
    expand_word(def);
  } else if (state) {
    apply_effects(def);
    write_call_word((size_t)def->wp);
  } else {
    imm_apply_effects(def);
    call_word(def->wp);
  }
}

void eval_strsyntax(Token* token) {
  global_push_type(cstrt);
  char* s = token->name+1;
  size_t p = (size_t)dp;
  for (;;) {
    char c = *s;
    s++;
    if (c == '"') break;
    if (c == '\\') {c = *s; s++;};
    *dp = c;
    dp++;
  }
  *dp = '\0';
  dp++;
  if (state) {
    add_cstr_effect();
    write_x(p);
  } else {
    push_x(p);
  }
}

bool eval_vocabsyntax(Token* token) {
  char* n = token->name+1;
  while (*n != '.' && *n != '\0') n++;
  n++;
  char vn[255] = {};
  strncpy(vn, token->name+1, (int)(n-token->name-2));
  for (int i=0; i<stacklen(definitions); i++) {
    Vocab* v = get(definitions, i);
    if (strcmp(v->name, vn) != 0) continue;
    for (int i=stacklen(v->words)-1; i>=0; i--) {
      Def* def = get(v->words, i);
      if (strcmp(def->name, n) == 0) {
        eval_def(def);
        return true;
      }
    }
  }
  return false;
}

bool eval_builtinwords(Token* token) {
  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_IMM_WORD("trait", word_trait);
  BUILTIN_WORD("impl", word_impl, 0, {});
  BUILTIN_IMM_WORD("![", word_force_effects);
  BUILTIN_IMM_WORD("eff.attach", word_eff_attach);
  BUILTIN_IMM_WORD("X", word_X);

  // builtin for type def
  BUILTIN_IMM_WORD("builtin.Type.eff", word_type_eff);
  BUILTIN_WORD("builtin.Quot", word_quot, 8, {});
  BUILTIN_WORD("builtin.Type", word_type, 8, {});
  BUILTIN_WORD("builtin.Int", word_int, 8, {});
  BUILTIN_WORD("builtin.Cstr", word_cstr, 8, {});
  BUILTIN_WORD("builtin.newtype", word_newtype, 8, {});
  BUILTIN_WORD("builtin.paramtype", word_paramtype, 8, {});
  BUILTIN_WORD("builtin.uniontype", word_uniontype, 8, {});
  BUILTIN_WORD("is", word_is, -16, {IN_EFF("Type", "Type")});

  // effect words
  BUILTIN_WORD("builtin.eff.push", word_eff_push, -8, {});
  BUILTIN_WORD("builtin.eff.drop", word_eff_drop, -8, {});
  BUILTIN_WORD("builtin.eff.dup", word_eff_dup, -8, {});
  BUILTIN_WORD("builtin.eff.save-out", word_eff_save_out, 8, {});
  BUILTIN_WORD("builtin.eff.load-out", word_eff_load_out, -8, {});
  BUILTIN_WORD("builtin.eff.save", word_eff_save, 8, {});
  BUILTIN_WORD("builtin.eff.load", word_eff_load, -8, {});
  BUILTIN_WORD("builtin.eff.check", word_eff_check, -16, {});

  // word control
  BUILTIN_WORD("parse-token", word_parse_token, 8, {OUT_EFF("Token")});
  BUILTIN_WORD("token-name", word_token_name, 0, {IN_EFF("Token"); OUT_EFF("Cstr")});

  BUILTIN_WORD("search-word", word_search_word, 0, {IN_EFF("Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD("<word>", word_create_word, 0, {IN_EFF("Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD(">>code", word_set_code, -8, {IN_EFF("Word", "Quot"); OUT_EFF("Word")});
  BUILTIN_WORD(">>impl", word_add_impl, -8, {IN_EFF("Word", "Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD(">>eff", word_set_eff, -8, {IN_EFF("Word", "Eff"); OUT_EFF("Word")});
  BUILTIN_WORD(">vocab", word_add_to_vocab, -8, {IN_EFF("Word")});
  BUILTIN_WORD("as-type", word_as_type, 0, {IN_EFF("Word"); OUT_EFF("Type")});

  BUILTIN_WORD("<eff>", word_create_eff, 8, {OUT_EFF("Eff")});
  BUILTIN_WORD(">>eff.in", word_add_effin, -8, {IN_EFF("Eff", "Word"); OUT_EFF("Eff")});
  BUILTIN_WORD(">>eff.out", word_add_effout, -8, {IN_EFF("Eff", "Word"); OUT_EFF("Eff")});

  BUILTIN_WORD("<vocab>", word_create_vocab, 0, {IN_EFF("Cstr"); OUT_EFF("Vocab")});
  BUILTIN_WORD(">>def", word_add_def, -8, {IN_EFF("Vocab", "Word"); OUT_EFF("Vocab")});
  BUILTIN_WORD("also", word_also, -8, {IN_EFF("Vocab")});
  BUILTIN_WORD("previous", word_previous, 8, {OUT_EFF("Vocab")});
  BUILTIN_WORD("definitions", word_definitions, -8, {IN_EFF("Vocab")});

  BUILTIN_WORD("builtin.dp", word_dp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD("builtin.cp", word_cp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD(".", word_dot, -8, {IN_EFF("Int")});
  BUILTIN_WORD(".c", word_dotc, -8, {IN_EFF("Int")});
  BUILTIN_WORD(".s", word_dots, -8, {IN_EFF("Cstr")});
  BUILTIN_WORD(".q", word_dotq, -8, {IN_EFF("Quot")});
  BUILTIN_WORD("cr", word_cr, 0, {});

  // op
  BUILTIN_WORD("op", word_op, -8, {IN_EFF("Int")});
  BUILTIN_WORD("fixup-op", word_fixup_op, -16, {IN_EFF("Int", "Pointer")});

  // quot
  BUILTIN_WORD("compile", word_compile, -8, {IN_EFF("Quot")});
  BUILTIN_WORD("literal", word_literal, 0, {IN_EFF("Int"); OUT_EFF("Token")});
  BUILTIN_WORD("quot->token", word_quot_to_token, 0, {IN_EFF("Quot"); OUT_EFF("Token")});
  BUILTIN_WORD("quot", word_new_quot, 8, {OUT_EFF("Quot")});
  BUILTIN_WORD("push", word_push, -16, {IN_EFF("Token", "Quot");});
  BUILTIN_WORD("combine", word_combine, -8, {IN_EFF("Quot", "Quot"); OUT_EFF("Quot")});
  BUILTIN_IMM_WORD("postquot", word_postquot);
  BUILTIN_IMM_WORD("postcompile", word_postcompile);

  // dump
  BUILTIN_WORD("dump-type", word_dump_type, 0, {});

  // cffi
  BUILTIN_WORD("builtin.c.dlopen", word_dlopen, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlsym", word_dlsym, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlclose", word_dlclose, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call1", word_call1, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call2", word_call2, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call6", word_call6, 8, {OUT_EFF("Pointer")});

  return false;
}

void eval_token(Token* token) {
  intoken = token;
  if (token->kind == TOKEN_QUOT) {
    imm_push_type(quott);
    push_x((size_t)token->quot);
    return;
  }
  // debug("%s", token->name);

  Def* def = search_def(token->name);
  if (def != NULL) {
    eval_def(def);
    return;
  }
  
  if (token->name[0] == '"') {
    eval_strsyntax(token);
    return;
  }

  // vocabulary syntax
  if (token->name[0] == '.') {
    if (eval_vocabsyntax(token)) return;
  }

  long x = strtol(token->name, NULL, 0);
  if (x != 0 || token->name[0] == '0') {
    global_push_type(intt);
    if (state) {
      add_int_effect();
      write_x(x);
    } else {
      push_x(x);
    }
    return;
  }

  if (eval_builtinwords(token)) return;

  error("undefined %s word", token->name);
}

void imm_eval_token(Token* t) {
  bool spill = state;
  state = false;
  eval_token(t);
  state = spill;
}

//
// main
//

void startup(size_t buffersize, size_t dpsize, size_t cpsize, size_t datasize) {
  buffer = malloc(buffersize);
  dp = (uint8_t*)jit_memalloc(dpsize);
  cp = (uint8_t*)jit_memalloc(cpsize);
  data = new_stack_cap(datasize);
  state = false;
  codegenstate = true;
  quotpos = -1;

  globaldefs = new_vocab("globals");
  searchlist = new_stack();
  definitions = new_stack();
  push(searchlist, globaldefs);
  push(definitions, globaldefs);

  imm_typein = new_stack();
  imm_typeout = new_stack();
  comp_typein = new_stack();
  comp_typeout = new_stack();
  typeidcnt = 0;
  quott = generate_type("Quot");
  typet = generate_type("Type");
  intt = generate_type("Int");
  cstrt = generate_type("Cstr");
}

void read_buffer(FILE* f) {
  char* bufferstart = buffer;
  for (;;) {
    char c = fgetc(f);
    if (c == EOF) break;
    *buffer = c;
    buffer++;
  }
  *buffer = '\0';
  buffer = bufferstart;
}

void eval_file(FILE* f) {
  char* tmpbuf = buffer;
  buffer = malloc(1024*1024);
  read_buffer(f);
  for (;;) {
    Token* t = parse_token();
    if (t == NULL) break;
    eval_token(t);
  }
  buffer = tmpbuf;
}

void eval_file_path(char* path) {
  FILE* f = fopen(path, "r");
  if (f == NULL) ierror("Unable to load %s", path);
  eval_file(f);
  fclose(f);
}

void load_core() {
  eval_file_path("prelude.hedon");
  eval_file_path("combinator.hedon");
  eval_file_path("cffi.hedon");
  eval_file_path("macro.hedon");
  eval_file_path("string.hedon");
  eval_file_path("fileio.hedon");
  eval_file_path("unittest.hedon");
}

int main(int argc, char** argv) {
  startup(1024*1024, 1024*1024, 1024*1024, 1024*1024);

  load_core();

  for (int i=1; i<argc; i++) {
    if (strcmp(argv[i], "-l") == 0) {
      i++;
      if (i >= argc) ierror("require filename argument by -l option");
      eval_file_path(argv[i]);
    } else if (strcmp(argv[i], "-c") == 0) {
      return 0;
    } else if (argv[i][0] == '-') {
      ierror("unknown cmdline argument: %s", argv[i]);
    } else {
      eval_file_path(argv[i]);
    }
  }

  read_buffer(stdin);
  for (;;) {
    Token* t = parse_token();
    if (t == NULL) break;
    eval_token(t);
  }

  return 0;
}
