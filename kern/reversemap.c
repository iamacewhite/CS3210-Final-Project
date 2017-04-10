#include <kern/reversemap.h>
#include <kern/pmap.h>


// List of free PteChain nodes
static struct PteChain* free_pte_chain = NULL;

// Utility function to allocate a page-worth of PteChain structs
int
alloc_pte_chain_page()
{
	int i;

	// Allocate a page worth of PteChain structs
	struct PageInfo* pp = page_alloc(ALLOC_ZERO);
	if (!pp)
		panic("Unable to allocate a page for PteChain\n");
	int num_free = PGSIZE / sizeof(struct PteChain);
	struct PteChain* pc;
	for (i = 0; i < num_free; i++)
	{
		pc = (struct PteChain*)page2kva(pp) + i;
		pc->pc_link = free_pte_chain;
		free_pte_chain = pc;
	}

	return 0;
}


// Try to find a pte that references the physical page which matches
// the given pte filter.
// If pc_store != NULL, store the resulting PteChain in pc_store
int
find_pte(struct PageInfo* pp, pte_t filter, struct PteChain** pc_store)
{
	struct PteChain* pc = pp->pp_refs_chain;
	while(pc != NULL)
	{
		if (filter & *(pc->pc_pte))
		{
			if (pc_store != NULL)
				*pc_store = pc;
			return 1;
		}
		       
	}

	return 0;
}

// Initialize the reverse map linked lists, memory allocation, etc.
// NOTE: Not currently used. free_pte_chain is automatically 0, and
// we don't need to allocate the initial page for PteChains, since
// when we call alloc_pte_chain we allocate a page if there isn't
// one already.
void
init_reverse_map()
{
	// Nullify free_pte_chain initially
	free_pte_chain = 0;
	// Allocate a page worth of PteChain
	alloc_pte_chain_page();
}

// Allocate one of the free PteChain structs, possible
// allocating a new page for more free structs.
struct PteChain*
alloc_pte_chain()
{
	struct PteChain* ret;
	if (free_pte_chain == NULL)
	{
		// If there are no more PteChain structs left,
		// alloc another page worth
		alloc_pte_chain_page();
	}

	ret = free_pte_chain; // Pull off the head
	free_pte_chain = ret->pc_link; // Update free_pte_chain
	// Clear the struct
	ret->pc_link = NULL;
	ret->pc_pte = NULL;
	ret->pc_pgdir = NULL;
	ret->pc_env_va = 0;

	return ret;
}

// Dealloc a PteChain struct (adds it to the free_pte_chain
// linked list).
void
dealloc_pte_chain(struct PteChain* pc)
{
	pc->pc_link = free_pte_chain;
	free_pte_chain = pc;
}
	
