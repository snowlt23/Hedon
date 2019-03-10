.intel_syntax noprefix;

// rdi=&sp rsi=wp
.global call_word_asm
call_word_asm:
  push rbx
  push rax
  push rcx
  push rdx
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15
  mov rbx, [rdi]
  call rsi
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rdx
  pop rcx
  pop rax
  mov [rdi], rbx
  pop rbx
  ret
