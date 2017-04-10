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

	for(va = 0x10000000; va < 0x14000000; va+=PGSIZE){
		cprintf("+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);

		// Store the address in the page
		*(uintptr_t*)va = (uintptr_t)va;
	}

	if ((r = fork()) == 0) // Child
	{
		for(va = 0x10000000; va < 0x14000000; va+=PGSIZE){
			cprintf("+%x=%x\n", va, *(uintptr_t*)va);
			assert(*(uintptr_t*)va == (uintptr_t)va);
		}
	}
	else // Parent
	{
		for(va = 0x10000000; va < 0x14000000; va+=PGSIZE){
			cprintf("+%x=%x\n", va, *(uintptr_t*)va);
			assert(*(uintptr_t*)va == (uintptr_t)va);
		}

		wait(r);

		cprintf("%s: Passed all checks!\n");
	}
}
