// Program that attempts to allocate more memory than the amount of
// physical memory that exists on the system (causing paging out),
// then tries to access pages that it initially allocated (causing
// paging in).

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int r;
	uintptr_t va;

	// Setup
	for(va = 0x10000000; va < 0x14000000; va+=PGSIZE){
		cprintf("+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);

		// Store the address in the page
		*(uintptr_t*)va = (uintptr_t)va;
	}

	// Try page mapping
	for(va = 0x10000000; va < 0x1000f000; va += PGSIZE)
	{
		cprintf("-%x\n", va);
		assert(page_map(0, (void*)va, 0, (void*)(va-PGSIZE), PTE_P|PTE_U) == 0);
	}
	cprintf("linearspecials: Done page mapping\n");
	
	// Test paging in
	for(va = 0x10000000; va < 0x1000e000; va+=PGSIZE){
		cprintf("+%x=%x\n", va, *(uintptr_t*)va);
		assert(*(uintptr_t*)va == (va+PGSIZE));
	}
	cprintf("linearspecials: Passed paging in\n");

	// Test unmapping paged out page
	for(va = 0x1000f000; va < 0x1001f000; va += PGSIZE)
	{
		cprintf("-%x\n", va);
		assert(page_unmap(0, (void*)va) == 0);
	}

	cprintf("linearspecials: Passed all checks!\n");
	get_and_print_paging_stats();
}
