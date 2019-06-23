#include "hedon.h"

char* vformat(char* fmt, va_list ap) {
  char buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  return strdup(buf);
}

char* format(char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* s = vformat(fmt, ap);
  va_end(ap);
  return s;
}

void ierror(char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* s = vformat(fmt, ap);
  va_end(ap);
  fprintf(stderr, "internal error: %s", s);
  exit(1);
}

void error(char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* s = vformat(fmt, ap);
  va_end(ap);
  s = format("error in %s: %s at %s", last_def()->name, s, intoken->name);
  Def* def = search_def("trap.error");
  if (def != NULL) {
    imm_push_type(cstr_type());
    push_x((size_t)s);
    imm_apply_effects(def);
    call_word(def->wp);
  } else {
    fprintf(stderr, "%s", s);
    exit(1);
  }
}
