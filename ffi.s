.intel_syntax noprefix;

// rdi=&sp rsi=wp
.global call_word_asm
call_word_asm:
  sub rsp, 8
  push rbx
  push rdi
  mov rbx, [rdi]
  call rsi
  pop rdi
  mov [rdi], rbx
  pop rbx
  add rsp, 8
  ret
