#ifndef __REVERSEMAP_H__
#define __REVERSEMAP_H__

#include <inc/memlayout.h>
#include <inc/types.h>
#include <inc/env.h>

// Linked list node for a chain of ptes
struct PteChain
{
	struct PteChain* pc_link;
	pte_t* pc_pte;
	pde_t* pc_pgdir;
	uintptr_t pc_env_va;
};

int              find_pte(struct PageInfo* pp, pte_t filter, struct PteChain** pc_store);
void             init_reverse_map();
struct PteChain* alloc_pte_chain();
int              alloc_pte_chain_page();
void             dealloc_pte_chain(struct PteChain* pc);

#endif //!__REVERSEMAP_H__
