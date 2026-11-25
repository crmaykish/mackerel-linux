/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CPU exception entry stubs for the NOMMU Mackerel boards. Each one builds
 * pt_regs over the exception frame and passes its vector number to
 * mackerel_trap_c() (mackerel_traps.c), since the 68000 doesn't stack it.
 * The same kernel runs on a 68000 (SR + PC) or a 68010 (SR + PC + format word).
 *
 * Bus and address errors stack more than that:
 * - 68000: a function code word, the fault address and the instruction word,
 *   then SR + PC. The stub drops the first 8 bytes.
 * - 68010: a 29-word format 8 frame: SR, PC, format word, special status word,
 *   fault address, then internal state. The stub writes a format 0 frame into
 *   its last 8 bytes and drops the rest. The faulted cycle is never retried:
 *   the program gets a signal, or the kernel panics.
 * Either way the fault address is saved first. The frame is above SP, so an
 * interrupt can't overwrite it before it is read.
 */

#include <linux/linkage.h>
#include <asm/entry.h>

.text

.macro stub vec, fault=0
mackerel_trap\vec:
	.if \fault
	tstl	mackerel_68010
	jne	1f
	movel	%sp@(2),mackerel_fault_addr
	addql	#8,%sp
	jra	2f
1:	movel	%sp@(10),mackerel_fault_addr
	movew	%sp@,%sp@(50)		/* SR */
	movel	%sp@(2),%sp@(52)	/* PC */
	movew	#\vec*4,%sp@(56)	/* format 0, vector offset */
	lea	%sp@(50),%sp
2:
	.endif
	SAVE_ALL_INT
	movel	%sp,%sp@-		/* pt_regs */
	movel	#\vec,%sp@-		/* vector number */
	jbsr	mackerel_trap_c
	addql	#8,%sp
	jra	ret_from_exception
.endm

	stub	2, 1
	stub	3, 1
	.irp	vec, 4,5,6,7,8,9,10,11,14,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	stub	\vec
	.endr

/* Trap handler for mackerel_trap_init(): %a0 is SP from before the trap, so %d0 = bytes stacked */
	.globl	mackerel_frame_probe
mackerel_frame_probe:
	movel	%a0,%d0
	subl	%sp,%d0
	rte

/* (vector, handler) pairs for mackerel_trap_init(), zero-terminated */
	.section .rodata
	.align	2
	.globl	mackerel_trap_table
mackerel_trap_table:
	.irp	vec, 2,3,4,5,6,7,8,9,10,11,14,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	.long	\vec, mackerel_trap\vec
	.endr
	.long	0
