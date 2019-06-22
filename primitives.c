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
  def->immediate = true;
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

bool eval_builtinwords(Token* token) {
  // builtin for def
  BUILTIN_WORD(":", word_def, 0, {});
  BUILTIN_WORD("template:", word_template, 0, {});
  BUILTIN_WORD("immediate", word_immediate, 0, {});
  BUILTIN_WORD("inline", word_inline, 0, {});
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
  BUILTIN_WORD("word-name>>", word_word_name, 8, {IN_EFF("Word"); OUT_EFF("Word", "Cstr")});
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
