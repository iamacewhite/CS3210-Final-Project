// Program that attempts to allocate more memory than the amount of
// physical memory that exists on the system (causing paging out),
// then tries to access pages that it initially allocated (causing
// paging in) in a random order.

#include <inc/lib.h>

#define SIZE 0x8000000
#define BASE 0x10000000

// From: http://en.wikipedia.org/wiki/Random_number_generation#Generation_methods
uint32_t m_w = 1234;    /* must not be zero */
uint32_t m_z = 5678;    /* must not be zero */
 
uint32_t get_random()
{
	m_z = 36969 * (m_z & 65535) + (m_z >> 16);
	m_w = 18000 * (m_w & 65535) + (m_w >> 16);
	return (m_z << 16) + m_w;  /* 32-bit result */
}

void
umain(int argc, char **argv)
{
	int r;
	uintptr_t va;

	for(va = BASE; va < BASE+SIZE; va+=PGSIZE){
		//cprintf("+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);

		// Store the address in the page
		*(uintptr_t*)va = (uintptr_t)va;
	}

	int i;
	for(i = 0; i < 10000; i++)
	{
		va = BASE + (get_random() % SIZE);
		va = ROUNDDOWN(va, PGSIZE);
		//cprintf("-%x\n", va);
		assert(va == *(uintptr_t*)va);
	}
		     

	cprintf("%s: Passed all checks!\n", argc > 0 ? argv[0] : "randompagein");
	get_and_print_paging_stats();
}
