#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/page.h>
#include <sys/time.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/reversemap.h>


static struct Taskstate ts;

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};

static long long counter = 0;

static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}


void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.

	// vectors[] is defined in kern/trapentry.S
	// see the comments there for important information about its data layout
	extern unsigned int vectors[];
	extern void T_DEFAULT_HANDLER(), T_SYSCALL_HANDLER(), T_BRKPT_HANDLER();    // trap handlers defined in kern/trapentry.S that we must make special references to
	int i;
	unsigned int *vector = vectors;

	// iterate through all possible 256 vectors, defining an IDT entry for each one
	// at each step, vector points to the next defined trap number that has yet to be set in the IDT
	// here we rely on the fact that, in kern/trapentry.S, TRAPHANDLER was called with increasing trap numbers
	for(i = 0; i < 256; i++) {
		if (*vector == i) {
			/*
			 * in this case, vector points to the trap number we must now set in the IDT,
			 * and vector+1 points to the address of the trap handler for this trap
			 */
			if (i >= IRQ_OFFSET && i < IRQ_OFFSET + 16) {
				// processor never checks DPL of the IDT entry when invoking a hardware interrupt handler
				SETGATE(idt[i], 0, GD_KT, *(vector+1), 3);
			}
			else {
				SETGATE(idt[i], 0, GD_KT, *(vector+1), 0);
			}
			vector += 2;	// make vector point to the next trap
		}
		else {
			/*
			 * in this case, there is no defined trap for this vector
			 * set the IDT entry to point to the default trap handler
			 */
			SETGATE(idt[i], 0, GD_KT, &T_DEFAULT_HANDLER, 0);
		}
	}
	// these are special, since they are traps we allow the user to call
	SETGATE(idt[T_SYSCALL], 0, GD_KT, &T_SYSCALL_HANDLER, 3);
	SETGATE(idt[T_BRKPT], 0, GD_KT, &T_BRKPT_HANDLER, 3);

	// Per-CPU setup 
	trap_init_percpu();
}

// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	// The example code here sets up the Task State Segment (TSS) and
	// the TSS descriptor for CPU 0. But it is incorrect if we are
	// running on other CPUs because each CPU has its own kernel stack.
	// Fix the code so that it works for all CPUs.
	//
	// Hints:
	//   - The macro "thiscpu" always refers to the current CPU's
	//     struct CpuInfo;
	//   - The ID of the current CPU is given by cpunum() or
	//     thiscpu->cpu_id;
	//   - Use "thiscpu->cpu_ts" as the TSS for the current CPU,
	//     rather than the global "ts" variable;
	//   - Use gdt[(GD_TSS0 >> 3) + i] for CPU i's TSS descriptor;
	//   - You mapped the per-CPU kernel stacks in mem_init_mp()
	//
	// ltr sets a 'busy' flag in the TSS selector, so if you
	// accidentally load the same TSS on more than one CPU, you'll
	// get a triple fault.  If you set up an individual CPU's TSS
	// wrong, you may not get a fault until you try to return from
	// user space on that CPU.
	//
	// LAB 4: Your code here:

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - (KSTKSIZE + KSTKGAP) * thiscpu->cpu_id;
	thiscpu->cpu_ts.ts_ss0 = GD_KD;

	// Initialize the TSS slot of the gdt.
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id] = SEG16(STS_T32A, (uint32_t) (&(thiscpu->cpu_ts)),
					sizeof(struct Taskstate), 0);
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0 + (thiscpu->cpu_id << 3));

	// Load the IDT
	lidt(&idt_pd);
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p from CPU %d\n", tf, cpunum());
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  esp  0x%08x\n", tf->tf_esp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	static int page_to_age = 0;
	int page_to_age_first = page_to_age;
	int num_page_updates = NPAGEUPDATES_FACTOR*NPAGESFREE_LOW_THRESHOLD;
	struct PteChain *pp_refs_chain;
	char page_accessed;

	// Handle processor exceptions.
	// LAB 3: Your code here.
	if (tf->tf_trapno == T_PGFLT) {
		page_fault_handler(tf);
		return;
	}
	else if (tf->tf_trapno == T_BRKPT) {
		monitor(tf);    // breakpoint exceptions invoke the kernel monitor
		return;
	}
	else if (tf->tf_trapno == T_SYSCALL) {
		tf->tf_regs.reg_eax = syscall(tf->tf_regs.reg_eax, tf->tf_regs.reg_edx, tf->tf_regs.reg_ecx, tf->tf_regs.reg_ebx, tf->tf_regs.reg_edi, tf->tf_regs.reg_esi);
		return;
	}

	// Handle spurious interrupts
	// The hardware sometimes raises these because of noise on the
	// IRQ line or other reasons. We don't care.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	// Handle clock interrupts. Don't forget to acknowledge the
	// interrupt using lapic_eoi() before calling the scheduler!
	// LAB 4: Your code here.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
		lapic_eoi();

		// Update the age of some physical pages

		// If we fall below the thresholds, update more pages than usual
		// This is somewhat arbitrary, we can tweak these numbers
		// We also might want to increase the order of magnitude of the number of pages we update per clock tick
		// This will require some testing and profiling
		// There is also no reason why num_page_updates is based on the threshold values, it can be incremented by its own macros
		if (num_free_pages <= NPAGESFREE_LOW_THRESHOLD) {
			num_page_updates += NPAGEUPDATES_FACTOR*NPAGESFREE_HIGH_THRESHOLD;
		}
		if (num_free_pages <= NPAGESFREE_HIGH_THRESHOLD) {
			num_page_updates += NPAGEUPDATES_FACTOR*NPAGESFREE_LOW_THRESHOLD;
		}

		for ( ; num_page_updates >= 0; --num_page_updates) {
			// Find the next page that is currently mapped in user space, by finding one with a nonzero pp_ref
			while (!pages[page_to_age].pp_ref) {
				// If we wrap around to where we started, stop updating page ages
				if ((page_to_age=(page_to_age+1)%npages) == page_to_age_first) {
					goto end_of_page_age_updates;
				}
			}

			// Iterate through all of the user space PTEs that map to this page
			// If any of them have been accessed, increment the age, and then clear the PTE_A bit in all of the PTEs
			page_accessed = 0;
			for (pp_refs_chain = pages[page_to_age].pp_refs_chain; pp_refs_chain; pp_refs_chain = pp_refs_chain->pc_link) {
				if (*pp_refs_chain->pc_pte & PTE_A) {
					page_accessed = 1;
					pages[page_to_age].age += PAGE_AGE_INCREMENT_ON_ACCESS;
                                        pages[page_to_age].nfu_age += PAGE_AGE_INCREMENT_ON_ACCESS;
					pages[page_to_age].timestamp = counter;
					counter++;
					for ( ; pp_refs_chain; pp_refs_chain = pp_refs_chain->pc_link) {
						*(pp_refs_chain->pc_pte) &= ~PTE_A;
					}
					break;
				}
			}
			pages[page_to_age].age = (pages[page_to_age].age > MAX_PAGE_AGE ? MAX_PAGE_AGE : pages[page_to_age].age);
			pages[page_to_age].nfu_age = (pages[page_to_age].nfu_age > MAX_PAGE_AGE ? MAX_PAGE_AGE : pages[page_to_age].nfu_age);
			if (!page_accessed) {
				if (pages[page_to_age].age >= (uint8_t)PAGE_AGE_DECREMENT_ON_CLOCK) {
					pages[page_to_age].age -= (uint8_t)PAGE_AGE_DECREMENT_ON_CLOCK;
				}
				else {
					pages[page_to_age].age = 0;
				}
			}

			// If we wrap around to where we started, stop updating page ages
			if ((page_to_age=(page_to_age+1)%npages) == page_to_age_first) {
				goto end_of_page_age_updates;
			}
		}
		end_of_page_age_updates:
		sched_yield();
	}

	// Handle keyboard and serial interrupts.
	// LAB 5: Your code here.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
		lapic_eoi();
		kbd_intr();
		return;
	}
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
		lapic_eoi();
		serial_intr();
		return;
	}

	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}

void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");

	// Re-acqurie the big kernel lock if we were halted in
	// sched_yield()
	if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
		lock_kernel();
	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Acquire the big kernel lock before doing any
		// serious kernel work.
		// LAB 4: Your code here.
		lock_kernel();
		assert(curenv);

		// Garbage collect if current enviroment is a zombie
		if (curenv->env_status == ENV_DYING) {
			env_free(curenv);
			curenv = NULL;
			sched_yield();
		}

		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;
	struct PageInfo *pp = NULL;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if ((tf->tf_cs & 3) == 0) {
		panic("page_fault_handler: kernel-mode page fault");
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 4: Your code here.

	// utf is a pointer to the UTrapframe in the user exception stack.
	// If this is the first fault, then the pointer is right below UXSTACKTOP.
	// Otherwise, the pointer is right below tf->tf_esp, with a 32-bit word offset.
	// If UXSTACKTOP-PGSIZE <= tf->tf_esp <= UXSTACKTOP-1, then it is a recursive trap, the latter case.
	struct UTrapframe *utf = (struct UTrapframe *)(
		(((UXSTACKTOP-PGSIZE)<=(tf->tf_esp))&&((tf->tf_esp)<=(UXSTACKTOP-1)))
		?
		((tf->tf_esp)-(sizeof(struct UTrapframe)+sizeof(uint32_t)))
		:
		(UXSTACKTOP-sizeof(struct UTrapframe))
	);

	if (!(curenv->env_pgfault_upcall)) {
		// Destroy the environment that caused the fault.
		cprintf("[%08x] user fault va %08x ip %08x\n",
			curenv->env_id, fault_va, tf->tf_eip);
		print_trapframe(tf);
		env_destroy(curenv);
	}

	// If the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// We can do this with user_mem_assert on the portion of the stack where *utf is stored.
	// This obviously covers the first two cases.
	// It also takes care of the third case because the region below UXSTACKTOP-PGSIZE is unmapped.
	user_mem_assert(curenv, (void *)utf, sizeof(struct UTrapframe), PTE_W|PTE_U|PTE_P);

	utf->utf_fault_va = fault_va;

	// Copy values from the Trapframe to the UTrapframe.
	utf->utf_err = tf->tf_err;
	utf->utf_regs = tf->tf_regs;
	utf->utf_eip = tf->tf_eip;
	utf->utf_eflags = tf->tf_eflags;
	utf->utf_esp = tf->tf_esp;

	// Set the Trapframe to return to env_pgfault_upcall, with an exception stack at utf.
	// NOTE I am not confident that this code is correct.
	// TODO Test that this is correct, and correct it if necessary.
	tf->tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
	tf->tf_esp = (uintptr_t)utf;
}

