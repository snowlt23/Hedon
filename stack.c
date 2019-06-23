#include "hedon.h"

Stack* new_stack_cap(size_t size) {
  Stack* s = malloc(sizeof(Stack));
  s->s = malloc(size);
  s->p = s->s + size;
  s->cap = size;
  return s;
}

Stack* new_stack() {
  return new_stack_cap(DEFAULT_STACK_SIZE);
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
  if (!(s->s <= s->p)) error("stack overflow");
  pushT(s, void*, v);
}
void* pop(Stack* s) {
  if (!(s->s+s->cap >= s->p)) error("stack underflow");
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
