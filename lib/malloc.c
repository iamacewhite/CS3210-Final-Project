#include <inc/lib.h>
#include <inc/page.h>

extern void *end;

// TODO: Make this less stupid (keep track of free pages)

// Find a (virtual) page that is unmapped and return a pointer to it
// Return NULL if it can't find a page
void *
find_unused_page()
{
	int r;
	mte_t *mte;
	void* va;
	static void *last_page = (void*)UTEXT;

	for(va = last_page + PGSIZE; va != last_page; va += PGSIZE){
		if((uintptr_t)va >= USTACKTOP - PGSIZE){
			va = (void*)(UTEXT - PGSIZE);
			continue;
		}
		if((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P))
			continue;
		mte = umapdir_walk(va, 0);
		if(mte && (*mte & PTE_P))
			continue;
		last_page = va;
		return va;
	}
	return NULL;
}

// Allocate a page of memory, returning a pointer to the beginning of the block
// On error, return NULL
void *malloc() {
	int r;
	void *va;

	va = find_unused_page();
	if(!va)
		return NULL;
	if((r = page_alloc(0, va, PTE_P|PTE_U|PTE_W, 1)) < 0)
		return NULL;
	return va;
}

// Free a page of memory returned by malloc, so it can be malloc'd later
void free(void *va) {
	sys_page_unmap(0, va);
}

