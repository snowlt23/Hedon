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

//
// write to .text
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
