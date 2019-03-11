.intel_syntax noprefix;

// rdi=&sp rsi=wp
.global call_word_asm
call_word_asm:
  push rsp
  push rbx
  push rdi
  mov rbx, [rdi]
align_stack:
  mov rax, rsp
  mov rcx, 16
  mov rdx, 0
  div rcx
  cmp rdx, 0
  je align_f
align_t:
  call rsi
  jmp align_e
align_f:
  sub rsp, 8
  call rsi
  add rsp, 8
align_e:
  pop rdi
  mov [rdi], rbx
  pop rbx
  pop rsp
  ret
