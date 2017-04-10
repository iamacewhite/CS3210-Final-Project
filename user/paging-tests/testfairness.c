// Program creates a "parent" environment that allocates all the memory
// and a "child" environment with no memory.  The child environment
// tries to allocate more pages while the parent environment tries
// to page in its paged out pages

#include <inc/lib.h>

#define MIN_ADDR 0x10000000
#define MAX_ADDR 0x14000000

void
umain(int argc, char **argv)
{
	int r;
	uintptr_t va;
	envid_t envid, parent_envid;

	parent_envid = thisenv->env_id;
	envid = fork();
	if(envid<0){
		panic("fork: %e", envid);
	} else if (envid){
		// Parent
		for(va = MIN_ADDR; va < MAX_ADDR; va+=PGSIZE){
			cprintf("parent +%x\n", va);
			if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
				panic("sys_page_alloc on %p: %e", va, r);

			// Store the address in the page
			*(uintptr_t*)va = (uintptr_t)va;
		}

		cprintf("starting child\n");
		if((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
			panic("parent sys_env_set_status: %e", r);

		for(va = MIN_ADDR; va < MIN_ADDR + PGSIZE*250; va+=PGSIZE){
                	cprintf("parent +%x=%x\n", va, *(uintptr_t*)va);
                	assert(*(uintptr_t*)va == (uintptr_t)va);
		}

		cprintf("parent done\n");
	}
	else{
		if((r = sys_env_set_status(0, ENV_NOT_RUNNABLE)) < 0)
			panic("child sys_env_set_status: %e", r);

		for(va = MIN_ADDR; va < MIN_ADDR + PGSIZE*250; va+=PGSIZE){
			cprintf("child +%x\n", va);
			if((r = page_alloc(0, (void*) va, PTE_P|PTE_U|PTE_W, 1)) < 0)
				panic("sys_page_alloc on %p: %e", va, r);

			// Store the address in the page
			*(uintptr_t*)va = (uintptr_t)va;
		}
		cprintf("child done\n");
	}

}
