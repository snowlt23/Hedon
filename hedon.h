#ifndef _HEDON_H_
#define _HEDON_H_

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

#define CELLSIZE 8
#define DEFAULT_STACKSIZE (CELLSIZE*1024)

#define CONCAT(a, b) a ## b
#define write_hex2(id, ...) \
  uint8_t id[] = {__VA_ARGS__}; \
  write_to_cp(id, sizeof(id));
#define write_hex1(ln, ...) write_hex2(CONCAT(_hex, ln), __VA_ARGS__)
#define write_hex(...) write_hex1(__LINE__, __VA_ARGS__)

#define take_byte(x, n) ((x >> (8*n)) & 0xff)

#define IN_FOR(arr) for (cell i=(cell)(sizeof(arr)/sizeof(char*))-1; i>=0; i--)
#define OUT_FOR(arr) for (cell i=0; i<(cell)(sizeof(arr)/sizeof(char*)); i++)
#define BUILTIN_EFF(arr, d, kind, fr, ...) \
  Def* d = last_def(); \
  char* arr[] = {__VA_ARGS__}; \
  fr { \
    Def* eff = search_def(arr[i]); \
    if (eff == NULL) error("undefined %s eff-word", arr[i]); \
    call_word(eff->wp); \
    apply_effects(eff); \
    if (kind == EFF_IMM) continue; \
    global_pop_type(type_type()); \
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

//
// types
//

typedef int64_t cell;

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

//
// globals
//

// hedon.c
extern char* buffer;
extern uint8_t* dp;
extern uint8_t* cp;
extern Stack* data;
extern bool state;
extern bool codestate;
extern Token* intoken;
extern Stack* inquot;
extern cell quotpos;
extern Vocab* globaldefs;
extern Stack* searchlist;
extern Stack* definitions;

// typesystem.c
extern Stack* imm_typein;
extern Stack* imm_typeout;
extern Stack* comp_typein;
extern Stack* comp_typeout;

//
// prototypes
//

// utils.c
char* vformat(char* fmt, va_list ap);
char* format(char* fmt, ...);
void error(char* fmt, ...);

// ffi.s
void call_word_asm(uint8_t** spp, uint8_t* wp);

// stack.c
Stack* new_stack_cap(size_t size);
Stack* new_stack();
Stack* dup_stack(Stack* s);
void push(Stack* s, void* v);
void* pop(Stack* s);
size_t stacklen(Stack* s);
void* get(Stack* s, size_t i);
Stack* rev_stack(Stack* s);

// typeapi.c
size_t newtypeid();
Type* new_type(char* name, size_t id);
Type* dup_type(Type* t);
Type* generate_type(char* name);
Type* init_paramtype(char* name, size_t id);
Type* init_uniontype(char* name, size_t id);
Type* init_reftype(Type* ref);
Type* init_paramtype_ref(char* name, size_t id);
int typeid(Type* t);
char* typename(Type* t);
bool eqtype(Type* a, Type* b);
void replace_typeref(Type* t, Type* r);
bool eq_typestack(Stack* a, Stack* b);
// for Effect
Eff* new_eff(Def* def, EffKind kind);
bool is_polytype(Type* t);
bool is_in_polytype(Type* t);
bool is_polymorphic(Stack* s);
bool is_in_polymorphic(Stack* s);
Stack* freeze(Stack* s);
EffSave* save_inout(Stack* in, Stack* out);

// typesystem.c
void init_typesystem();
Type* quot_type();
Type* type_type();
Type* int_type();
Type* cstr_type();
void imm_push_type(Type* l);
void global_push_type(Type* l);
bool imm_pop_type(Type* l);
bool global_pop_type(Type* l);
void apply_in(Def* def, Stack* in);
void apply_out(Def* def, Stack* out);
bool eq_in(Stack* a, Stack* in);
void spill_typestack();
void restore_typestack();
// for definition
void apply_effects(Def* def);
void imm_apply_effects(Def* def);
void add_int_effect();
void add_cstr_effect();
Def* solve_trait_word(Stack* defs);

// codegen.c
void init_codegen();
void spill_codestate();
void restore_codestate();

// primitives.c
bool eval_builtinwords(Token* token);

// hedon.c
bool is_trait(Def* def);
Token* new_token(TokenKind kind);
Token* new_token_name(char* name);
Token* new_token_quot(Stack* s);
void write_call_builtin(void* p);
void write_stack_increment(int inc);
cell* write_x(cell x);
char bgetc();
Def* new_def();
Token* parse_quot();
Vocab* new_vocab(char* name);
Vocab* last_vocab();
void add_def(Def* def);
void write_to_cp(uint8_t* buf, size_t n);
void push_x(cell x);
cell pop_x();
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

#endif
