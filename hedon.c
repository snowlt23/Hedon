#include "hedon.h"

char* buffer;
uint8_t* dp;
uint8_t* cp;
Stack* data;
bool state;
bool codestate;
Token* intoken;
Stack* inquot;
cell quotpos;
Vocab* globaldefs;
Stack* searchlist;
Stack* definitions;

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
  if (!codestate) return;
  memcpy(cp, buf, n);
  cp += n;
}

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

void push_x(cell x) {
  push(data, (void*)x);
}
cell pop_x() {
  return (cell)pop(data);
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

cell* write_x(cell x) {
  write_hex(0x48, 0x83, 0xeb, 0x08); // sub rbx, 8
  write_hex(0x48, 0xb8); // movabs rax, x
  cell* fixup = (cell*)cp; write_qword(x);
  write_hex(0x48, 0x89, 0x03); // mov [rbx], rax
  return fixup;
}

void write_call_word(size_t wp) {
  write_hex(0x49, 0xba); write_qword(wp); // movabs r10, wp
  write_hex(0x41, 0xff, 0xd2); // call r10
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

void expand_word(Def* def) {
  eval_quot(def->quot->quot);
}

void typing_quot(Stack* s) {
  spill_codestate();
  codestate = false;
  eval_quot(s);
  restore_codestate();
}

void codegen_quot(Stack* s) {
  spill_codestate();
  codestate = true;
  eval_quot(s);
  restore_codestate();
}

void eval_quot(Stack* s) {
  Stack* tmpinquot = inquot;
  int tmpquotpos = quotpos;
  inquot = s;
  for (quotpos=0; quotpos<(cell)stacklen(inquot); quotpos++) {
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
  } else if (is_trait(def) && !codestate) {
    apply_effects(def);
  } else if (is_trait(def) && codestate) {
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
  global_push_type(cstr_type());
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
  for (size_t i=0; i<stacklen(definitions); i++) {
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

void eval_token(Token* token) {
  intoken = token;
  if (token->kind == TOKEN_QUOT) {
    imm_push_type(quot_type());
    push_x((size_t)token->quot);
    return;
  }

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
    global_push_type(int_type());
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
  codestate = true;
  quotpos = -1;

  init_typesystem();
  init_codegen();

  globaldefs = new_vocab("globals");
  searchlist = new_stack();
  definitions = new_stack();
  push(searchlist, globaldefs);
  push(definitions, globaldefs);
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
  eval_file_path("record.hedon");
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
