EXTERN g_asm_wrapper_return:QWORD
EXTERN g_asm_register_preservation_ok:DWORD
EXTERN g_asm_wrapper_target:QWORD

PUBLIC AbiCallWithNonvolatileSentinels
.code

; Test-only Win64 caller. Saves the caller's nonvolatile state, loads unique sentinels,
; invokes the compiled C++ wrapper with the exact five-argument shape, then checks all
; Windows x64 nonvolatile GPRs before restoring the original caller state.
AbiCallWithNonvolatileSentinels PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub  rsp, 28h
    .allocstack 28h
    .endprolog

    mov  rax, qword ptr [rsp+90h]
    mov  qword ptr [rsp+20h], rax

    mov  rbx, 1122334455667788h
    mov  rbp, 2233445566778899h
    mov  rsi, 33445566778899AAh
    mov  rdi, 445566778899AABBh
    mov  r12, 5566778899AABBCCh
    mov  r13, 66778899AABBCCDDh
    mov  r14, 778899AABBCCDDEEh
    mov  r15, 123456789ABCDEF0h

    mov  rax, qword ptr [g_asm_wrapper_target]
    call rax

    lea  r10, g_asm_wrapper_return
    mov  qword ptr [r10], rax
    xor  eax, eax

    mov  r11, 1122334455667788h
    cmp  rbx, r11
    jne  abi_probe_done
    mov  r11, 2233445566778899h
    cmp  rbp, r11
    jne  abi_probe_done
    mov  r11, 33445566778899AAh
    cmp  rsi, r11
    jne  abi_probe_done
    mov  r11, 445566778899AABBh
    cmp  rdi, r11
    jne  abi_probe_done
    mov  r11, 5566778899AABBCCh
    cmp  r12, r11
    jne  abi_probe_done
    mov  r11, 66778899AABBCCDDh
    cmp  r13, r11
    jne  abi_probe_done
    mov  r11, 778899AABBCCDDEEh
    cmp  r14, r11
    jne  abi_probe_done
    mov  r11, 123456789ABCDEF0h
    cmp  r15, r11
    jne  abi_probe_done
    mov  eax, 1

abi_probe_done:
    lea  r10, g_asm_register_preservation_ok
    mov  dword ptr [r10], eax
    add  rsp, 28h
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rdi
    pop  rsi
    pop  rbp
    pop  rbx
    ret
AbiCallWithNonvolatileSentinels ENDP

END
