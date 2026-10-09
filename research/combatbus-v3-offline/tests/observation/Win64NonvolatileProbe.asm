EXTERN g_asm_wrapper_return:QWORD
EXTERN g_asm_register_preservation_ok:DWORD
EXTERN g_asm_wrapper_target:QWORD

PUBLIC AbiCallWithNonvolatileSentinels

.const
ALIGN 16
xmm_sentinel_6  dq 0606060606060606h, 123456789ABCDEF0h
xmm_sentinel_7  dq 0707070707070707h, 123456789ABCDEF0h
xmm_sentinel_8  dq 0808080808080808h, 123456789ABCDEF0h
xmm_sentinel_9  dq 0909090909090909h, 123456789ABCDEF0h
xmm_sentinel_10 dq 1010101010101010h, 123456789ABCDEF0h
xmm_sentinel_11 dq 1111111111111111h, 123456789ABCDEF0h
xmm_sentinel_12 dq 1212121212121212h, 123456789ABCDEF0h
xmm_sentinel_13 dq 1313131313131313h, 123456789ABCDEF0h
xmm_sentinel_14 dq 1414141414141414h, 123456789ABCDEF0h
xmm_sentinel_15 dq 1515151515151515h, 123456789ABCDEF0h

.code

; Test-only Win64 caller. Saves the caller's nonvolatile state, loads unique sentinels,
; invokes the compiled C++ wrapper with the exact five-argument shape, then checks all
; Windows x64 nonvolatile GPRs and XMM6-XMM15 low 128-bit lanes before restoring caller state.
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
    sub  rsp, 0D8h
    .allocstack 0D8h
    movdqu xmmword ptr [rsp+30h], xmm6
    .savexmm128 xmm6, 30h
    movdqu xmmword ptr [rsp+40h], xmm7
    .savexmm128 xmm7, 40h
    movdqu xmmword ptr [rsp+50h], xmm8
    .savexmm128 xmm8, 50h
    movdqu xmmword ptr [rsp+60h], xmm9
    .savexmm128 xmm9, 60h
    movdqu xmmword ptr [rsp+70h], xmm10
    .savexmm128 xmm10, 70h
    movdqu xmmword ptr [rsp+80h], xmm11
    .savexmm128 xmm11, 80h
    movdqu xmmword ptr [rsp+90h], xmm12
    .savexmm128 xmm12, 90h
    movdqu xmmword ptr [rsp+0A0h], xmm13
    .savexmm128 xmm13, 0A0h
    movdqu xmmword ptr [rsp+0B0h], xmm14
    .savexmm128 xmm14, 0B0h
    movdqu xmmword ptr [rsp+0C0h], xmm15
    .savexmm128 xmm15, 0C0h
    .endprolog

    mov  rax, qword ptr [rsp+140h]
    mov  qword ptr [rsp+20h], rax

    mov  rbx, 1122334455667788h
    mov  rbp, 2233445566778899h
    mov  rsi, 33445566778899AAh
    mov  rdi, 445566778899AABBh
    mov  r12, 5566778899AABBCCh
    mov  r13, 66778899AABBCCDDh
    mov  r14, 778899AABBCCDDEEh
    mov  r15, 123456789ABCDEF0h

    movdqu xmm6, xmmword ptr [xmm_sentinel_6]
    movdqu xmm7, xmmword ptr [xmm_sentinel_7]
    movdqu xmm8, xmmword ptr [xmm_sentinel_8]
    movdqu xmm9, xmmword ptr [xmm_sentinel_9]
    movdqu xmm10, xmmword ptr [xmm_sentinel_10]
    movdqu xmm11, xmmword ptr [xmm_sentinel_11]
    movdqu xmm12, xmmword ptr [xmm_sentinel_12]
    movdqu xmm13, xmmword ptr [xmm_sentinel_13]
    movdqu xmm14, xmmword ptr [xmm_sentinel_14]
    movdqu xmm15, xmmword ptr [xmm_sentinel_15]

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

    pcmpeqb xmm6, xmmword ptr [xmm_sentinel_6]
    pmovmskb eax, xmm6
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm7, xmmword ptr [xmm_sentinel_7]
    pmovmskb eax, xmm7
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm8, xmmword ptr [xmm_sentinel_8]
    pmovmskb eax, xmm8
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm9, xmmword ptr [xmm_sentinel_9]
    pmovmskb eax, xmm9
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm10, xmmword ptr [xmm_sentinel_10]
    pmovmskb eax, xmm10
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm11, xmmword ptr [xmm_sentinel_11]
    pmovmskb eax, xmm11
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm12, xmmword ptr [xmm_sentinel_12]
    pmovmskb eax, xmm12
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm13, xmmword ptr [xmm_sentinel_13]
    pmovmskb eax, xmm13
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm14, xmmword ptr [xmm_sentinel_14]
    pmovmskb eax, xmm14
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm15, xmmword ptr [xmm_sentinel_15]
    pmovmskb eax, xmm15
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    mov  eax, 1

abi_probe_done:
    lea  r10, g_asm_register_preservation_ok
    mov  dword ptr [r10], eax
    movdqu xmm6, xmmword ptr [rsp+30h]
    movdqu xmm7, xmmword ptr [rsp+40h]
    movdqu xmm8, xmmword ptr [rsp+50h]
    movdqu xmm9, xmmword ptr [rsp+60h]
    movdqu xmm10, xmmword ptr [rsp+70h]
    movdqu xmm11, xmmword ptr [rsp+80h]
    movdqu xmm12, xmmword ptr [rsp+90h]
    movdqu xmm13, xmmword ptr [rsp+0A0h]
    movdqu xmm14, xmmword ptr [rsp+0B0h]
    movdqu xmm15, xmmword ptr [rsp+0C0h]
    add  rsp, 0D8h
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
