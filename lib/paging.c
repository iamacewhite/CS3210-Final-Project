// Implement paging of memory to disk if we ever overflow

#include <inc/lib.h>
#include <inc/page.h>


static envid_t pagingenv = 0;
extern char end[];

void init_map_dir();


// Update the pagingenv static variable
void
find_paging_env()
{
	if(!pagingenv)
		pagingenv = ipc_find_env(ENV_TYPE_PAGE);
}

// The default page choice function, overridable by assigning page_choice
void *
default_page_choice_func(envid_t env, void *pg_in)
{
	static uint32_t pgnum = 0;
	uint32_t pgnum_offset, pgnum_actual;

	for (pgnum_offset = 0; pgnum_offset < NPDENTRIES*NPTENTRIES; pgnum_offset++)
	{
		// Calculate the actual pgnum
		pgnum_actual = (pgnum + pgnum_offset)%(NPDENTRIES*NPTENTRIES);
		// Check for the directory being present and UTOP
		if (!(uvpd[pgnum_actual/NPTENTRIES] & PTE_P) || pgnum_actual >= PGNUM(UTOP))
		{
			pgnum_offset += (NPTENTRIES - pgnum_actual%NPTENTRIES) - 1;
			continue;
		}
		// Checks (other)
		if (pgnum_actual*PGSIZE < (uintptr_t)end)
			continue;
		if (!(pgnum_actual*PGSIZE < USTACKTOP - PGSIZE) ||
		    !(uvpt[pgnum_actual] & PTE_P) ||
		    (uvpt[pgnum_actual] & PTE_SHARE) ||
		    (uvpt[pgnum_actual] & PTE_NO_PAGE))
			continue;
		if (pages[PGNUM(uvpt[pgnum_actual])].pp_ref >= 2)
			continue;
		
		// Update pgnum
		pgnum = pgnum_actual;
		return (void*)(pgnum*PGSIZE);
	}

	// There are no valid pages to page out -- return an address
	// that's guaranteed to be invalid
	pgnum = 0;
	return (void*)UTOP;
}

float
percentage_of_pgdir_to_walk(uint8_t age)
{
	float num, ret;
	age = (age > MAX_PAGE_AGE ? MAX_PAGE_AGE + 1 : age);
	ret = age / ((float)(MAX_PAGE_AGE+1));
	return ret*ret;
}

// Page choice function that pages out the environment page
// which is the least used.
// Doesn't actually choose the optimal page, as it would take a long time
// to walk the entire page directory to find it.
// Instead, we search through a percentage of the page directory,
// and choose the optimal page in that area.
void *
environment_page_age_page_choice_func0(envid_t env, void *pg_in)
{
	static uint32_t pgnum = 0;
	static uint32_t nentries = UTOP / PGSIZE;
	uint32_t pgnum_offset, pgnum_actual, pgnum_opt = 0, num_searched_ptes = 0;
	uint8_t age_opt = MAX_PAGE_AGE + 1;
	float pct_to_walk = 1.0f;

	for (pgnum_offset = 0; pgnum_offset < NPDENTRIES*NPTENTRIES; pgnum_offset++)
	{
		++num_searched_ptes;
		// Calculate the actual pgnum
		pgnum_actual = (pgnum + pgnum_offset)%(NPDENTRIES*NPTENTRIES);

		// Check for the directory being present and UTOP
		if (!(uvpd[pgnum_actual/NPTENTRIES] & PTE_P) || pgnum_actual >= PGNUM(UTOP))
		{
			pgnum_offset += (NPTENTRIES - pgnum_actual%NPTENTRIES) - 1;
			continue;
		}
		// Checks
		if (pgnum_actual*PGSIZE < (uintptr_t)end)
			goto environment_page_age_page_choice_func0_ret;
		if (!(pgnum_actual*PGSIZE < USTACKTOP - PGSIZE) ||
		    !(uvpt[pgnum_actual] & PTE_P) ||
		    (uvpt[pgnum_actual] & PTE_SHARE) ||
		    (uvpt[pgnum_actual] & PTE_NO_PAGE))
			goto environment_page_age_page_choice_func0_ret;
		if (pages[PGNUM(uvpt[pgnum_actual])].pp_ref >= 2)
			goto environment_page_age_page_choice_func0_ret;
		if (age_opt > MAX_PAGE_AGE || pages[PGNUM(uvpt[pgnum_actual])].age < age_opt) {
			age_opt = pages[PGNUM(uvpt[pgnum_actual])].age;
			pgnum_opt = pgnum_actual;
			pct_to_walk = percentage_of_pgdir_to_walk(age_opt);
		}
	environment_page_age_page_choice_func0_ret:
		if (num_searched_ptes >= nentries*pct_to_walk) {
			// cprintf("pgchoice: %x %d\n", pgnum_opt*PGSIZE, age_opt);
			// Update pgnum
			pgnum = pgnum_actual;
			return (void*)(pgnum_opt*PGSIZE);
		}
	}

	if (age_opt <= MAX_PAGE_AGE) {
		// cprintf("pgchoice: %x %d\n", pgnum_opt*PGSIZE, age_opt);
		return (void*)(pgnum_opt*PGSIZE);
	}

	// There are no valid pages to page out -- return an address
	// that's guaranteed to be invalid
	pgnum = 0;
	return (void*)UTOP;

	/*
	static size_t pgdir_index = 0, pgtbl_index = 0;
	static uint32_t nentries = UTOP / PGSIZE;
	int pgdir_offset, pgtbl_offset;
	int pgdir_actual_index, pgtbl_actual_index;
	uint32_t pgnum, pgnum_opt = 0, num_searched_ptes = 0;

	for (pgdir_offset = 0; pgdir_offset < NPDENTRIES; pgdir_offset++)
	{
		pgdir_actual_index = (pgdir_index + pgdir_offset)%NPDENTRIES;
		if (pgdir_actual_index*PTSIZE < UTOP && uvpd[pgdir_actual_index] & PTE_P)
			for (pgtbl_offset = 0; pgtbl_offset < NPTENTRIES; pgtbl_offset++)
			{
				++num_searched_ptes;
				pgtbl_actual_index = (pgtbl_index + pgtbl_offset)%NPTENTRIES;
				pgnum = pgdir_actual_index * NPTENTRIES + pgtbl_actual_index;

				// Checks
				if (pgnum*PGSIZE < (uintptr_t)end)
					goto environment_page_age_page_choice_func0_ret;
				if (!(pgnum*PGSIZE < USTACKTOP - PGSIZE) ||
					!(uvpt[pgnum] & PTE_P) ||
					(uvpt[pgnum] & PTE_SHARE) ||
					(uvpt[pgnum] & PTE_NO_PAGE))
					goto environment_page_age_page_choice_func0_ret;
				if (pages[PGNUM(uvpt[pgnum])].pp_ref >= 2)
					goto environment_page_age_page_choice_func0_ret;
				if (age_opt > MAX_PAGE_AGE || pages[PGNUM(uvpt[pgnum])].age < age_opt) {
					age_opt = pages[PGNUM(uvpt[pgnum])].age;
					pgnum_opt = pgnum;
					pct_to_walk = percentage_of_pgdir_to_walk(age_opt);
				}
environment_page_age_page_choice_func0_ret:
				if (num_searched_ptes >= nentries*pct_to_walk) {
					return (void*)(pgnum_opt*PGSIZE);
				}
			}
	}

	if (age_opt <= MAX_PAGE_AGE) {
		return (void*)(pgnum_opt*PGSIZE);
	}

	// There are no valid pages to page out -- return an address
	// that's guaranteed to be invalid
	return (void*)UTOP;
	*/
}

void *(*page_choice_func)(envid_t env, void *pg_in) = environment_page_age_page_choice_func0;
//void *(*page_choice_func)(envid_t env, void *pg_in) = default_page_choice_func;

// Function to set the page choice function
void
set_page_choice_func(void *(*pgchc_func)(envid_t env, void *pg_in))
{
	page_choice_func = pgchc_func;
}

// Use this function to actually get the page choice
void *
get_page_choice(envid_t env, void *pg_in)
{
	void *pg_out = page_choice_func(env, pg_in);

	// Check constraints
	// Prevent paging out the user exception stack,
	// that is an unrecoverable situation
	if ((uintptr_t)pg_out < UXSTACKTOP &&
	    (uintptr_t)pg_out >= UXSTACKTOP-PGSIZE)
		// Return an error
		panic("We tried to page out the user exception stack\n");
	// Prevent paging out the kernel
	else if ((uintptr_t)pg_out >= UTOP)
//		panic("We tried to page out UTOP -- we need swapping!\n");
		return (void *) UTOP;
	// Prevent paging out code pages
	else if ((uintptr_t)pg_out <= (uintptr_t)end)
		panic("We tried to page out code pages\n");
		
	// Make sure we actually have a valid mapping for the given VA
	if (!(uvpd[PDX(pg_out)] & PTE_P) || !(uvpt[PGNUM(pg_out)] & PTE_P))
		panic("Invalid mapping for the va\n");
	// Prevent paging out shared pages (for now)
	if (!(uvpd[PDX(pg_out)] & PTE_P) ||
	    !(uvpt[PGNUM(pg_out)] & PTE_P) ||
	    uvpt[PGNUM(pg_out)] & PTE_SHARE ||
	    uvpt[PGNUM(pg_out)] & PTE_NO_PAGE)
		panic("We tried to page out a shared page\n");
	if (pages[PGNUM(uvpt[PGNUM(pg_out)])].pp_ref >= 2)
		panic("We tried to page out a page mapped in >1 locations\n");

	return pg_out;
}


// Paging in and out functions (not callable from outside paging.c)
int
page_in(envid_t env, void *addr)
{
	//cprintf("page_in %p\n", addr);
	// Check that the paging server is up
	find_paging_env();
	if (pagingenv == 0)
		return -E_PAGING;

	// Game plan:
	// (1) Find page server index using mapping directory.
	// (2) Send IPC to page server requesting page (set up IPC
	//     to map the page to the correct address)
	// (3) Block in ipc_recv until the paging server is done
	// (4) Update mte to indicate not paged out

	int r;
	mte_t *mte;

	// Step 1: Find page server index
	mte = umapdir_walk(addr, 0);

	// Step 2: Send IPC to page server
	int map_index = *mte >> MTEFLAGS;
	int ipc_val = (map_index << 2) | PAGEREQ_PAGE_IN;
	int perm = (*mte & PTE_SYSCALL) | PTE_P;
	if ((r = page_alloc(env, addr, perm, 0)) < 0)
		return r;
	ipc_send(pagingenv, ipc_val, addr, PTE_U|PTE_W|PTE_P);

	// Step 3: Block in ipc_recv until the paging server finishes
	if ((r = ipc_recv(NULL, NULL, NULL)) < 0)
		panic("page_in: failed to recv from paging server -- %e\n", r);

	// Step 4: Update the mte
	*mte = 0;

	return 0;
}

int
page_out(envid_t env, void *pg_in)
{
	// Check that the paging server is up
	find_paging_env();
	if (pagingenv == 0)
		return -E_PAGING;

	// Game plan:
	// (1) Select page to page out using get_page_choice.
	// (2) Send IPC to the paging server to page out the page
	// (3) Block in ipc_recv, get the mapping index
	// (4) Unmap the page from our end
	// (5) Set up the mapping table entry.

	int r, perm;
	pte_t pte;

	// Step 1: Select page to page out (and check it)
	void *map_out_addr = get_page_choice(env, pg_in);
	if(map_out_addr == (void *) UTOP)
		return 0;
	//cprintf("page_out %p\n", map_out_addr);

	// Step 2: Send the IPC to the paging server
	ipc_send(pagingenv, PAGEREQ_PAGE_OUT, map_out_addr, PTE_P|PTE_U);

	// Step 3: Recv the mapping table index
	uint32_t map_index = ipc_recv(NULL, NULL, NULL);
	if ((int)map_index < 0)
		panic("Error from the paging server: %e\n", map_index);

	// Step 5: Unmap our page
	// NOTE: this relies on a "stable" unmapping, which doesn't
	// zero out the PTE_AVAIL bits
	pte = uvpt[PGNUM(map_out_addr)];
	perm = (pte & PTE_SYSCALL) | MTE_P;
	sys_page_unmap(0, map_out_addr);

	// Step 4: Find the mapping table entry, and set the index
	mte_t *mte = umapdir_walk(map_out_addr, 1);
	*mte = (map_index << MTEFLAGS);
	*mte |= perm;

	return 0;
}




// Safe page alloc function - wraps sys_page_alloc to avoid
// -E_NO_MEM by paging one page to disk in that situation
// and trying to the allocation again.
int
page_alloc(envid_t env, void *pg, int perm, int check_mte)
{
	int r;
	mte_t *mte;

	// Call init_map_dir if it hasn't been called yet
	// TODO: put this somewhere else, in case we're already out of memory
	if(!umapdir)
		init_map_dir();

	// First check if we have paged out the VA previously
	if (check_mte)
	{
		mte = umapdir_walk(pg, 0);
		if (mte && (*mte & PTE_P))
		{
			// TODO: We should send an IPC to the page server to
			// throw away the page
			panic("Unhandled case -- mapping to a paged out page: %p = %x\n%p\n", mte, *mte, pg);
		}
	}

	int t = 0;
	// Try just calling through
	while ((r = sys_page_alloc(env, pg, perm)) < 0)
	{
		// Pass through all non-memory errors
		if (r != -E_NO_MEM)
			return r;

		// Handle -E_NO_MEM by paging a page to disk
		if (page_out(env, pg) < 0)
			return r;

		// If we successfully paged out a page, yield then
		// try allocating the page again
		// Only yield once every ten loops
		if(!t){
			sys_yield();
			t = 10;
		}
		t--;
	}

	return 0;
}

// Safe page map function - wraps sys_page_map to handle several
// cases that may arise:
// (1) Mapping a page that was paged out (will return -E_INVAL).
// (2) Mapping over a page that was paged out (should remove stored
//     page in the paging server). [Handled directly by syscall
//     wrapper]
int
page_map(envid_t srcenvid, void *srcva,
	 envid_t dstenvid, void *dstva, int perm)
{
	int r, r2;
	mde_t *mde;
	mte_t *mte;

	// First, try calling through
	while ((r = sys_page_map(srcenvid, srcva, dstenvid, dstva, perm)) < 0)
	{
		//cprintf("Straight mapping failed: %e\n", r);
		// Check for -E_NO_MEM
		if (r == -E_NO_MEM)
		{
			page_out(0, (void*)UTEMP);
			// Now try again
			continue;
		}

		// Anything that's not -E_INVAL should get passed through
		if (r != -E_INVAL)
			return r;

		// Check for the paged out case by:
		// - Map in srcenv's umapdir at UTEMP
		// - Walk the umapdir, page in a map table if needed
		//   (overwrite the map dir at UTEMP)
		// - Find whether the page is actually paged out, if so
		//   we need to page back in
		// - Unmap UTEMP
		while ((r2 = sys_page_map(srcenvid, (void*)UMAPDIR, 0, (void*)UTEMP, PTE_P|PTE_U)) < 0)
		{
			if (r2 != -E_NO_MEM)
				panic("page_map: Unable to map UMAPDIR -- %e\n", r2);
			page_out(-E_NO_MEM, (void*)UTEMP);
		}
				
		mde = (mde_t*)UTEMP + MDX(srcva);
		if (!(*mde & MTE_P)) // Mapping table doesn't exist
			return r; // Pass through the error
		mte = (mte_t*)(PGNUM(*mde) << MTXSHIFT);
		while ((r2 = sys_page_map(srcenvid, (void*)mte, 0, (void*)UTEMP, PTE_P|PTE_U)) < 0)
		{
			if (r2 != -E_NO_MEM)
				panic("page_map: Unable to map map table -- %e\n", r2);
			page_out(0, (void*)UTEMP);
		}
		mte = (mte_t*)UTEMP + MTX(srcva);
		if (!(*mte & MTE_P)) // Page doesn't exist in mapping table
			return r; // Pass through the error
		if ((r2 = sys_page_unmap(0, UTEMP)) < 0)
			panic("page_map: Unable to unmap UTEMP -- %e\n", r2);

		// Paged out case
		while ((r2 = page_in(srcenvid, srcva)) < 0)
		{
			if (r2 != -E_NO_MEM)
				panic("page_map: Unable to page in target page -- %e\n", r2);
			page_out(0, (void*)srcva);
		}

		// Now we loop back to the top and hopefully succeed in mapping,
		// otherwise try handling again
	}

	return r;
}

// Safe page unmap function - wrap sys_page_unmap to handle
// several special cases that may arise:
// (1) Unmapping a paged out page
int
page_unmap(envid_t envid, void *va)
{
	int r, r2;
	mde_t *mde;
	mte_t *mte;

	// First call through
	r = sys_page_unmap(envid, va);

	// In the case where we just unmapped page that was
	// paged out AND in another environment (therefore
	// wouldn't have been paged in by our poking code)
	// then we need to tell the paging server to throw
	// away that page.

	// Unmapping from current env
	if (envid == thisenv->env_id || envid <= 0)
		return r;
	// Check for paging env
	find_paging_env();
	if (pagingenv == 0)
		return r;

	// Other env
	if ((r2 = sys_page_map(envid, (void*)UMAPDIR, 0, (void*)UTEMP, PTE_P|PTE_U)) < 0)
		panic("page_unmap: Unable to map UMAPDIR -- %e\n", r2);
	mde = (mde_t*)UTEMP + MDX(va);
	if (!(*mde & MTE_P)) // Mapping table doesn't exist
		return r; // Just return
	mte = (mte_t*)(PGNUM(*mde) << MTXSHIFT);
	if ((r2 = sys_page_map(envid, (void*)mte, 0, (void*)UTEMP, PTE_P|PTE_U)) < 0)
		panic("page_unmap: Unable to map map table -- %e\n", r2);
	mte = (mte_t*)UTEMP + MTX(va);
	if (!(*mte & MTE_P)) // Page doesn't exist in mapping table
		return r; // Just return
	if ((r2 = sys_page_unmap(0, UTEMP)) < 0)
		panic("page_unmap: Unable to unmap UTEMP -- %e\n", r2);
	// Page was paged out, so we need to tell paging server to drop it
	int map_index = *mte >> MTEFLAGS;
	int ipc_val = (map_index << 2) | PAGEREQ_PAGE_REMOVE;
	ipc_send(pagingenv, ipc_val, NULL, 0);
	if ((r2 = ipc_recv(NULL, NULL, NULL)) < 0)
		panic("page_unmap: failed to recv from paging server -- %e\n", r2);

	return r;
}


// Page fault handler -- checks if the page that we faulted on is
// paged out currently. If that's the case, we need to page it back
// in before returning 1. Else, return 0.
int
paging_pgfault_handler(struct UTrapframe *utf)
{
	// First, check that the paging env is up
	find_paging_env();
	if (pagingenv == 0)
		return 0;
	// Pull the fault address
	void *fault_addr = (void*)utf->utf_fault_va;

	int r;
	mte_t *mte = umapdir_walk(fault_addr, 0);
	if (mte && (*mte & MTE_P))
	{
		// Try paging in the page
		if ((r = page_in(thisenv->env_id, fault_addr)) < 0)
			return 0;
		return 1;
	}

	// Otherwise, we didn't handle it
	return 0;
}

// Get paging stats from the paging server.
// Simply a wrapper around sending the IPC to the paging server
struct Pageret_stat*
get_paging_stats()
{
	// First, check that the paging env is up
	find_paging_env();
	if (pagingenv == 0)
		return NULL;
	// Send the ipc
	struct Pageret_stat* stats = (struct Pageret_stat*)malloc();
	ipc_send(pagingenv, PAGEREQ_PAGE_STAT, stats, PTE_U|PTE_W|PTE_P);
	// Recv the return value
	ipc_recv(NULL, stats, NULL);
	return stats;
}

void
print_paging_stats(struct Pageret_stat *stats)
{
	cprintf("\n");
	cprintf("Total number of page outs: %d\n", stats->num_page_outs);
	cprintf("Total number of page ins: %d\n", stats->num_page_ins);
	cprintf("Total number of page removes: %d\n", stats->num_page_removes);
	cprintf("\n");
}

void
get_and_print_paging_stats()
{
	print_paging_stats(get_paging_stats());
}

void
init_map_dir()
{
	int r;

	// Set the pgfault handler
	add_pgfault_handler(paging_pgfault_handler);

        // Define the global umapdir variable, allocate a page
	umapdir = (mde_t*)UMAPDIR;
	if((r = sys_page_alloc(0, umapdir, PTE_P|PTE_U|PTE_W|PTE_NO_PAGE)) < 0)
		panic("init_map_dir: %e", r);

	find_paging_env();
}

mte_t *
umapdir_walk(const void *va, int create)
{
	mde_t *mde;
	void *page;
	int r;

	// Find directory entry
	mde = (mde_t*)(umapdir + MDX(va));
	// Mapping table doesn't exist yet
	if(!(*mde & MTE_P)){
		// Should we create it?
		if(!create)
			return NULL;
		// Find a page to allocate for it
		page = malloc();
		if(!page)
			return NULL;
		// Check to see if a recursive call to umapdir_walk
		// created the table already
		if(*mde & MTE_P)
		{
			sys_page_unmap(0, page);
		}
		else
		{
			// Find the mapping and set PTE_NO_PAGE
			sys_page_map(0, page, 0, page, PTE_P|PTE_W|PTE_U|PTE_NO_PAGE);
			*mde = (uintptr_t)(page) | MTE_P;
		}
	}
	// Get the pointer to the mapping table and add the mapping table offset
	return (mte_t*)(PGNUM(*mde) << MTXSHIFT) + MTX(va);
}
