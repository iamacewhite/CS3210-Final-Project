/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, (void *)s, len, PTE_U);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv && curenv->env_type == ENV_TYPE_USER)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else if (curenv->env_type == ENV_TYPE_USER)
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *e;
	int r;
	if ((r = env_alloc(&e, curenv->env_id)) < 0) {
		return r;
	}
	e->env_status = ENV_NOT_RUNNABLE;

	// Copy the register set from the current to new Trapframe, but set eax to 0, so sys_exofork will appear to return 0 in the new environment.
	memmove((void *)(&(e->env_tf)), (void *)(&(curenv->env_tf)), (size_t)(sizeof(struct Trapframe)));
	e->env_tf.tf_regs.reg_eax = 0;

	return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env *e;
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}
	if ((status != ENV_RUNNABLE) && (status != ENV_NOT_RUNNABLE)) {
		return -E_INVAL;
	}
	e->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	struct Env *e;
	user_mem_assert(curenv, (void *)tf, sizeof(struct Trapframe), PTE_U);
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}
	e->env_tf = *tf;
	e->env_tf.tf_cs |= 3;
	e->env_tf.tf_eflags |= FL_IF;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *e;
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}
	e->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct PageInfo *p = 0;
	struct Env *e;

	// return -E_BAD_ENV if environment envid doesn't currently exist,
	// or the caller doesn't have permission to change envid.
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	// return -E_INVAL if va >= UTOP, or va is not page-aligned.
	if ((((uint32_t)va) >= UTOP) || ((uint32_t)va)%PGSIZE) {
		return -E_INVAL;
	}

	// return -E_INVAL if perm is inappropriate.
	if (((perm & (PTE_U | PTE_P)) ^ (PTE_U | PTE_P)) | (perm & (~(PTE_U | PTE_P | PTE_AVAIL | PTE_W)))) {
		return -E_INVAL;
	}

	// refuse to allocate memory if we're running low on free pages and the
	// environment is using more than its share
	extern size_t num_free_envs;
	assert(NENV - num_free_envs > 0);
	if(num_free_pages < SOFT_MIN_FREE_PAGES && e->env_npages > npages / (NENV - num_free_envs))
		return -E_NO_MEM;

	if(num_free_pages < HARD_MIN_FREE_PAGES)
		return -E_NO_MEM;

	// allocate a page of memory
	p = page_alloc(ALLOC_ZERO);

	// return -E_NO_MEM if there's no memory to allocate the new page.
	if (!p) {
		return -E_NO_MEM;
	}

	// map the page at va in environment e.
	// return -E_NO_MEM if there's no memory to allocate any necessary page tables.
	if (page_insert(e->env_pgdir, p, va, perm, &e->env_npages) < 0) {
		page_free(p);   // free the page we allocated, since we can't use it
		return -E_NO_MEM;
	}

	// Return 0 on success.
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct PageInfo *p = 0;
	struct Env *srcenv, *dstenv;
	pte_t *pte;

	// return -E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist, or the caller doesn't have permission to change one of them.
	if ((envid2env(srcenvid, &srcenv, 1) < 0) || (envid2env(dstenvid, &dstenv, 1) < 0)) {
		return -E_BAD_ENV;
	}

	// return -E_INVAL if srcva >= UTOP or srcva is not page-aligned, or dstva >= UTOP or dstva is not page-aligned.
	if ((((uint32_t)srcva) >= UTOP) || ((uint32_t)srcva)%PGSIZE || (((uint32_t)dstva) >= UTOP) || ((uint32_t)dstva)%PGSIZE) {
		return -E_INVAL;
	}

	// return -E_INVAL if perm is inappropriate.
	if (((perm & (PTE_U | PTE_P)) ^ (PTE_U | PTE_P)) | (perm & (~(PTE_U | PTE_P | PTE_AVAIL | PTE_W)))) {
		return -E_INVAL;
	}

	// get the page and pte mapped at srcva in the srcenv environment.
	// return -E_INVAL is srcva is not mapped in srcenvid's address space.
	if (!(p = page_lookup(srcenv->env_pgdir, srcva, &pte))) {
		return -E_INVAL;
	}

	// return -E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
	if ((perm & PTE_W) && !(*pte & PTE_W)) {
		return -E_INVAL;
	}

	// map the page at dstva in environment dstenv.
	// return -E_NO_MEM if there's no memory to allocate any necessary page tables.
	if (page_insert(dstenv->env_pgdir, p, dstva, perm, &dstenv->env_npages) < 0) {
		return -E_NO_MEM;
	}

	// Return 0 on success.
	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env *e;

	// return -E_BAD_ENV if environment envid doesn't currently exist, or the caller doesn't have permission to change envid.
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}

	// return -E_INVAL if va >= UTOP, or va is not page-aligned.
	if ((((uint32_t)va) >= UTOP) || ((uint32_t)va)%PGSIZE) {
		return -E_INVAL;
	}

	// unmap the page.
	page_remove(e->env_pgdir, va, &e->env_npages);

	// Return 0 on success.
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send can fail for the reasons listed below.
//
// If the target is blocked waiting for an IPC and
// there are no errors, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.
//
// If the target is not blocked waiting for an IPC,
// the data is stored in the current environment,
// and the current environment is added to a linked list
// of environments waiting to send IPCs to the target.
// The current environment then blocks until the IPC is complete.
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	int r;
	struct PageInfo *p = 0;
	struct Env *dstenv, *e;
	pte_t *pte;
	if (envid2env(envid, &dstenv, 0) < 0) {
		return -E_BAD_ENV;
	}
	if ((uint32_t)srcva < UTOP) {

		// return -E_INVAL if srcva is not page-aligned.
		if (((uint32_t)srcva)%PGSIZE) {
			return -E_INVAL;
		}

		// return -E_INVAL if perm is inappropriate.
		if (((perm & (PTE_U | PTE_P)) ^ (PTE_U | PTE_P)) | (perm & (~(PTE_U | PTE_P | PTE_AVAIL | PTE_W)))) {
			return -E_INVAL;
		}

		// get the page and pte mapped at srcva in the current environment.
		// return -E_INVAL is srcva is not mapped in curenv's address space.
		if (!(p = page_lookup(curenv->env_pgdir, srcva, &pte))) {
			return -E_INVAL;
		}

		// return -E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
		if ((perm & PTE_W) && !(*pte & PTE_W)) {
			return -E_INVAL;
		}
	}
	else {
		perm = 0;
	}

	// If the target is not blocked waiting for an IPC.
	if (!dstenv->env_ipc_recving || dstenv->env_status!=ENV_NOT_RUNNABLE) {

		// Save the IPC data in the curenv.
		curenv->env_ipc_page = p;
		curenv->env_ipc_value_sending = value;
		curenv->env_ipc_perm_sending = perm;

		// Add the curenv to the end of the linked list of environments waiting
		// to send to the target.
		// env_ipc_blocked_sender points to the first waiting environment.
		// The rest of the waiting environments are found by repeatedly
		// following the env_ipc_blocked_sender_chain pointer.
		if (!dstenv->env_ipc_blocked_sender) {
			dstenv->env_ipc_blocked_sender = curenv;
		}
		else {
			for (e = dstenv->env_ipc_blocked_sender; e->env_ipc_blocked_sender_chain; e = e->env_ipc_blocked_sender_chain) ;
			e->env_ipc_blocked_sender_chain = curenv;
		}

		// Block until the IPC occurs, when the target will mark this process as ENV_RUNNABLE.
		// This function will not return;
		// sys_ipc_recv will ensure that the system call returns the correct value to the user program.
		curenv->env_status = ENV_NOT_RUNNABLE;
		sched_yield();
	}

	// If the targetis blocked waiting for an IPC.
	else {
		if (((uint32_t)dstenv->env_ipc_dstva < UTOP) && ((uint32_t)srcva < UTOP)) {
			// map the page at dstva in environment dstenv.
			// return -E_NO_MEM if there's no memory to allocate any necessary page tables.
			if ((r = page_insert(dstenv->env_pgdir, p, (void *)dstenv->env_ipc_dstva, perm, &dstenv->env_npages)) < 0) {
				return r;
			}
		}
		dstenv->env_ipc_recving = 0;
		dstenv->env_ipc_from = curenv->env_id;
		dstenv->env_ipc_value = value;
		dstenv->env_ipc_perm = perm;
		dstenv->env_tf.tf_regs.reg_eax = 0; // makes sys_ipc_recv return 0
		dstenv->env_status = ENV_RUNNABLE;
	}

	// Return 0 on success.
	return 0;
}

// If there isn't an environment blocked waiting to send to this environment,
// block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If there is an environment blocked waiting to send to this environment,
// process the IPC immediately. The target ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The sending environment is marked runnable again, returning 0
// from the paused sys_ipc_send system call.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// Return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	struct Env *srcenv;
	if (((uint32_t)dstva < UTOP) && (((uint32_t)dstva)%PGSIZE)) {
		return -E_INVAL;
	}
sys_ipc_recv_find_sender:
	if ((srcenv = curenv->env_ipc_blocked_sender)) {

		// If there is a blocked sender, pop it from the head of the linked list of senders, and mark it as runnable.
		curenv->env_ipc_blocked_sender = srcenv->env_ipc_blocked_sender_chain;
		srcenv->env_ipc_blocked_sender_chain = 0;
		srcenv->env_status = ENV_RUNNABLE;

		// If a page mapping is in order, attempt the insertion.
		// If it fails, return the appropriate error code from the source's call to sys_ipc_send,
		// and try again with the next blocked sender.
		if (((uint32_t)dstva < UTOP) && srcenv->env_ipc_page) {
			if (page_insert(curenv->env_pgdir, srcenv->env_ipc_page, dstva, srcenv->env_ipc_perm_sending, &curenv->env_npages) < 0) {
				srcenv->env_tf.tf_regs.reg_eax = -E_NO_MEM; // makes sys_ipc_send return -E_NO_MEM
				goto sys_ipc_recv_find_sender;  // go back to the top to try again with the next blocked sender in the linked list
			}
		}

		// Store the values from the IPC in the curenv.
		curenv->env_ipc_from = srcenv->env_id;
		curenv->env_ipc_value = srcenv->env_ipc_value_sending;
		curenv->env_ipc_perm = srcenv->env_ipc_perm_sending;
		srcenv->env_tf.tf_regs.reg_eax = 0; // makes sys_ipc_send return 0
	}
	else {
		// If there is no blocked sender (or if all waiting sends failed),
		// block and wait for the next send.
		curenv->env_ipc_recving = 1;
		curenv->env_ipc_dstva = dstva;
		curenv->env_status = ENV_NOT_RUNNABLE;
		sched_yield();
	}
	return 0;
}

// Try to receive a value via IPC and return it.
// The receive fails with a return value of -E_IPC_NOT_SEND if there
// is no sender blocked, waiting to send an IPC.
//
// If there is an environment blocked waiting to send to this environment,
// process the IPC immediately. The target ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The sending environment is marked runnable again, returning 0
// from the paused sys_ipc_send system call.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// Return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_IPC_NOT_SEND if no sender is currently blocked in sys_ipc_send.
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_try_recv(void *dstva)
{
	// LAB 4: Your code here.
	struct Env *srcenv;
	if (((uint32_t)dstva < UTOP) && (((uint32_t)dstva)%PGSIZE)) {
		return -E_INVAL;
	}
sys_ipc_try_recv_find_sender:
	if ((srcenv = curenv->env_ipc_blocked_sender)) {

		// If there is a blocked sender, pop it from the head of the linked list of senders, and mark it as runnable.
		curenv->env_ipc_blocked_sender = srcenv->env_ipc_blocked_sender_chain;
		srcenv->env_ipc_blocked_sender_chain = 0;
		srcenv->env_status = ENV_RUNNABLE;

		// If a page mapping is in order, attempt the insertion.
		// If it fails, return the appropriate error code from the source's call to sys_ipc_send,
		// and try again with the next blocked sender.
		if (((uint32_t)dstva < UTOP) && srcenv->env_ipc_page) {
			if (page_insert(curenv->env_pgdir, srcenv->env_ipc_page, dstva, srcenv->env_ipc_perm_sending, NULL) < 0) {
				srcenv->env_tf.tf_regs.reg_eax = -E_NO_MEM; // makes sys_ipc_send return -E_NO_MEM
				goto sys_ipc_try_recv_find_sender;  // go back to the top to try again with the next blocked sender in the linked list
			}
		}

		// Store the values from the IPC in the curenv.
		curenv->env_ipc_from = srcenv->env_id;
		curenv->env_ipc_value = srcenv->env_ipc_value_sending;
		curenv->env_ipc_perm = srcenv->env_ipc_perm_sending;
		srcenv->env_tf.tf_regs.reg_eax = 0; // makes sys_ipc_send return 0
	}
	else {
		// If there is no blocked sender (or if all waiting sends failed),
		// fail with a return value of -E_IPC_NOT_SEND.
		return -E_IPC_NOT_SEND;
	}
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// An array of pointers that store the addresses of the system calls.
	// Because the system calls have different signatures, it is impossible to describe them with a single pointer declaration.
	// So we simply cast all of the function addresses to void *.
	static void *syscalls[] = {
		[SYS_cputs]             &sys_cputs,
		[SYS_cgetc]             &sys_cgetc,
		[SYS_getenvid]          &sys_getenvid,
		[SYS_env_destroy]       &sys_env_destroy,
		[SYS_yield]             &sys_yield,
		[SYS_exofork]           &sys_exofork,
		[SYS_env_set_status]    &sys_env_set_status,
		[SYS_page_alloc]        &sys_page_alloc,
		[SYS_page_map]          &sys_page_map,
		[SYS_page_unmap]        &sys_page_unmap,
		[SYS_env_set_pgfault_upcall]    &sys_env_set_pgfault_upcall,
		[SYS_ipc_send]          &sys_ipc_send,
		[SYS_ipc_recv]          &sys_ipc_recv,
		[SYS_ipc_try_recv]      &sys_ipc_try_recv,
		[SYS_env_set_trapframe] &sys_env_set_trapframe,
	};
	uint32_t ret = 0, esp = 0, ebp = 0;

	// return -E_INVAL if the system call number is invalid
	// i.e., if it is not the case that 0 <= syscallno < NSYSCALLS
	if (!(0 <= syscallno && syscallno < NSYSCALLS)) {
		return -E_INVAL;
	}

	// Now we need to call the requested system call, but there is a problem.
	// syscall takes five arguments a1-a5, but the system calls take 0-5 arguments.
	// There is no way to determine the arity of the system call at runtime,
	// and I didn't want to hard-code the arity of each function.
	// So with asm code, we do the following:
	//  1. Push the current stack pointer to the stack.
	//     Note that, after this instruction, 4(%esp) == 4 + %esp; and that, if we were to pop to the variable esp, we would have *esp == esp.
	//  2. Push to the stack, in reverse order, the five arguments a1-a5.
	//     All functions expect arguments to be pushed in reverse order like this.
	//  3. Call the function at the address stored in syscalls[syscallno].
	//     If the arity of this system call is n, the function will refer to the last n arguments that were pushed onto the stack (i.e., the first n of the arguments a1-a5).
	//     It won't refer to anything higher on the stack than these arguments,
	//     so it doesn't matter that we pushed all five of the arguments a1-a5 despite the fact that the system call probably takes less than five arguments.
	//     If the system call has a return value, it will store it in %eax. Store the value of %eax in the variable ret.
	//  4. Pop all of the arguments off of the stack, until...
	//  5. We pop off the base pointer that we pushed in 1. (identifiable by esp == ebp).
	//  6. At this point, the state of the stack is exactly the same as before we started executing our asm code. So we return, with a value of ret.
	asm volatile("push %esp");
	asm volatile("pushl %0" :               : "r" (a5));
	asm volatile("pushl %0" :               : "r" (a4));
	asm volatile("pushl %0" :               : "r" (a3));
	asm volatile("pushl %0" :               : "r" (a2));
	asm volatile("pushl %0" :               : "r" (a1));
	asm volatile("call *%0" : "=a" (ret)    : "r" (syscalls[syscallno]));
	do {
		asm volatile("pop %0" : "=r" (ebp));
		asm volatile("mov %%esp,%0" : "=r" (esp));
	} while (esp != ebp);
	return ret;
}

