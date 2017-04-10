#ifndef JOS_INC_PAGE_H
#define JOS_INC_PAGE_H

#include <inc/types.h>
#include <inc/mmu.h>
#include <inc/memlayout.h>

// PTE_NO_PAGE is a syscall bit that the paging library sets
// on it's own pages so that those specific pages are protected
// from paging out.
#define PTE_NO_PAGE         0x200

// Typedefs for mapping directory
typedef pte_t mte_t;
typedef pde_t mde_t;
// Mapping directory index
#define MDX(la)		((((uintptr_t) (la)) >> MDXSHIFT) & 0x3FF)
// Mapping table index
#define MTX(la)		((((uintptr_t) (la)) >> MTXSHIFT) & 0x3FF)

// Sizes/shifts
#define NMDENTIRES      1024            // mapping directory entries per mapping directory
#define NMTENTIRES      1024            // mapping table entires per mapping table
#define MTXSHIFT        12              // offset of MTX in a linear address
#define MDXSHIFT        22              // offset of MDX in a linear address
#define MTEFLAGS        12              // the number of bits used for flags

// Flags (last 2 bits of MTE, 12 bits of MDE)
#define MTE_FLAG_FILTER 0xFFF
#define MDE_FLAG_FILTER 0xFFF
#define MTE_P           0x001   // Present

// Mapping table stored index value
#define MTE_VAL(mte)  (mte >> 12)


// Definitions for requests from clients to page system
enum {
	PAGEREQ_PAGE_IN = 0,
	PAGEREQ_PAGE_OUT,
	PAGEREQ_PAGE_REMOVE,
	PAGEREQ_PAGE_STAT,
};

struct Pageipc {
	// Ensure Pageipc is one page
	char page_content[PGSIZE];
};

// Page directory for paged-out pages
mde_t *umapdir;

struct Pageret_stat {
	uint32_t num_page_outs;
	uint32_t num_page_ins;
	uint32_t num_page_removes;
};

struct Pageret_stat *get_paging_stats(void);
void print_paging_stats(struct Pageret_stat *stats);
void get_and_print_paging_stats(void);

#define MAX_PAGE_AGE 254
#define PAGE_AGE_INCREMENT_ON_ACCESS 100
#define PAGE_AGE_DECREMENT_ON_CLOCK  1
#define PAGE_AGE_INITIAL MAX_PAGE_AGE
#define NPAGESFREE_HIGH_THRESHOLD (1<<8)
#define NPAGESFREE_LOW_THRESHOLD  (1<<4)
#define NPAGEUPDATES_FACTOR 50

#endif /* !JOS_INC_PAGE_H */


