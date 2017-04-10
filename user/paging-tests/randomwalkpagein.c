// Program that attempts to allocate more memory than the amount of
// physical memory that exists on the system (causing paging out),
// then tries to access pages that it initially allocated (causing
// paging in) in NRANDS random walks.

#include <inc/lib.h>

#define SIZE 0x5000000
#define BASE 0x10000000
#define NRANDS 5

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
	uintptr_t randva[NRANDS];

	for(va = BASE; va < BASE+SIZE; va+=PGSIZE){
		//cprintf("+%x\n", va);
		if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
			panic("sys_page_alloc on %p: %e", va, r);

		// Store the address in the page
		*(uintptr_t*)va = (uintptr_t)va;
	}

	memset(randva, 0, sizeof(uintptr_t)*NRANDS);
	int i;
	for(i = 0; i < 10000; i++)
	{
		randva[i%NRANDS] += (get_random()%21 - 10)*PGSIZE;
		if(randva[i%NRANDS] < BASE || randva[i%NRANDS] >= BASE+SIZE)
			randva[i%NRANDS] = ROUNDDOWN(BASE + (get_random() % SIZE), PGSIZE);
		cprintf("-%x\n", randva[i%NRANDS]);
		assert(randva[i%NRANDS] == *(uintptr_t*)randva[i%NRANDS]);
	}

	cprintf("%s: Passed all checks!\n", argc > 0 ? argv[0] : "randomwalkpagein");
	get_and_print_paging_stats();
}
