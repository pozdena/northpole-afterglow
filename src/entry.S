| The code resource starts here. After Dark JSRs to byte 0 with a Pascal call:
|
|   pascal OSErr main(Handle *storage, RgnHandle blankRgn, short message,
|                     GMParamBlockPtr params);
|
| Stack on entry: return address, params (4), message (2), blankRgn (4), storage (4),
| then 2 bytes for the result. This stub re-pushes them for gcc's C convention,
| calls ad_main, stores the result and pops the 14 bytes of arguments itself.

    .section .entry,"ax"
    .globl _start
_start:
    bra.s   1f
    | the header a THINK C code resource carries (Berkeley's modules have it)
    .short  0
    .ascii  "ADgm"
    .short  0
    .short  0, 0, 0
1:
    link.w  %fp,#0
    movem.l %d2-%d7/%a2-%a4,-(%sp)
    move.l  8(%fp),-(%sp)           | params
    move.w  12(%fp),%d0
    ext.l   %d0
    move.l  %d0,-(%sp)              | message
    move.l  14(%fp),-(%sp)          | blankRgn
    move.l  18(%fp),-(%sp)          | storage
    bsr.w   ad_main
    lea     16(%sp),%sp
    move.w  %d0,22(%fp)             | function result
    movem.l (%sp)+,%d2-%d7/%a2-%a4
    unlk    %fp
    move.l  (%sp)+,%a0
    lea     14(%sp),%sp
    jmp     (%a0)
