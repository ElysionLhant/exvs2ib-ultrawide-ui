; uwfix_hook.asm — entry stub for the ViewportPlug factory inline hook,
; plus forward stubs for the proxied nbamUsbFinder.dll exports.
EXTERN uwfix_on_factory:PROC
EXTERN uwfix_on_vpapply:PROC
EXTERN uwfix_on_lumen:PROC
EXTERN g_uwfix_tramp_slot:qword
EXTERN g_vp_tramp_slot:qword
EXTERN g_lq_tramp_slot:qword
EXTERN g_lt_tramp_slot:qword
EXTERN g_lnq_tramp_slot:qword
EXTERN g_lnt_tramp_slot:qword
EXTERN g_l2d_tramp_slot:qword
EXTERN g_fwd_table:qword

.code

uwfix_hook_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    ; 64 bytes pushed; caller's return address now at [rsp+40h]
    mov rax, [rsp + 40h]
    sub rsp, 28h
    mov qword ptr [rsp + 20h], rax   ; 5th arg = retaddr
    call uwfix_on_factory
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_uwfix_tramp_slot]
uwfix_hook_entry ENDP

; viewport_apply hook: rcx=state, rdx=slot, r8=rect; retaddr at [rsp]
; handler uwfix_on_vpapply(rcx, rdx, r8, r9=retaddr) — 4 args, all registers
uwfix_vp_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov r9, [rsp + 40h]              ; retaddr -> 4th arg
    sub rsp, 20h
    call uwfix_on_vpapply
    add rsp, 20h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_vp_tramp_slot]
uwfix_vp_entry ENDP

; lumen hooks: rcx=context, rdx=vertices, r8=retaddr, r9d=kind
uwfix_lumen_quad_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov r8, [rsp + 40h]
    mov r9d, 0
    sub rsp, 28h
    call uwfix_on_lumen
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_lq_tramp_slot]
uwfix_lumen_quad_entry ENDP

uwfix_lumen_tri_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov r8, [rsp + 40h]
    mov r9d, 1
    sub rsp, 28h
    call uwfix_on_lumen
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_lt_tramp_slot]
uwfix_lumen_tri_entry ENDP

uwfix_lumen_ndc_quad_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov r8, [rsp + 40h]
    mov r9d, 2
    sub rsp, 28h
    call uwfix_on_lumen
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_lnq_tramp_slot]
uwfix_lumen_ndc_quad_entry ENDP

uwfix_lumen_ndc_tri_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov r8, [rsp + 40h]
    mov r9d, 3
    sub rsp, 28h
    call uwfix_on_lumen
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_lnt_tramp_slot]
uwfix_lumen_ndc_tri_entry ENDP

; direct 2D command hook: r9=packet; pass packet+8 as NDC quad vertices
uwfix_lumen_2dcmd_entry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    mov rdx, [rsp + 10h]
    add rdx, 8
    mov r8, [rsp + 40h]
    mov r9d, 4
    sub rsp, 28h
    call uwfix_on_lumen
    add rsp, 28h
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp qword ptr [g_l2d_tramp_slot]
uwfix_lumen_2dcmd_entry ENDP

nbamUsbFinderGetSerialNumber PROC
    jmp qword ptr [g_fwd_table + 0*8]
nbamUsbFinderGetSerialNumber ENDP

nbamUsbFinderInitialize PROC
    jmp qword ptr [g_fwd_table + 1*8]
nbamUsbFinderInitialize ENDP

nbamUsbFinderRead PROC
    jmp qword ptr [g_fwd_table + 2*8]
nbamUsbFinderRead ENDP

nbamUsbFinderRelease PROC
    jmp qword ptr [g_fwd_table + 3*8]
nbamUsbFinderRelease ENDP

nbamUsbFinderWrite PROC
    jmp qword ptr [g_fwd_table + 4*8]
nbamUsbFinderWrite ENDP

nbamUsbFinder_GetInformation PROC
    jmp qword ptr [g_fwd_table + 5*8]
nbamUsbFinder_GetInformation ENDP

END
