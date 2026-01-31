add r1, r0, r0
add r2, r0, r0
lw  r3, 0(r2)
add r4, r3, r3
sw  r4, 4(r2)
beq r4, r0, 2
sub r5, r4, r3
nop
