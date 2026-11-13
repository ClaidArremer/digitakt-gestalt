| SPDX-License-Identifier: GPL-2.0-or-later
| RADS's sample loop for a block, in assembly: rads.c's rd_frames (the C is the reference, and
| the host test runs it), here with the state in registers. Stereo (GESTALT 4.4): each channel
| through the converter, the table, the DC block, even and the blend; the hold's clock and the
| level measurements are shared.
|
| unsigned rd_frames_asm(const int32_t *in, int32_t *out, struct rd_chs *s, const struct rd_k *k,
|                        unsigned *pinp, unsigned *poutp)
| in and out: 32 frames, interleaved L, R. s: two channels' state. Returns the hold's phase.
|
|   d2 idx L  d3 dx L  d4 dy L   d5 idx R  d6 dx R  d7 dy R   d0 d1 scratch
|   a0 in   a1 out   a2 ta   a3 tb   a4 k   a5 the end of out   a6 the hold's phase
|   locals: 44(sp) pin, 48(sp) pout, 52(sp) frames to the next level reading, 56(sp) the blend's
|   ramp (dwm (i + 1)), 60(sp) this frame's blend
|
| struct rd_k: g 0, lim 4, ev 8, f 12, inc 16, wm0 20, dwm 24, full 28, ph 32, ta 36, tb 40, sh 44
| struct rd_chs: idx 0, dx 4, dy 8 (left), then the right at 12.

        .equ    K_G, 0
        .equ    K_LIM, 4
        .equ    K_EV, 8
        .equ    K_F, 12
        .equ    K_INC, 16
        .equ    K_WM0, 20
        .equ    K_DWM, 24
        .equ    K_FULL, 28
        .equ    K_PH, 32
        .equ    K_TA, 36
        .equ    K_TB, 40
        .equ    K_SH, 44
        .equ    KNEE, 0x600000
        .equ    LMAX, 0x740000

| the converter: x24 at OFF(a0) into the 8-bit index in IDX (preamp, clip)
        .macro  INDEX off, idx
        move.l  \off(%a0), %d0
        asr.l   #8, %d0
        asr.l   #8, %d0                 | x16
        cmp.l   K_LIM(%a4), %d0
        blt.s   1f
        move.l  #255, \idx
        bra.s   4f
1:      move.l  K_LIM(%a4), %d1
        neg.l   %d1
        cmp.l   %d1, %d0
        bgt.s   2f
        moveq   #0, \idx
        bra.s   4f
2:      muls.l  K_G(%a4), %d0
        addi.l  #0x808000, %d0
        swap    %d0
        ext.l   %d0
        cmpi.l  #255, %d0               | just under the clip it can round to 256
        ble.s   3f
        move.l  #255, %d0
3:      move.l  %d0, \idx
4:
        .endm

| the table at IDX (crossfading while f), then the DC block on DX / DY
        .macro  TABLE idx, dx, dy, fast
        mvz.b   (0, %a2, \idx\().l), %d0
        .if \fast
        lsl.l   #8, %d0
        .else
        tst.l   K_F(%a4)
        beq.s   4f
        mvz.b   (0, %a3, \idx\().l), %d1 | crossfading: + (tb - ta) f
        sub.l   %d0, %d1
        muls.l  K_F(%a4), %d1
        lsl.l   #8, %d0
        add.l   %d1, %d0
        bra.s   5f
4:      lsl.l   #8, %d0
5:
        .endif
        addi.l  #-32640, %d0
        lsl.l   #4, %d0                 | t
        move.l  \dy, %d1                | DC block: dy += t - dx - leak, leak = dy >> 11,
        asr.l   #8, %d1                 |   at least 1 while dy > 0 (it settles at 0)
        asr.l   #3, %d1
        bne.s   9f
        tst.l   \dy
        ble.s   9f
        moveq   #1, %d1
9:      sub.l   %d1, \dy
        add.l   %d0, \dy
        sub.l   \dx, \dy
        move.l  %d0, \dx
        .endm

| even on DY, the blend against the dry at OFF(a0) unless fully wet, the ceiling, out
        .macro  OUT off, dy, fast
        move.l  \dy, %d0                | even: ((dy >> 4) ev) >> 3
        asr.l   #4, %d0
        muls.l  K_EV(%a4), %d0
        asr.l   #3, %d0
        .if !\fast
        tst.l   K_FULL(%a4)
        bne.s   6f
        move.l  \off(%a0), %d1          | the blend: x24 + (((w - x24) >> 7) wm) >> 3
        asr.l   #8, %d1
        sub.l   %d1, %d0
        asr.l   #7, %d0
        muls.l  60(%sp), %d0
        asr.l   #3, %d0
        add.l   %d1, %d0
6:
        .endif
        cmpi.l  #KNEE, %d0              | the ceiling: the common case falls straight through
        bgt.s   7f
        cmpi.l  #-KNEE, %d0
        bge.s   8f
        move.l  #-KNEE, %d1
        sub.l   %d0, %d1
        asr.l   #1, %d1
        move.l  #-KNEE, %d0
        sub.l   %d1, %d0
        cmpi.l  #-LMAX, %d0
        bge.s   8f
        move.l  #-LMAX, %d0
        bra.s   8f
7:      subi.l  #KNEE, %d0
        asr.l   #1, %d0
        addi.l  #KNEE, %d0
        cmpi.l  #LMAX, %d0
        ble.s   8f
        move.l  #LMAX, %d0
8:      lsl.l   #8, %d0
        move.l  %d0, (%a1)+
        .endm

| the frames: FAST, with no table change under way and BLEND fully wet (most of the time), leaves
| out their tests
        .macro  FRAMES fast
.Lframe\@:
        tst.l   K_SH(%a4)               | the hold: new indexes only when its clock ticks
        beq.s   .Lnew\@
        adda.l  K_INC(%a4), %a6
        cmpa.l  #65536, %a6
        bcs.w   .Lheld\@
        move.l  %a6, %d0
        andi.l  #0xffff, %d0
        movea.l %d0, %a6
.Lnew\@:
        INDEX   0, %d2
        INDEX   4, %d5
.Lheld\@:
        TABLE   %d2, %d3, %d4, \fast
        TABLE   %d5, %d6, %d7, \fast
        subq.l  #1, 52(%sp)             | the levels, a frame in eight
        bne.s   .Lnolev\@
        moveq   #8, %d0
        move.l  %d0, 52(%sp)
        move.l  (%a0), %d0              | the dry: ((xl + xr) >> 1) >> 8, squared >> 8
        asr.l   #8, %d0
        move.l  4(%a0), %d1
        asr.l   #8, %d1
        add.l   %d1, %d0
        asr.l   #1, %d0
        asr.l   #8, %d0
        muls.l  %d0, %d0
        asr.l   #8, %d0
        add.l   %d0, 44(%sp)
        move.l  %d4, %d0                | the wet: ((dyl + dyr) >> 1) >> 6, squared >> 4
        add.l   %d7, %d0
        asr.l   #1, %d0
        asr.l   #6, %d0
        muls.l  %d0, %d0
        asr.l   #4, %d0
        add.l   %d0, 48(%sp)
.Lnolev\@:
        .if !\fast
        tst.l   K_FULL(%a4)             | the blend for this frame: wm0 + (dwm (i + 1)) >> 11
        bne.s   .Lout\@
        move.l  56(%sp), %d0
        add.l   K_DWM(%a4), %d0
        move.l  %d0, 56(%sp)
        asr.l   #8, %d0
        asr.l   #3, %d0
        add.l   K_WM0(%a4), %d0
        move.l  %d0, 60(%sp)
.Lout\@:
        .endif
        OUT     0, %d4, \fast
        OUT     4, %d7, \fast
        addq.l  #8, %a0
        cmpa.l  %a5, %a1
        bne.w   .Lframe\@

        .endm

        .section .run, "ax"
        .globl  rd_frames_asm
rd_frames_asm:
        lea     -64(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        movea.l 68(%sp), %a0            | in
        movea.l 72(%sp), %a1            | out
        movea.l 76(%sp), %a5            | s
        movea.l 80(%sp), %a4            | k
        move.l  (%a5), %d2
        move.l  4(%a5), %d3
        move.l  8(%a5), %d4
        move.l  12(%a5), %d5
        move.l  16(%a5), %d6
        move.l  20(%a5), %d7
        movea.l K_TA(%a4), %a2
        movea.l K_TB(%a4), %a3
        movea.l K_PH(%a4), %a6
        lea     256(%a1), %a5           | the end of out
        clr.l   44(%sp)
        clr.l   48(%sp)
        moveq   #1, %d0
        move.l  %d0, 52(%sp)            | the first frame is read
        clr.l   56(%sp)

        tst.l   K_F(%a4)                | the fast loop, or the full one
        bne.w   .Lfull
        tst.l   K_FULL(%a4)
        beq.w   .Lfull
        FRAMES  1
        bra.w   .Ldone
.Lfull:
        FRAMES  0
.Ldone:
        movea.l 76(%sp), %a5            | the state back
        move.l  %d2, (%a5)
        move.l  %d3, 4(%a5)
        move.l  %d4, 8(%a5)
        move.l  %d5, 12(%a5)
        move.l  %d6, 16(%a5)
        move.l  %d7, 20(%a5)
        movea.l 84(%sp), %a5
        move.l  44(%sp), %d0
        add.l   %d0, (%a5)
        movea.l 88(%sp), %a5
        move.l  48(%sp), %d0
        add.l   %d0, (%a5)
        move.l  %a6, %d0
        movem.l (%sp), %d2-%d7/%a2-%a6
        lea     64(%sp), %sp
        rts
