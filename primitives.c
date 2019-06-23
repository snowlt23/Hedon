#include "hedon.h"

void word_type_eff() {
  global_push_type(type_type());
}

void word_quot() {
  push_x((size_t)quot_type());
}

void word_type() {
  push_x((size_t)type_type());
}

void word_int() {
  push_x((size_t)int_type());
}

void word_cstr() {
  push_x((size_t)cstr_type());
}

void word_def() {
  Token* n = parse_token(); // parse name
  Def* def = new_def();
  add_def(def);
  strcpy(def->name, n->name);
  def->wp = cp;

  spill_typestack();

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
  spill_codestate();
  codestate = true;
  write_hex(0x48, 0x83, 0xec, 0x08); // sub rsp, 8
  codegen_quot(def->quot->quot);
  write_hex(0x48, 0x83, 0xc4, 0x08); // add rsp, 8
  write_hex(0xc3); // ret
  restore_codestate();

  restore_typestack();
  state = false;
}

void word_template() {
  Token* n = parse_token(); // parse name
  Def* def = new_def();
  add_def(def);
  strcpy(def->name, n->name);
  def->polymorphic = true;
  def->quot = parse_quot();
}

void word_typeid() {
  write_x(newtypeid());
}

void word_gentype() {
  size_t id = pop_x();
  char* name = (char*)pop_x();
  push_x((size_t)new_type(name, id));
}

void word_newtype() {
  char* name = last_def()->name;
  size_t id = newtypeid();
  write_x((size_t)new_type(name, id));
}

void word_paramtype_in() {
  char* name = (char*)pop_x();
  push_x((size_t)init_paramtype_ref(name, newtypeid()));
}

void word_paramtype() {
  char* name = last_def()->name;
  write_x((size_t)name);
  write_call_builtin(word_paramtype_in);
  // write_stack_increment(0);
}

void word_uniontype() {
  char* name = last_def()->name;
  size_t id = newtypeid();
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
  def->immediate |= FLAG_IMM;
}

void word_inline() {
  Def* def = last_def();
  def->polymorphic = true;
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
  imm_pop_type(quot_type());
  Stack* q = (Stack*)pop_x();
  for (size_t i=0; i<stacklen(q); i++) {
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
  spill_typestack();
  state = true;
  apply_effects(def);
  def->freezein = freeze(comp_typein);
  def->freezeout = freeze(comp_typeout);
  def->polymorphic = is_in_polymorphic(comp_typein) || is_polymorphic(comp_typeout);
  state = false;
  restore_typestack();
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
  for (size_t i=1; i<stacklen(s); i++) {
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
    spill_typestack();
    state = true;
    apply_effects(def);
    printf("%s -- %s", format_typestack(rev_stack(comp_typein)), format_typestack(comp_typeout));
    state = false;
    restore_typestack();
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

void word_word_name() {
  Def* def = (Def*)pop_x();
  push_x((size_t)def);
  push_x((size_t)def->name);
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
  for (size_t i=0; i<stacklen(q); i++) {
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
  printf("%ld", x);
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
  if (!codestate) return;
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
  imm_pop_type(quot_type());
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
  for (size_t i=0; i<stacklen(a); i++) {
    push(q, get(a, i));
  }
  for (size_t i=0; i<stacklen(b); i++) {
    push(q, get(b, i));
  }
  push_x((size_t)(q));
}

void word_postquot() {
  imm_pop_type(quot_type());
  global_push_type(quot_type());
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

//
// initialize
//

void builtin_word(char* name, cell flags, void* fnp, int depth, Stack* effs) {
  Def* def = new_def();
  add_def(def);
  strcpy(def->name, name);
  def->immediate = flags | FLAG_BUILTIN;
  def->wp = fnp;
  def->effects = effs;
  def->quot = NULL;
  def->polymorphic = true;
  def->deptheffect = depth;
}

void load_startup_words() {
  // builtin for def
  BUILTIN_WORD(":", 0, word_def, {});
  BUILTIN_WORD("template:", 0, word_template, {});
  BUILTIN_WORD("immediate", 0, word_immediate, {});
  BUILTIN_WORD("inline", 0, word_inline, {});
  BUILTIN_WORD("trait", FLAG_IMM, word_trait, {});
  BUILTIN_WORD("impl", 0, word_impl, {});
  BUILTIN_WORD("![", FLAG_IMM, word_force_effects, {});
  BUILTIN_WORD("eff.attach", FLAG_IMM, word_eff_attach, {});
  BUILTIN_WORD("X", FLAG_IMM, word_X, {});

  // builtin for type def
  STARTUP_WORD("builtin.Type.eff", FLAG_IMM, word_type_eff, 0, {});
  STARTUP_WORD("builtin.Quot", 0, word_quot, 1, {});
  STARTUP_WORD("builtin.Type", 0, word_type, 1, {});
  STARTUP_WORD("builtin.Int", 0, word_int, 1, {});
  STARTUP_WORD("builtin.Cstr", 0, word_cstr, 1, {});
  STARTUP_WORD("builtin.newtype", 0, word_newtype, 1, {});
  STARTUP_WORD("builtin.paramtype", 0, word_paramtype, 1, {});
  STARTUP_WORD("builtin.uniontype", 0, word_uniontype, 1, {});

  // effect words
  STARTUP_WORD("builtin.eff.push", 0, word_eff_push, -1, {});
  STARTUP_WORD("builtin.eff.drop", 0, word_eff_drop, -1, {});
  STARTUP_WORD("builtin.eff.dup", 0, word_eff_dup, -1, {});
  STARTUP_WORD("builtin.eff.save-out", 0, word_eff_save_out, 1, {});
  STARTUP_WORD("builtin.eff.load-out", 0, word_eff_load_out, -1, {});
  STARTUP_WORD("builtin.eff.save", 0, word_eff_save, 1, {});
  STARTUP_WORD("builtin.eff.load", 0, word_eff_load, -1, {});
  STARTUP_WORD("builtin.eff.check", 0, word_eff_check, -2, {});
}

void load_builtin_words() {
  // type api
  BUILTIN_WORD("is", 0, word_is, {IN_EFF("Type", "Type")});

  // word control
  BUILTIN_WORD("parse-token", 0, word_parse_token, {OUT_EFF("Token")});
  BUILTIN_WORD("token-name", 0, word_token_name, {IN_EFF("Token"); OUT_EFF("Cstr")});

  BUILTIN_WORD("search-word", 0, word_search_word, {IN_EFF("Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD("<word>", 0, word_create_word, {IN_EFF("Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD(">>code", 0, word_set_code, {IN_EFF("Word", "Quot"); OUT_EFF("Word")});
  BUILTIN_WORD(">>impl", 0, word_add_impl, {IN_EFF("Word", "Cstr"); OUT_EFF("Word")});
  BUILTIN_WORD(">>eff", 0, word_set_eff, {IN_EFF("Word", "Eff"); OUT_EFF("Word")});
  BUILTIN_WORD(">vocab", 0, word_add_to_vocab, {IN_EFF("Word")});
  BUILTIN_WORD("word-name>>", 0, word_word_name, {IN_EFF("Word"); OUT_EFF("Word", "Cstr")});
  BUILTIN_WORD("as-type", 0, word_as_type, {IN_EFF("Word"); OUT_EFF("Type")});

  BUILTIN_WORD("<eff>", 0, word_create_eff, {OUT_EFF("Eff")});
  BUILTIN_WORD(">>eff.in", 0, word_add_effin, {IN_EFF("Eff", "Word"); OUT_EFF("Eff")});
  BUILTIN_WORD(">>eff.out", 0, word_add_effout, {IN_EFF("Eff", "Word"); OUT_EFF("Eff")});

  BUILTIN_WORD("<vocab>", 0, word_create_vocab, {IN_EFF("Cstr"); OUT_EFF("Vocab")});
  BUILTIN_WORD(">>def", 0, word_add_def, {IN_EFF("Vocab", "Word"); OUT_EFF("Vocab")});
  BUILTIN_WORD("also", 0, word_also, {IN_EFF("Vocab")});
  BUILTIN_WORD("previous", 0, word_previous, {OUT_EFF("Vocab")});
  BUILTIN_WORD("definitions", 0, word_definitions, {IN_EFF("Vocab")});

  BUILTIN_WORD("builtin.dp", 0, word_dp, {OUT_EFF("Int")});
  BUILTIN_WORD("builtin.cp", 0, word_cp, {OUT_EFF("Int")});
  BUILTIN_WORD(".", 0, word_dot, {IN_EFF("Int")});
  BUILTIN_WORD(".c", 0, word_dotc, {IN_EFF("Int")});
  BUILTIN_WORD(".s", 0, word_dots, {IN_EFF("Cstr")});
  BUILTIN_WORD(".q", 0, word_dotq, {IN_EFF("Quot")});
  BUILTIN_WORD("cr", 0, word_cr, {});

  // op
  BUILTIN_WORD("op", 0, word_op, {IN_EFF("Int")});
  BUILTIN_WORD("fixup-op", 0, word_fixup_op, {IN_EFF("Int", "Pointer")});

  // quot
  BUILTIN_WORD("compile", 0, word_compile, {IN_EFF("Quot")});
  BUILTIN_WORD("literal", 0, word_literal, {IN_EFF("Int"); OUT_EFF("Token")});
  BUILTIN_WORD("quot->token", 0, word_quot_to_token, {IN_EFF("Quot"); OUT_EFF("Token")});
  BUILTIN_WORD("quot", 0, word_new_quot, {OUT_EFF("Quot")});
  BUILTIN_WORD("push", 0, word_push, {IN_EFF("Token", "Quot");});
  BUILTIN_WORD("combine", 0, word_combine, {IN_EFF("Quot", "Quot"); OUT_EFF("Quot")});
  BUILTIN_WORD("postquot", FLAG_IMM, word_postquot, {});
  BUILTIN_WORD("postcompile", FLAG_IMM, word_postcompile, {});

  // dump
  BUILTIN_WORD("dump-type", 0, word_dump_type, {});

  // cffi
  BUILTIN_WORD("builtin.c.dlopen", 0, word_dlopen, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlsym", 0, word_dlsym, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.c.dlclose", 0, word_dlclose, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call1", 0, word_call1, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call2", 0, word_call2, {OUT_EFF("Pointer")});
  BUILTIN_WORD("builtin.test.call6", 0, word_call6, {OUT_EFF("Pointer")});
}
