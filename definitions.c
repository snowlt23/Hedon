#include "hedon.h"

Vocab* globaldefs;
Stack* searchlist;
Stack* definitions;

void init_definitions() {
  globaldefs = new_vocab("globals");
  searchlist = new_stack();
  definitions = new_stack();
  push(searchlist, globaldefs);
  push(definitions, globaldefs);
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
