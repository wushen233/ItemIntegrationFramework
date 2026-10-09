EXTERN g_asm_wrapper_return:QWORD
EXTERN g_asm_register_preservation_ok:DWORD
EXTERN g_asm_xmm_restore_ok:DWORD
EXTERN g_asm_corruption_mode:DWORD
EXTERN g_asm_xmm6_mask:DWORD
EXTERN g_asm_outer_gpr_restore_ok:DWORD
EXTERN g_asm_wrapper_target:QWORD

PUBLIC AbiCallWithNonvolatileSentinels
PUBLIC AbiCallProbeAndVerifyGprs

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

    ; Test-only negative injection after the wrapper returns. The epilogue below
    ; still restores every nonvolatile register saved by this probe.
    cmp  dword ptr [g_asm_corruption_mode], 1
    je   abi_probe_corrupt_gpr
    cmp  dword ptr [g_asm_corruption_mode], 2
    je   abi_probe_corrupt_xmm
    jmp  abi_probe_check

abi_probe_corrupt_gpr:
    xor  ebx, ebx
    jmp  abi_probe_check

abi_probe_corrupt_xmm:
    ; xmm6_sentinel has byte 0 == 06h and all other bytes nonzero. MOVD
    ; therefore creates an exact one-bit PMOVMSKB match (mask == 1).
    mov  eax, 6
    movd xmm6, eax

abi_probe_check:
    xor  eax, eax

    mov  r11, 1122334455667788h
    cmp  rbx, r11
    jne  abi_probe_failed
    mov  r11, 2233445566778899h
    cmp  rbp, r11
    jne  abi_probe_failed
    mov  r11, 33445566778899AAh
    cmp  rsi, r11
    jne  abi_probe_failed
    mov  r11, 445566778899AABBh
    cmp  rdi, r11
    jne  abi_probe_failed
    mov  r11, 5566778899AABBCCh
    cmp  r12, r11
    jne  abi_probe_failed
    mov  r11, 66778899AABBCCDDh
    cmp  r13, r11
    jne  abi_probe_failed
    mov  r11, 778899AABBCCDDEEh
    cmp  r14, r11
    jne  abi_probe_failed
    mov  r11, 123456789ABCDEF0h
    cmp  r15, r11
    jne  abi_probe_failed

    pcmpeqb xmm6, xmmword ptr [xmm_sentinel_6]
    pmovmskb eax, xmm6
    mov  dword ptr [g_asm_xmm6_mask], eax
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm7, xmmword ptr [xmm_sentinel_7]
    pmovmskb eax, xmm7
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm8, xmmword ptr [xmm_sentinel_8]
    pmovmskb eax, xmm8
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm9, xmmword ptr [xmm_sentinel_9]
    pmovmskb eax, xmm9
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm10, xmmword ptr [xmm_sentinel_10]
    pmovmskb eax, xmm10
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm11, xmmword ptr [xmm_sentinel_11]
    pmovmskb eax, xmm11
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm12, xmmword ptr [xmm_sentinel_12]
    pmovmskb eax, xmm12
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm13, xmmword ptr [xmm_sentinel_13]
    pmovmskb eax, xmm13
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    pcmpeqb xmm14, xmmword ptr [xmm_sentinel_14]
    pmovmskb eax, xmm14
    cmp  eax, 0FFFFh
    jne  abi_probe_done
    pcmpeqb xmm15, xmmword ptr [xmm_sentinel_15]
    pmovmskb eax, xmm15
    cmp  eax, 0FFFFh
    jne  abi_probe_failed
    mov  eax, 1
    jmp  abi_probe_done

abi_probe_failed:
    xor  eax, eax

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
	xor  r10d, r10d
	movdqu xmm0, xmmword ptr [rsp+30h]
	pcmpeqb xmm0, xmm6
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+40h]
	pcmpeqb xmm0, xmm7
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+50h]
	pcmpeqb xmm0, xmm8
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+60h]
	pcmpeqb xmm0, xmm9
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+70h]
	pcmpeqb xmm0, xmm10
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+80h]
	pcmpeqb xmm0, xmm11
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+90h]
	pcmpeqb xmm0, xmm12
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+0A0h]
	pcmpeqb xmm0, xmm13
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+0B0h]
	pcmpeqb xmm0, xmm14
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	movdqu xmm0, xmmword ptr [rsp+0C0h]
	pcmpeqb xmm0, xmm15
	pmovmskb eax, xmm0
	cmp  eax, 0FFFFh
	jne  xmm_restore_done
	mov  r10d, 1

xmm_restore_done:
	lea  rax, g_asm_xmm_restore_ok
	mov  dword ptr [rax], r10d
	lea  r10, g_asm_register_preservation_ok
    mov  eax, dword ptr [r10]
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

; Outer ABI verifier loads sentinels into every nonvolatile GPR before invoking
; the probe, then checks those sentinels after the probe returns. This proves
; even deliberate inner failures do not leak a corrupted GPR to the caller.
AbiCallProbeAndVerifyGprs PROC FRAME
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
    sub  rsp, 68h
    .allocstack 68h
    .endprolog

    mov  qword ptr [rsp+28h], rcx
    mov  qword ptr [rsp+30h], rdx
    mov  qword ptr [rsp+38h], r8
    mov  qword ptr [rsp+40h], r9
    mov  r10, qword ptr [rsp+0D0h]
    mov  qword ptr [rsp+20h], r10
    mov  qword ptr [rsp+48h], r10

    mov  rbx, 1122334455667788h
    mov  rbp, 2233445566778899h
    mov  rsi, 33445566778899AAh
    mov  rdi, 445566778899AABBh
    mov  r12, 5566778899AABBCCh
    mov  r13, 66778899AABBCCDDh
    mov  r14, 778899AABBCCDDEEh
    mov  r15, 123456789ABCDEF0h

    mov  rcx, qword ptr [rsp+28h]
    mov  rdx, qword ptr [rsp+30h]
    mov  r8,  qword ptr [rsp+38h]
    mov  r9,  qword ptr [rsp+40h]
    call AbiCallWithNonvolatileSentinels
    mov  qword ptr [rsp+50h], rax

    mov  r11, 1122334455667788h
    cmp  rbx, r11
    jne  outer_gpr_restore_failed
    mov  r11, 2233445566778899h
    cmp  rbp, r11
    jne  outer_gpr_restore_failed
    mov  r11, 33445566778899AAh
    cmp  rsi, r11
    jne  outer_gpr_restore_failed
    mov  r11, 445566778899AABBh
    cmp  rdi, r11
    jne  outer_gpr_restore_failed
    mov  r11, 5566778899AABBCCh
    cmp  r12, r11
    jne  outer_gpr_restore_failed
    mov  r11, 66778899AABBCCDDh
    cmp  r13, r11
    jne  outer_gpr_restore_failed
    mov  r11, 778899AABBCCDDEEh
    cmp  r14, r11
    jne  outer_gpr_restore_failed
    mov  r11, 123456789ABCDEF0h
    cmp  r15, r11
    jne  outer_gpr_restore_failed
    mov  r10d, 1
    jmp  outer_gpr_restore_done

outer_gpr_restore_failed:
    xor  r10d, r10d

outer_gpr_restore_done:
    lea  r11, g_asm_outer_gpr_restore_ok
    mov  dword ptr [r11], r10d
    mov  rax, qword ptr [rsp+50h]
    add  rsp, 68h
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rdi
    pop  rsi
    pop  rbp
    pop  rbx
    ret
AbiCallProbeAndVerifyGprs ENDP

END
