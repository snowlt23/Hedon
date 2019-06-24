#include "hedon.h"

//
// globals
//

size_t typeidcnt = 0;

//
// functions
//

size_t newtypeid() {
  return typeidcnt++;
}

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
  return new_type(name, newtypeid());
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

Type* init_uniontype_ref(char* name, size_t id) {
  return init_reftype(init_uniontype(name, id));
}

Type* type_origin(Type* t) {
  if (t->kind == TYPE_REF) return type_origin(t->ref);
  return t;
}

int typeid(Type* t) {
  return type_origin(t)->id;
}

char* typename(Type* t) {
  return type_origin(t)->name;
}

bool eqtype(Type* a, Type* b) {
  if (typeid(a) == typeid(b)) return true;
  if (a->kind == TYPE_REF) return eqtype(a->ref, b);
  if (b->kind == TYPE_REF) return eqtype(a, b->ref);
  if (a->kind == TYPE_PARAM) return true;
  if (b->kind == TYPE_PARAM) return true;
  if (a->kind == TYPE_UNION) {
    for (size_t i=0; i<stacklen(a->types); i++) {
      if (eqtype(get(a->types, i), b)) return true;
    }
  }
  if (b->kind == TYPE_UNION) {
    for (size_t i=0; i<stacklen(b->types); i++) {
      if (eqtype(a, get(b->types, i))) return true;
    }
  }
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

bool eq_typestack(Stack* a, Stack* b) {
  if (stacklen(a) != stacklen(b)) return false;
  for (size_t i=0; i<stacklen(a); i++) {
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
  return type_origin(t)->kind != TYPE_SINGLE;
}

bool is_in_polytype(Type* t) {
  return type_origin(t)->kind != TYPE_SINGLE && type_origin(t)->kind != TYPE_UNION;
}

bool is_polymorphic(Stack* s) {
  for (size_t i=0; i<stacklen(s); i++) {
    Type* t = get(s, i);
    if (is_polytype(t)) return true;
  }
  return false;
}

bool is_in_polymorphic(Stack* s) {
  for (size_t i=0; i<stacklen(s); i++) {
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

void eff_types(Stack* effs, EffKind kind, int n, char** arr) {
  for (int i=0; i<n; i++) {
    Def* eff = search_def(arr[i]);
    if (eff == NULL) ierror("undefined %s eff-word", arr[i]);
    push(effs, new_eff(eff, kind));
  }
}
