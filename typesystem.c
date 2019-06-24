#include "hedon.h"

//
// globals
//

Stack* imm_typein;
Stack* imm_typeout;
Stack* comp_typein;
Stack* comp_typeout;
Stack* spill_imm_typein;
Stack* spill_imm_typeout;
Stack* spill_comp_typein;
Stack* spill_comp_typeout;

Type* quott;
Type* typet;
Type* intt;
Type* cstrt;

//
// functions
//

void init_typesystem() {
  imm_typein = new_stack();
  imm_typeout = new_stack();
  comp_typein = new_stack();
  comp_typeout = new_stack();
  quott = generate_type("Quot");
  typet = generate_type("Type");
  intt = generate_type("Int");
  cstrt = generate_type("Cstr");
}

Type* quot_type() {
  return quott;
}

Type* type_type() {
  return typet;
}

Type* int_type() {
  return intt;
}

Type* cstr_type() {
  return cstrt;
}

void imm_push_type(Type* l) {
  push(imm_typeout, l);
}

void global_push_type(Type* l) {
  if (state) push(comp_typeout, l);
  else imm_push_type(l);
}

bool imm_pop_type_result(Type* l, char** err) {
  *err = NULL;
  if (stacklen(imm_typeout) == 0) {
    // if stack is empty at global state
    *err = format("stack is empty, but expected %s value", typename(l));
    return false;
  }
  Type* sl = pop(imm_typeout);
  if (!eqtype(l, sl)) *err = format("unmatch %s type to %s", typename(sl), typename(l));
  return false;
}

bool imm_pop_type(Type* l) {
  char* err;
  bool b = imm_pop_type_result(l, &err);
  if (err != NULL) error("%s", err);
  return b;
}

bool global_pop_type_result(Type* l, char** err) {
  *err = NULL;
  if (state) {
    if (stacklen(comp_typeout) == 0) {
      // pop_type for word argument.
      push(comp_typein, l);
      return true;
    }
    // consume current stack type.
    Type* sl = pop(comp_typeout);
    if (!eqtype(l, sl)) {
      *err = format("unmatch %s type to %s", typename(sl), typename(l));
      return false;
    }
    if (is_polytype(l)) replace_typeref(l, sl);
    if (is_polytype(sl)) replace_typeref(sl, l);
    return false;
  } else {
    return imm_pop_type_result(l, err);
  }
}

bool global_pop_type(Type* l) {
  char* err;
  bool b = global_pop_type_result(l, &err);
  if (err != NULL) error("%s", err);
  return b;
}

void apply_in_result(Def* def, Stack* in, char** err) {
  *err = NULL;
  for (size_t i=0; i<stacklen(in); i++) {
    Type* t = get(in, i);
    global_pop_type(t);
    Def* eff = search_def(t->name);
    if (eff == NULL) {
      *err = format("undefined %s word in apply-freeze", t->name);
      return;
    }
    if (!codestate && state) push(def->effects, new_eff(eff, EFF_IN));
  }
}

void apply_out_result(Def* def, Stack* out, char** err) {
  *err = NULL;
  for (size_t i=0; i<stacklen(out); i++) {
    Type* t = get(out, i);
    global_push_type(t);
    Def* eff = search_def(t->name);
    if (eff == NULL) {
      *err = format("undefined %s word in apply-freeze", t->name);
      return;
    }
    if (!codestate && state) push(def->effects, new_eff(eff, EFF_OUT));
  }
}

bool eq_in(Stack* a, Stack* in) {
  if (stacklen(a) < stacklen(in)) return false;
  for (size_t i=0; i<stacklen(in); i++) {
    Type* l = get(a, stacklen(a)-i-1);
    Type* r = get(in, i);
    if (eqtype(l, r)) return true;
  }
  return false;
}

//
// for Definition
//

void apply_effects_result(Def* def, char** err) {
  *err = NULL;
  Def* wdef = last_def();
  if (!def->polymorphic) {
    assert(def->freezein != NULL);
    assert(def->freezeout != NULL);
    apply_in_result(wdef, def->freezein, err);
    apply_out_result(wdef, def->freezeout, err);
    return;
  }

  for (size_t i=0; i<stacklen(def->effects); i++) {
    Eff* eff = get(def->effects, i);
    spill_codestate();
    codestate = true;
    apply_effects(eff->def);
    restore_codestate();
    call_word(eff->def->wp);
    // imm_eval_def(eff->def); // FIXME:
    if (eff->kind == EFF_IMM) continue;
    global_pop_type(typet);
    // global_pop_type_result(typet, err);
    // if (err != NULL) return;
    Type* t = (Type*)pop_x();
    if (eff->kind == EFF_IN) global_pop_type_result(t, err);
    else global_push_type(t);
    if (*err != NULL) return;
  }

  if (!codestate) {
    for (size_t i=0; i<stacklen(def->effects); i++) {
      Eff* eff = get(def->effects, i);
      push(wdef->effects, eff);
    }
  }
}

void apply_effects(Def* def) {
  char* err;
  apply_effects_result(def, &err);
  if (err != NULL) error("%s", err);
}

void imm_apply_effects(Def* def) {
  bool tmpstate = state;
  state = false;
  apply_effects(def);
  state = tmpstate;
}

void spill_typestack() {
  spill_imm_typein = imm_typein;
  spill_imm_typeout = imm_typeout;
  spill_comp_typein = comp_typein;
  spill_comp_typeout = comp_typeout;
  imm_typein = new_stack();
  imm_typeout = new_stack();
  comp_typein = new_stack();
  comp_typeout = new_stack();
}

void restore_typestack() {
  imm_typein = spill_imm_typein;
  imm_typeout = spill_imm_typeout;
  comp_typein = spill_comp_typein;
  comp_typeout = spill_comp_typeout;
}

void add_int_effect() {
  if (codestate) return;
  Def* eff = search_def("Int");
  if (eff == NULL) error("undefined Int word");
  Def* wdef = last_def();
  push(wdef->effects, new_eff(eff, EFF_OUT));
}

void add_cstr_effect() {
  if (codestate) return;
  Def* eff = search_def("Cstr");
  if (eff == NULL) error("undefined Cstr word");
  Def* wdef = last_def();
  push(wdef->effects, new_eff(eff, EFF_OUT));
}

//
// trait solver
//

Def* solve_trait_word(Stack* defs) {
  for (size_t i=0; i<stacklen(defs); i++) {
    Def* def = get(defs, i);
    // if (def->polymorphic) error("%s impl is polymorphic", def->name);
    if (def->polymorphic) {
      char* err;
      spill_typestack();
      apply_effects_result(def, &err);
      restore_typestack();
      if (err == NULL) return def;
    } else {
      assert(def->freezein != NULL);
      if (eq_in(comp_typeout, def->freezein)) return def;
    }
  }
  return NULL;
}
