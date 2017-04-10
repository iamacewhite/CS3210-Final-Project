// Program that attempts to allocate more memory than the amount of
// physical memory that exists on the system (causing paging out),
// then tries to access pages that it initially allocated (causing
// paging in).

#include <inc/lib.h>

#define SIZE 0x2000000
#define BASE 0x10000000

void
umain(int argc, char **argv)
{
	int r;
	uintptr_t va;

	if((r = page_alloc(0, (void*) 0x0f000000, PTE_P|PTE_U|PTE_W, 1)) < 0)
		panic("sys_page_alloc on %p: %e", (void*) 0x0f000000, r);
	*((char*)0x0f000000) = argc > 0 ? argv[0][0] : 'r';

	for(va = BASE+SIZE-PGSIZE; va >= BASE; va-=PGSIZE){
		//cprintf("[rs]+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);

		// Store the address in the page
		*(uintptr_t*)va = (uintptr_t)va;
	}

	for(va = BASE; va < BASE+SIZE; va+=PGSIZE){
		//cprintf("[rs]+%x=%x\n", va, *(uintptr_t*)va);
		assert(*(uintptr_t*)va == (uintptr_t)va);
	}
	for(va = BASE+SIZE-PGSIZE; va >= BASE; va-=PGSIZE){
		//cprintf("[rs]+%x=%x\n", va, *(uintptr_t*)va);
		assert(*(uintptr_t*)va == (uintptr_t)va);
	}

	sys_cputs((char*)0x0f000000, 1);

	struct Pageret_stat *stats = (struct Pageret_stat *)0x0f000000;

	cprintf("%s: Passed all checks!\n", argc > 0 ? &(argv[0][1]) : "everselinearpagein");
	get_and_print_paging_stats();
}
