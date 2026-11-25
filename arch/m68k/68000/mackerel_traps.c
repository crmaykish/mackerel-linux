// SPDX-License-Identifier: GPL-2.0
// CPU exception handling for the NOMMU Mackerel boards (08, 10, F).
//
// The 68000 pushes no format/vector word, so the generic trap_c() can't tell
// which exception it was handed. Each vector gets a stub in mackerel_entry.S
// that passes its number here instead. A faulting user program gets the usual
// signal; a fault in the kernel is fatal. Mackerel-10 can also take a 68010,
// whose frames differ, so the CPU is detected at boot.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/mackerel.h>

// Fault address from a bus/address error frame, saved by its entry stub
unsigned long mackerel_fault_addr;

// Nonzero on a 68010: every frame carries a format word, and bus/address errors stack a format 8 frame
unsigned long mackerel_68010;

// (vector, handler) pairs, zero-terminated (mackerel_entry.S)
extern const unsigned long mackerel_trap_table[];

// Called from the entry stubs
asmlinkage void mackerel_trap_c(int vector, struct pt_regs *regs);
asmlinkage void mackerel_frame_probe(void);

asmlinkage void mackerel_trap_c(int vector, struct pt_regs *regs)
{
	unsigned long addr = regs->pc;
	int sig, code;

	if (vector == VEC_BUSERR || vector == VEC_ADDRERR)
		addr = mackerel_fault_addr;

	if (!user_mode(regs)) {
		pr_emerg("Kernel exception %d: pc=0x%08lx addr=0x%08lx\n", vector, regs->pc, addr);
		panic("Kernel exception");
	}

	switch (vector) {
	case VEC_BUSERR:
		sig = SIGBUS;
		code = BUS_ADRERR;
		break;
	case VEC_ADDRERR:
		sig = SIGBUS;
		code = BUS_ADRALN;
		break;
	case VEC_ZERODIV:
		sig = SIGFPE;
		code = FPE_INTDIV;
		break;
	case VEC_CHK:
	case VEC_TRAP:
		sig = SIGFPE;
		code = FPE_INTOVF;
		break;
	case VEC_PRIV:
		sig = SIGILL;
		code = ILL_PRVOPC;
		break;
	case VEC_TRACE:
		sig = SIGTRAP;
		code = TRAP_TRACE;
		break;
	case VEC_TRAP15:
		sig = SIGTRAP;
		code = TRAP_BRKPT;
		break;
	case VEC_TRAP1 ... VEC_TRAP14:
		sig = SIGILL;
		code = ILL_ILLTRP;
		break;
	default: // illegal instruction, line A, line F
		sig = SIGILL;
		code = ILL_ILLOPC;
		break;
	}

	pr_info_ratelimited("%s[%d]: exception %d at pc 0x%08lx, sending signal %d\n",
			    current->comm, task_pid_nr(current), vector, regs->pc, sig);
	force_sig_fault(sig, code, (void __user *)addr);
}

// A trap stacks 6 bytes on a 68000 (SR + PC) and 8 on a 68010 (+ format word)
static unsigned long __init trap_frame_bytes(void)
{
	e_vector old = _ramvec[VEC_TRAP15];
	unsigned long bytes;

	_ramvec[VEC_TRAP15] = (e_vector)mackerel_frame_probe;
	asm volatile("movel %%sp,%%a0\n\ttrap #15\n\tmovel %%d0,%0"
		     : "=g"(bytes) : : "d0", "a0", "memory");
	_ramvec[VEC_TRAP15] = old;
	return bytes;
}

void __init mackerel_trap_init(void)
{
	static bool detected;
	const unsigned long *p;

	if (!detected) {
		mackerel_68010 = trap_frame_bytes() == 8;
		pr_info("CPU exception frames: %s\n", mackerel_68010 ? "68010" : "68000");
		detected = true;
	}
	for (p = mackerel_trap_table; *p; p += 2)
		_ramvec[p[0]] = (e_vector)p[1];
}
