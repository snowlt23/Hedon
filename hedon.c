#include "hedon.h"

uint8_t* dp;
Stack* data;
bool state;
bool codestate;
// currently quotation in global
Token* intoken;
Stack* inquot;
cell quotpos;

//
// interpret semantics
//

void push_x(cell x) {
  push(data, (void*)x);
}
cell pop_x() {
  return (cell)pop(data);
}

void call_word(uint8_t* wp) {
  call_word_asm(&data->p, wp);
}

//
// interpreter
//

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

void startup() {
  dp = (uint8_t*)jit_memalloc(DEFAULT_DATA_SIZE);
  data = new_stack_cap(DEFAULT_STACK_SIZE);
  state = false;
  codestate = true;
  quotpos = -1;

  init_typesystem();
  init_codegen();
  init_definitions();
  init_parser();
}

void eval_file(FILE* f) {
  char* tmpbuf = buffer;
  buffer = malloc(1024*1024);
  read_to_buffer(f);
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

void process_cmdline(int argc, char** argv) {
  for (int i=1; i<argc; i++) {
    if (strcmp(argv[i], "-l") == 0) {
      i++;
      if (i >= argc) ierror("require filename argument by -l option");
      eval_file_path(argv[i]);
    } else if (strcmp(argv[i], "-c") == 0) {
      exit(0);
    } else if (argv[i][0] == '-') {
      ierror("unknown cmdline argument: %s", argv[i]);
    } else {
      eval_file_path(argv[i]);
    }
  }
}

int main(int argc, char** argv) {
  startup();

  load_core();
  process_cmdline(argc, argv);

  read_to_buffer(stdin);
  for (;;) {
    Token* t = parse_token();
    if (t == NULL) break;
    eval_token(t);
  }

  return 0;
}
