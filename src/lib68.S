| 32-bit multiply and divide for the 68000, which has only 16-bit MULU/DIVU.
| gcc calls these by name. libgcc has them too, but its __divsi3 reaches __udivsi3
| with an ABSOLUTE jsr, which a code resource (loaded anywhere) cannot have; these
| only branch PC-relative. Calling convention: arguments on the stack, result in d0,
| d0-d1/a0-a1 scratch.

    .text

    .globl __mulsi3
__mulsi3:                               | (a * b) mod 2^32
    move.w  4(%sp),%d0                  | a.hi
    mulu.w  10(%sp),%d0                 | a.hi * b.lo
    move.w  6(%sp),%d1                  | a.lo
    mulu.w  8(%sp),%d1                  | a.lo * b.hi
    add.w   %d1,%d0
    swap    %d0
    clr.w   %d0
    move.w  6(%sp),%d1
    mulu.w  10(%sp),%d1                 | a.lo * b.lo
    add.l   %d1,%d0
    rts

| unsigned divide, registers in and out: d0 = n, d1 = d  ->  d0 = n / d, d1 = n % d
udivmod:
    movem.l %d2-%d3,-(%sp)
    move.l  %d1,%d3
    moveq   #0,%d1
    moveq   #31,%d2
1:  add.l   %d0,%d0                     | next dividend bit out the top ...
    addx.l  %d1,%d1                     | ... into the remainder
    cmp.l   %d3,%d1
    bcs.s   2f
    sub.l   %d3,%d1
    addq.l  #1,%d0                      | quotient bit into the freed low bit
2:  dbra    %d2,1b
    movem.l (%sp)+,%d2-%d3
    rts

    .globl __udivsi3
__udivsi3:
    move.l  4(%sp),%d0
    move.l  8(%sp),%d1
    bra.s   udivmod

    .globl __umodsi3
__umodsi3:
    move.l  4(%sp),%d0
    move.l  8(%sp),%d1
    bsr.s   udivmod
    move.l  %d1,%d0
    rts

    .globl __divsi3
__divsi3:                               | rounds toward zero, like C
    move.l  %d4,-(%sp)
    move.l  8(%sp),%d0
    move.l  12(%sp),%d1
    move.l  %d0,%d4
    eor.l   %d1,%d4                     | bit 31: the sign of the quotient
    tst.l   %d0
    bpl.s   1f
    neg.l   %d0
1:  tst.l   %d1
    bpl.s   2f
    neg.l   %d1
2:  bsr.s   udivmod
    tst.l   %d4
    bpl.s   3f
    neg.l   %d0
3:  move.l  (%sp)+,%d4
    rts

    .globl __modsi3
__modsi3:                               | takes the dividend's sign, like C
    move.l  %d4,-(%sp)
    move.l  8(%sp),%d0
    move.l  12(%sp),%d1
    move.l  %d0,%d4
    tst.l   %d0
    bpl.s   1f
    neg.l   %d0
1:  tst.l   %d1
    bpl.s   2f
    neg.l   %d1
2:  bsr.s   udivmod
    move.l  %d1,%d0
    tst.l   %d4
    bpl.s   3f
    neg.l   %d0
3:  move.l  (%sp)+,%d4
    rts
