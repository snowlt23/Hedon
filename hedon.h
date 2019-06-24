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

#define CELLSIZE 8
#define DEFAULT_STACK_SIZE (CELLSIZE*1024)
#define DEFAULT_TEXT_SIZE (1024*1024)
#define DEFAULT_DATA_SIZE (1024*1024)
#define DEFAULT_BUFFER_SIZE (1024*1024)

#define FLAG_IMM 0b01
#define FLAG_BUILTIN 0b10

#define CONCAT(a, b) a ## b
#define write_hex2(id, ...) \
  uint8_t id[] = {__VA_ARGS__}; \
  write_to_cp(id, sizeof(id));
#define write_hex1(ln, ...) write_hex2(CONCAT(_hex, ln), __VA_ARGS__)
#define write_hex(...) write_hex1(__LINE__, __VA_ARGS__)

#define take_byte(x, n) ((x >> (8*n)) & 0xff)

#define EFF_TYPES2(arr, ret, len, kind, ...) \
  char* arr[] = {__VA_ARGS__}; \
  len += sizeof(arr)/sizeof(char*); \
  eff_types(ret, kind, sizeof(arr)/sizeof(char*), arr);
#define EFF_TYPES1(ln, ret, len, kind, ...) EFF_TYPES2(CONCAT(tmptypes ## kind, ln), ret, len, kind, __VA_ARGS__)
#define EFF_TYPES(ret, len, kind, ...) EFF_TYPES1(__LINE__, ret, len, kind, __VA_ARGS__)
#define IN_EFF(...) EFF_TYPES(effs, len, EFF_IN, __VA_ARGS__); effs=rev_stack(effs); len=-len;
#define OUT_EFF(...) EFF_TYPES(effs, len, EFF_OUT, __VA_ARGS__);

#define STARTUP_WORD(name, flags, fnp, depth, tdecl) \
  { \
    Stack* effs = new_stack(); \
    tdecl \
    builtin_word(name, flags, fnp, depth, effs); \
  }
#define BUILTIN_WORD(name, flags, fnp, tdecl) \
  { \
    Stack* effs = new_stack(); \
    int len = 0; \
    tdecl \
    builtin_word(name, flags, fnp, len, effs); \
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
  size_t immediate;
  bool polymorphic;
  cell deptheffect;
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
extern uint8_t* dp;
extern Stack* data;
extern bool state;
extern bool codestate;
extern Token* intoken;
extern Stack* inquot;
extern cell quotpos;

// typesystem.c
extern Stack* imm_typein;
extern Stack* imm_typeout;
extern Stack* comp_typein;
extern Stack* comp_typeout;

// codegen.c
extern uint8_t* cp;

// definitions.c
extern Vocab* globaldefs;
extern Stack* searchlist;
extern Stack* definitions;

// parser.c
extern char* buffer;

//
// prototypes
//

// utils.c
char* vformat(char* fmt, va_list ap);
char* format(char* fmt, ...);
void ierror(char* fmt, ...);
void error(char* fmt, ...);
#define debug(...) {fprintf(stderr, "L%d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");}

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
Type* init_uniontype_ref(char* name, size_t id);
Type* type_origin(Type* t);
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
void eff_types(Stack* effs, EffKind kind, int n, char** arr);

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
// emit to .text
void write_to_cp(uint8_t* buf, size_t n);
void write_lendian(size_t x, int n);
void write_qword(size_t x);
void write_call_builtin(void* p);
void write_stack_increment(int inc);
cell* write_x(cell x);
void write_call_word(size_t x);

// definitions.c
void init_definitions();
Def* new_def();
Def* search_def(char* name);
void add_def(Def* def);
Def* last_def();
bool is_trait(Def* def);
// for vocab
Vocab* new_vocab(char* name);
Vocab* last_vocab();

// primitives.c
void load_startup_words();
void load_builtin_words();

// parser.c
// for Token
Token* new_token(TokenKind kind);
Token* new_token_name(char* name);
Token* new_token_quot(Stack* s);
// parser
void init_parser();
void read_to_buffer(FILE* f);
Token* new_token(TokenKind kind);
Token* new_token_name(char* name);
Token* new_token_quot(Stack* s);
char bgetc();
Token* parse_quot();
Token* parse_token();

// hedon.c
void push_x(cell x);
cell pop_x();
void call_word(uint8_t* wp);
void typing_quot(Stack* t);
void codegen_quot(Stack* t);
void eval_quot(Stack* t);
void eval_token(Token* t);
void imm_eval_token(Token* t);
// printer
void print_quot(Stack* q);
void print_token(Token* t);
char* format_typestack(Stack* s);
void dump_typestack(FILE* f, Stack* s);

#endif
