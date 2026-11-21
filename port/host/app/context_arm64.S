#if defined(__aarch64__)
#ifdef __APPLE__
#define JUMP _khdays_context_jump_arm64
#else
#define JUMP khdays_context_jump_arm64
#endif
.text
.p2align 2
.globl JUMP
JUMP:
    stp x19, x20, [x0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    mrs x10, fpcr
    stp x9, x10, [x0, #96]
    stp d8, d9, [x0, #112]
    stp d10, d11, [x0, #128]
    stp d12, d13, [x0, #144]
    stp d14, d15, [x0, #160]
    ldp x19, x20, [x1]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldp x9, x10, [x1, #96]
    mov sp, x9
    msr fpcr, x10
    ldp d8, d9, [x1, #112]
    ldp d10, d11, [x1, #128]
    ldp d12, d13, [x1, #144]
    ldp d14, d15, [x1, #160]
    ret
#endif
#if defined(__linux__)
.section .note.GNU-stack,"",%progbits
#endif
