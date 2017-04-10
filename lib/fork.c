// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void init_map_dir();

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static int
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte_t pte = uvpt[PGNUM(addr)];
	int perm = pte&PTE_SYSCALL;

	if (!((err&FEC_WR) && (perm&PTE_COW))) {
	//	panic("pgfault: not a write, or not to a copy-on-write page\naddr %08x pgnum %d pte %08x err %08x perm %08x", addr, PGNUM(addr), pte, err, perm);
		return 0;
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	perm &= ~PTE_COW;
	perm |= PTE_W;
	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = page_alloc(0, (void *)PFTEMP, perm, 1)) < 0) {
		panic("pgfault: page_alloc failed with error %d", r);
	}
	memmove((void *)PFTEMP, addr, PGSIZE);
	if ((r = page_map(0, (void *)PFTEMP, 0, addr, perm)) < 0) {
		panic("pgfault: page_map failed with error %d", r);
	}

	return 1;
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	int perm;

	if (uvpt[pn]&PTE_SHARE) {
		if ((r = page_map(0, PGADDR(0,pn,0), envid, PGADDR(0,pn,0), uvpt[pn]&PTE_SYSCALL)) < 0) {
			panic("page_map %d", r);
		}
	}

	// If the page is for a mapping table, copy it instead of making it copy-on-write
	else if (uvpt[pn]&PTE_NO_PAGE) {
		void *temp;

		if (!(temp = malloc()))
			panic("malloc failed");
		memcpy(temp, PGADDR(0,pn,0), PGSIZE);
		if ((r = page_map(0, temp, envid, PGADDR(0,pn,0), uvpt[pn]&PTE_SYSCALL)) < 0)
			panic("page_map: %e", r);
		if ((r = page_unmap(0, temp)) < 0)
			panic("page_unmap: %e", r);
	}

	// If the page is a writable or copy-on-write page, the new and old mappings must be made copy-on-write.
	else if ((uvpt[pn]&PTE_W) || (uvpt[pn]&PTE_COW)) {

		// Permissions for a user mode copy-on-write page.
		perm = PTE_P|PTE_U|PTE_COW|(uvpt[pn]&PTE_AVAIL);

		// Copy the page mapping (as copy-on-write) from the parent to the child.
		if ((r = page_map(0, PGADDR(0,pn,0), envid, PGADDR(0,pn,0), perm)) < 0) {
			panic("page_map %d", r);
		}

		/*
		 *
		 * NOTE It should be possible to use page_map to change the permissions of a PTE.
		 * If this turns out to not be the case, the following code unmaps the page in the parent page table,
		 * and then remaps it with the new permissions by copying the PTE from the child.
		 *
		 *if ((r = sys_page_unmap(0, PGADDR(0,pn,0))) < 0) {
		 *    panic("sys_page_unmap %d", r);
		 *}
		 *if ((r = page_map(envid, PGADDR(0,pn,0), 0, PGADDR(0,pn,0), perm)) < 0) {
		 *    panic("page_map %d", r);
		 *}
		 */

		// Mark the parent mapping as copy-on-write.
		if ((r = page_map(0, PGADDR(0,pn,0), 0, PGADDR(0,pn,0), perm)) < 0) {
			panic("page_map %d", r);
		}
	}

	// Otherwise (if the page is user mode readonly), use page_map to copy the page mapping, keeping the same set of permissions.
	else {
		if ((r = page_map(0, PGADDR(0,pn,0), envid, PGADDR(0,pn,0), uvpt[pn]&PTE_SYSCALL)) < 0) {
			panic("page_map %d", r);
		}
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// Some code and comments taken from dumbfork().

	envid_t envid;
	int pn;
	int r;

	if(!umapdir)
		init_map_dir();
	// Set up our page fault handler appropriately.
	add_pgfault_handler(pgfault);

	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %d", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.
	// Copy our address space to the child with copy-on-write.
	// We need to iterate through all PTEs for addresses below UTOP.
	// However, we handle the exception stack separately, so we only need to iterate up until UXSTACKTOP-PGSIZE.
	// Since [USTACKTOP, UXSTACKTOP-PGSIZE) is empty memory, we can get away with only iterating up until USTACKTOP.
	// This is the same as iterating through pages 0 through PGNUM(USTACKTOP)-1 inclusive.
	for (pn = 0; pn < PGNUM(USTACKTOP); ++pn) {

		// We only copy this page if it is present and readable in user mode.
		// This requires checking for PTE_P and PTE_U not only in uvpt[pn],
		// but in the associated PDE as well, since page pn won't exist if its page table doesn't exist, i.e. if it isn't present in the page directory.
		if (((uvpd[PDX(PGADDR(0,pn,0))]&PTE_P) && (uvpd[PDX(PGADDR(0,pn,0))]&PTE_U)) && ((uvpt[pn]&PTE_P) && (uvpt[pn]&PTE_U))) {
			duppage(envid, pn);
		}
	}

	// Allocate a new page for the child's user exception stack.
	page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_P|PTE_W, 1);

	// Copy our address space and page fault handler setup to the child.
	sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall);

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) {
		panic("sys_env_set_status: %d", r);
	}

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
