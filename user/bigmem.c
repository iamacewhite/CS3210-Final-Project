// Program that attempts to allocate more memory than the amount of
// physical memory that exists on the system

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int r;
	uintptr_t va;

	for(va = 0x10000000; va < USTACKTOP - PGSIZE; va+=PGSIZE){
		cprintf("+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);
	}
}
