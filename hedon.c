#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

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
#define error(...) {fprintf(stderr, "error in %s: ", last_def()->name); fprintf(stderr, __VA_ARGS__); fprintf(stderr, " at %s", intoken->name); exit(1);}

#define spill_globaltype \
  Stack* tmpb = comp_typein; \
  Stack* tmpa = comp_typeout; \
  comp_typein = new_stack(); \
  comp_typeout = new_stack();
#define restore_globaltype \
  comp_typein = tmpb; \
  comp_typeout = tmpa;
#define start_codegenstate(b) \
  bool tmpcs = codegenstate; \
  codegenstate = b;
#define end_codegenstate() \
  codegenstate = tmpcs;

#define BUILTIN_EFF(arr, d, kind, ...) \
  Def* d = last_def(); \
  char* arr[] = {__VA_ARGS__}; \
  for (int i=0; i<sizeof(arr)/sizeof(char*); i++) { \
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
#define IN_EFF(...) BUILTIN_EFF(inarr, indef, EFF_IN, __VA_ARGS__)
#define OUT_EFF(...) BUILTIN_EFF(outarr, outdef, EFF_OUT, __VA_ARGS__)
#define BUILTIN_WORD(s, f, stackinc, tdecl) \
  if (strcmp(token->name, s) == 0) { \
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
  if (strcmp(token->name, s) == 0) { \
    f(); \
    return; \
  }
#define BUILTIN_PARSE_WORD(s, f) \
  if (strcmp(t->name, s) == 0) { \
    t = f(); \
    if (t == NULL) return parse_token(); \
    else return t; \
  }

#define WORDSIZE 8
#define DEFAULT_STACKSIZE (WORDSIZE*1024)

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
} TypeKind;
typedef struct _Type {
  TypeKind kind;
  char* name;
  size_t id;
  Stack* params;
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
  char* bufferaddr;
  Token* quot;
  uint8_t* wp;
  size_t codesize;
  Stack* effects;
  Stack* freezein;
  Stack* freezeout;
  bool immediate;
  Stack* traitwords;
  bool template;
} Def;

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
Stack* globaldefs;
Token* intoken;

Stack* imm_typein;
Stack* imm_typeout;
Stack* comp_typein;
Stack* comp_typeout;
size_t typeid;
Type* quott;
Type* typet;
Type* intt;

//
// prototypes
//

void call_word_asm(uint8_t** spp, uint8_t* wp);

Token* parse_token();
void dump_typestack(FILE* f, Stack* s);
void typing_quot(Stack* t);
void codegen_quot(Stack* t);
void eval_quot(Stack* t);
void eval_token(Token* t);
void imm_eval_token(Token* t);

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
  assert(s->s <= s->p);
  pushT(s, void*, v);
}
void* pop(Stack* s) {
  assert(s->s+s->cap >= s->p);
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
  t->params = NULL;
  return t;
}

Type* dup_type(Type* t) {
  Type* newt = malloc(sizeof(Type));
  *newt = *t;
  return newt;
}

Type* generate_type(char* name) {
  return new_type(name, typeid++);
}

Type* init_paramtype(char* name, size_t id) {
  Type* t = new_type(name, id);
  t->kind = TYPE_PARAM;
  return t;
}

bool eqtype(Type* a, Type* b) {
  if (a->id == b->id) return true;
  if (a->kind == TYPE_PARAM) return true;
  if (b->kind == TYPE_PARAM) return true;
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

bool can_freeze(Stack* s) {
  for (int i=0; i<stacklen(s); i++) {
    Type* t = get(s, i);
    if (t->kind != TYPE_SINGLE) return false;
  }
  return true;
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
  def->bufferaddr = buffer;
  def->immediate = NULL;
  def->traitwords = NULL;
  def->template = false;
  return def;
}

bool is_trait(Def* def) {
  return def->traitwords != NULL;
}

Def* search_def(char* s) {
  for (int i=stacklen(globaldefs)-1; i>=0; i--) {
    Def* def = get(globaldefs, i);
    if (strcmp(def->name, s) == 0) return def;
  }
  return NULL;
}

Def* last_def() {
  return get(globaldefs, stacklen(globaldefs)-1);
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
    error("stack is empty, but expected %s value", l->name);
  }
  Type* sl = pop(imm_typeout);
  if (!eqtype(l, sl)) error("unmatch %s type to %s", sl->name, l->name);
  return false;
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
    if (!eqtype(l, sl)) error("unmatch %s type to %s", sl->name, l->name);
    if (l->kind == TYPE_PARAM) *l = *sl;
    if (sl->kind == TYPE_PARAM) *sl = *l;
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

Token* parse_name() {
  skip_spaces();
  char token[256] = {};
  int i;
  for (i=0; ; i++) {
    if (i >= 256-1) error("hedon word length should be <256.");
    char c = bgetc();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\0' || c == EOF) break;
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
    if (def->freezein == NULL) error("%s impl isn't freezing", def->name);
    if (eq_in(comp_typeout, def->freezein)) return def;
  }
  return NULL;
}

void to_effect(Def* def, Stack* s, EffKind kind) {
  for (int i=0; i<stacklen(s); i++) {
    Type* t = get(s, i);
    Def* eff = search_def(t->name);
    if (eff == NULL) error("undefined %s word", t->name);
    push(def->effects, new_eff(eff, kind));
  }
}

void apply_effects(Def* def) {
  Def* wdef = last_def();
  if (def->freezein != NULL) {
    apply_in(wdef, def->freezein);
    apply_out(wdef, def->freezeout);
    return;
  }

  for (int i=0; i<stacklen(def->effects); i++) {
    Eff* eff = get(def->effects, i);
    start_codegenstate(true);
    apply_effects(eff->def);
    end_codegenstate();
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

void word_def() {
  Token* n = parse_token(); // parse name
  Def* def = new_def();
  push(globaldefs, def);
  strcpy(def->name, n->name);
  def->wp = cp;

  spill_globaltype;

  state = true;
  def->quot = parse_quot();

  // type
  typing_quot(def->quot->quot);
  if (can_freeze(comp_typein) && can_freeze(comp_typeout)) {
    def->freezein = freeze(comp_typein);
    def->freezeout = freeze(comp_typeout);
  } else {
    def->template = true;
  }
  // store inferred typestack for write
  comp_typeout = rev_stack(comp_typein);
  comp_typein = new_stack();

  // write with inferred type
  start_codegenstate(true);
  write_hex(0x48, 0x83, 0xec, 0x08); // sub rsp, 8
  codegen_quot(def->quot->quot);
  write_hex(0x48, 0x83, 0xc4, 0x08); // add rsp, 8
  write_hex(0xc3); // ret
  end_codegenstate();

  restore_globaltype;
  state = false;

  // debug("%s %d", def->name, def->template);
}

void word_typeid() {
  write_x(typeid++);
}

void word_gentype() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)new_type(name, id));
}

void word_newtype() {
  char* name = last_def()->name;
  size_t id = typeid++;
  write_x((size_t)new_type(name, id));
}

void word_paramtype_in() {
  char* name = (char*)pop_x();
  push_x((size_t)init_paramtype(name, typeid++));
}

void word_paramtype() {
  char* name = last_def()->name;
  write_x((size_t)name);
  write_call_builtin(word_paramtype_in);
  // write_stack_increment(0);
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
  if (!eq_typestack(a->in, b->in)) {dump_typestack(stderr, a->in); fprintf(stderr, " <-> "); dump_typestack(stderr, b->in); fprintf(stderr, " "); error("unmatch in-effect");}
  if (!eq_typestack(a->out, b->out)) {dump_typestack(stderr, a->out); fprintf(stderr, " <-> "); dump_typestack(stderr, b->out); fprintf(stderr, " "); error("unmatch out-effect");}
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
  def->effects = new_stack();
  Token* t = parse_token();
  Def* eff = search_def(t->name);
  if (eff == NULL) error("undefined %s word", t->name);
  push(def->effects, new_eff(eff, EFF_IMM));
}

void word_force_effects() {
  Def* def = last_def();
  def->effects = new_stack();
  def->freezein = NULL;
  def->freezeout = NULL;
  EffKind inout = EFF_IN;
  for (;;) {
    Token* t = parse_token();
    if (t->kind == TOKEN_NAME && strcmp(t->name, "--") == 0) {
      def->effects = rev_stack(def->effects);
      inout = EFF_OUT;
      continue;
    }
    if (t->kind == TOKEN_NAME && strcmp(t->name, ")") == 0) break;
    Def* eff = search_def(t->name);
    if (eff == NULL) error("undefined %s word", t->name);
    push(def->effects, new_eff(eff, inout));
  }

  spill_globaltype;
  state = true;
  apply_effects(def);
  if (can_freeze(comp_typein) && can_freeze(comp_typeout)) {
    def->freezein = freeze(comp_typein);
    def->freezeout = freeze(comp_typeout);
  }
  state = false;
  restore_globaltype;
}

void dump_typestack(FILE* f, Stack* s) {
  Type* tfirst = get(s, 0);
  if (stacklen(s) != 0) fprintf(f, "%s", tfirst->name);
  for (int i=1; i<stacklen(s); i++) {
    Type* t = get(s, i);
    fprintf(f, " %s", t->name);
  }
}

void word_dump_comp_typestack() {
  dump_typestack(stdout, comp_typein);
  printf(" -- ");
  dump_typestack(stdout, comp_typeout);
  fflush(stdout);
}

void word_dump_imm_typestack() {
  dump_typestack(stdout, imm_typein);
  printf(" -- ");
  dump_typestack(stdout, imm_typeout);
  fflush(stdout);
}

void word_dump_type() {
  Token* t = parse_token();
  Def* def = search_def(t->name);
  if (def == NULL) error("undefined %s word in dump-type", t->name);
  if (def->freezein != NULL) {
    dump_typestack(stdout, rev_stack(def->freezein));
    printf(" -- ");
    dump_typestack(stdout, def->freezeout);
  } else {
    spill_globaltype;
    state = true;
    apply_effects(def);
    dump_typestack(stdout, rev_stack(comp_typein));
    printf(" -- ");
    dump_typestack(stdout, comp_typeout);
    state = false;
    restore_globaltype;
  }
  fflush(stdout);
}

void word_dump_effect() {
  Token* t = parse_token();
  Def* def = search_def(t->name);
  if (def == NULL) error("undefined %s word in dump-effect", t->name);
  for (int i=0; i<stacklen(def->effects); i++) {
    Eff* eff = get(def->effects, i);
    if (eff->kind == EFF_IN) {
      printf("%s:in ", eff->def->name);
    } else if (eff->kind == EFF_OUT) {
      printf("%s:out ", eff->def->name);
    } else {
      printf("%s:imm ", eff->def->name);
    }
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
  *dp = '\0';
  dp++;
  write_x(p);
}

void word_rem() {
  for (;;) {
    if (bgetc() == '\n') break;
  }
}

void word_parse_token() {
  push_x((size_t)parse_token());
}

void word_search_word() {
  char* name = (char*)pop_x();
  push_x((size_t)search_def(name));
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
void word_sdot() {
  char* x = (char*)pop_x();
  printf("%s", x);
  fflush(stdout);
}
void word_cr() {
  printf("\n");
}

void word_op() {
  write_hex((uint8_t)pop_x());
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

void* dlopen_x(char* libname, size_t flag) {
  fprintf(stderr, "dlopen %s %zd\n", libname, flag);
  void* h = dlopen(libname, flag);
  if (h == NULL) error("aaa");
  fprintf(stderr, "dlshared %p\n", h);
  return h;
}
void* dlsym_x(void* h, char* symname) {
  fprintf(stderr, "dlsym %p %s\n", h, symname);
  void* s = dlsym(h, symname);
  if (s == NULL) error("aaa dlsym");
  return s;
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
  bool prevcs = codegenstate;
  codegenstate = false;
  eval_quot(s);
  codegenstate = prevcs;
}

void codegen_quot(Stack* s) {
  bool prevcs = codegenstate;
  codegenstate = true;
  eval_quot(s);
  codegenstate = prevcs;
}

void eval_quot(Stack* s) {
  for (int i=0; i<stacklen(s); i++) {
    Token* t = get(s, i);
    eval_token(t);
  }
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
    if (def->immediate) {
      imm_apply_effects(def);
      call_word(def->wp);
    } else if (def->template) {
      expand_word(def);
    // } else if (is_trait(def) && !state) {
    //   error("trait can't call on toplevel in currently");
    } else if (is_trait(def) && !codegenstate) {
      apply_effects(def);
    } else if (is_trait(def) && codegenstate) {
      Def* solved = solve_trait_word(def->traitwords);
      if (solved == NULL) {
        if (last_def()->freezein != NULL) error("unresolved %s trait word", def->name);
        last_def()->template = true;
      } else {
        write_call_word((size_t)solved->wp);
      }
      apply_effects(def);
    } else if (state) {
      apply_effects(def);
      write_call_word((size_t)def->wp);
    } else {
      imm_apply_effects(def);
      call_word(def->wp);
    }
    return;
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

  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_IMM_WORD("trait", word_trait);
  BUILTIN_WORD("impl>", word_impl, 0, {});
  BUILTIN_IMM_WORD("!(", word_force_effects);
  BUILTIN_IMM_WORD("eff.attach", word_eff_attach);
  BUILTIN_IMM_WORD("X", word_X);

  // builtin for type def
  BUILTIN_IMM_WORD("builtin.Type.eff", word_type_eff);
  BUILTIN_WORD("builtin.Quot", word_quot, 8, {});
  BUILTIN_WORD("builtin.Type", word_type, 8, {});
  BUILTIN_WORD("builtin.Int", word_int, 8, {});
  BUILTIN_WORD("builtin.newtype", word_newtype, 8, {});
  BUILTIN_WORD("builtin.paramtype", word_paramtype, 8, {});

  // effect words
  BUILTIN_WORD("builtin.eff.push", word_eff_push, -8, {});
  BUILTIN_WORD("builtin.eff.drop", word_eff_drop, -8, {});
  BUILTIN_WORD("builtin.eff.dup", word_eff_dup, -8, {});
  BUILTIN_WORD("builtin.eff.save", word_eff_save, 8, {});
  BUILTIN_WORD("builtin.eff.load", word_eff_load, -8, {});
  BUILTIN_WORD("builtin.eff.check", word_eff_check, -16, {});

  // builtin parse words
  BUILTIN_IMM_WORD("s\"", word_strlit);

  // word control
  BUILTIN_WORD("parse-token", word_parse_token, 8, {IN_EFF("Token")});
  BUILTIN_WORD("search-word", word_search_word, 0, {IN_EFF("Int"); OUT_EFF("Int")});
  BUILTIN_WORD("word-code", word_word_code, 0, {IN_EFF("Int"); OUT_EFF("Int")});

  BUILTIN_WORD("builtin.dp", word_dp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD("builtin.cp", word_cp, 8, {OUT_EFF("Int")});
  BUILTIN_WORD(".", word_dot, -8, {IN_EFF("Int")});
  BUILTIN_WORD("s.", word_sdot, -8, {IN_EFF("Int")});
  BUILTIN_WORD("cr", word_cr, 0, {});
  BUILTIN_WORD("op", word_op, -8, {IN_EFF("Int")});
  BUILTIN_WORD("compile", word_compile, -8, {IN_EFF("Quot")});
  BUILTIN_IMM_WORD("postcompile", word_postcompile);

  // dump
  BUILTIN_WORD("dump-type", word_dump_type, 0, {});
  BUILTIN_WORD("dump-effect", word_dump_effect, 0, {});
  BUILTIN_WORD("dump-comp-typestack", word_dump_comp_typestack, 0, {});
  BUILTIN_WORD("dump-imm-typestack", word_dump_imm_typestack, 0, {});

  // cffi
  BUILTIN_WORD("builtin.c.dlopen", word_dlopen, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlsym", word_dlsym, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlclose", word_dlclose, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call1", word_call1, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call2", word_call2, 8, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call6", word_call6, 8, {OUT_EFF("Pointer")});

  error("undefined %s word", token->name);
}

void imm_eval_token(Token* t) {
  bool tmpstate = state;
  state = false;
  eval_token(t);
  state = tmpstate;
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

  globaldefs = new_stack_cap(sizeof(Def*)*4096);

  imm_typein = new_stack();
  imm_typeout = new_stack();
  comp_typein = new_stack();
  comp_typeout = new_stack();
  typeid = 0;
  quott = generate_type("Quot");
  typet = generate_type("Type");
  intt = generate_type("Int");
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
  // eval_file_path("cffi.hedon");
  // eval_file_path("fileio.hedon");
  // eval_file_path("local.hedon");
}

int main(int argc, char** argv) {
  startup(1024*1024, 1024*1024, 1024*1024, 1024*1024);

  load_core();

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
    Token* t = parse_token();
    if (t == NULL) break;
    eval_token(t);
  }

  return 0;
}
