#include "hedon.h"

Stack* gspill_codestate;

void init_codegen() {
  gspill_codestate = new_stack();
}

void spill_codestate() {
  push(gspill_codestate, (void*)codestate);
}
void restore_codestate() {
  codestate = pop(gspill_codestate);
}
