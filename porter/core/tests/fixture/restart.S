// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// Synthetic restart initializer. Loads packed signed x/y through a double register, as the game does.
.text
.global _ZN11CScriptMain19DefineRestartPointsEv
.type _ZN11CScriptMain19DefineRestartPointsEv, %function
_ZN11CScriptMain19DefineRestartPointsEv:
    sub sp, sp, #32
    str x30, [sp, #24]
.macro point x, y, z, heading
    adrp x8, .Lpair\@
    add x8, x8, :lo12:.Lpair\@
    ldr d0, [x8]
    str d0, [sp]
    mov w8, #\z
    str w8, [sp, #8]
    mov x1, sp
    mov w2, #\heading
    bl AddHospitalRestartPoint
    .pushsection .rodata
    .balign 8
.Lpair\@:
    .long \x, \y
    .popsection
.endm
    point -4096, 8192, 123, 90
    point 2048, -1024, 0, -90
    point -1, -2, 456, 180
    point 0, 0, 1, 0
    point 2147483647, -2147483648, 789, 270
    ldr x30, [sp, #24]
    add sp, sp, #32
    ret
.size _ZN11CScriptMain19DefineRestartPointsEv, .-_ZN11CScriptMain19DefineRestartPointsEv
.section .note.GNU-stack,"",%progbits
