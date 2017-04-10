/*
 * Page system server main loop -
 * serves IPC requests from other environments.
 */

#include <inc/x86.h>
#include <inc/string.h>
#include <fs/fs.h>

#include "page.h"


#define debug 0

// Virtual address at which to receive page mappings containing client requests.
struct Pageipc *pagereq = (struct Pageipc *)0x0ffff000;

/*
 * We use a bitmap to keep track of HD blocks that are free and not-free in the swap space.
 * To get easy access to free blocks, we put the bitmap in a linked list.
 * Each node is a bitmap for a group of contiguous blocks.
 * Each node has a group number.
 * A group is free if at least one block in its group is free.
 * A free group has a link to another free group.
 *
 * In the bitmap, each 0 indicates a free block, and each 1 indicates a not-free block.
 */
struct page_bitmap_node {
	uint32_t bitmap;
	uint32_t groupno;
	struct page_bitmap_node *link;
};

/*
 * The desired number of blocks for the swap space
 * Update PAGE_NBLOCKS whenever count in fs/Makefrag is updated, and vice-versa
 */
#define PAGE_NBLOCKS 32768

/*
 * To reduce the overhead of having a large number of linked lists, each node is a bitmap for a group of blocks, rather than a single block.
 * We use a uint32_t for this bitmap; therefore the number of blocks per group is 32.
 */
#define NBLOCKS_PER_GROUP 32
#define PAGE_NGROUPS PAGE_NBLOCKS/NBLOCKS_PER_GROUP

#define PAGE_BLOCKS_OFFSET 1024 // The size of the fs partition, and the start of the swap partition

struct page_bitmap_node page_bitmap_nodes[PAGE_NGROUPS];  // PAGE_NBLOCKS bits, to indicate free and used blocks in the swap space
struct page_bitmap_node *page_bitmap_node_free_list = 0;  // linked list of free groups
struct Pageret_stat serve_stats_s;                        // stats for the page server

// returns the block number of a free block in the page swap space
// returns 0 if there are no such free blocks
// panics on error
// NOTE: does not mark the block as not free, you must call mark_page_block_as_not_free with the return value from get_free_page_block
// TODO: determine if this is the correct behavior
int
get_free_page_block(void)
{
	int i;
	uint32_t bitmap;
	struct page_bitmap_node *node = page_bitmap_node_free_list;
	if (!page_bitmap_node_free_list) {
		return -E_SWAP_SPACE_FULL;
	}
	for (i = 0, bitmap = node->bitmap; i < NBLOCKS_PER_GROUP; ++i, bitmap >>= 1) {
		if (!(bitmap&1)) {
			return PAGE_BLOCKS_OFFSET + node->groupno*NBLOCKS_PER_GROUP + i;
		}
	}
	panic("get_free_page_block: there is a block group in page_bitmap_node_free_list that has no free blocks");
	return 0;
}

// marks the given block as not free
// if all blocks in the group are now not free, removes the group from the free list
// the given block number must be the actual block number (so it must be >= PAGE_BLOCKS_OFFSET)
// for simplicity, you can only unfree a block in the group at the head of the free list
// panics on error
void
mark_page_block_as_not_free(uint32_t blockno)
{
	int i;
	uint32_t groupno;
	blockno -= PAGE_BLOCKS_OFFSET;
	if (blockno < 0 || blockno >= PAGE_NBLOCKS) {
		panic("mark_page_block_as_not_free: invalid block number");
	}
	groupno = blockno / NBLOCKS_PER_GROUP;
	if (!page_bitmap_node_free_list || groupno != page_bitmap_node_free_list->groupno) {
		panic("mark_page_block_as_not_free: attempting to unfree block that isn't in front of page_bitmap_node_free_list");
	}
	i = blockno % NBLOCKS_PER_GROUP;
	if (page_bitmap_node_free_list->bitmap & (1<<i)) {
		panic("mark_page_block_as_not_free: attempting to unfree block that is already unfree");
	}
	page_bitmap_node_free_list->bitmap |= (1<<i);
	if (!(~(page_bitmap_node_free_list->bitmap))) {
		page_bitmap_node_free_list = page_bitmap_node_free_list->link;
	}
}

// marks the given block as free
// if none of the blocks in the group were free, adds the group to the free list
// the given block number must be the actual block number (so it must be >= PAGE_BLOCKS_OFFSET)
// panics on error
void
mark_page_block_as_free(uint32_t blockno)
{
	int i;
	uint32_t groupno;
	blockno -= PAGE_BLOCKS_OFFSET;
	if (blockno < 0 || blockno >= PAGE_NBLOCKS) {
		panic("mark_page_block_as_free: invalid block number");
	}
	groupno = blockno / NBLOCKS_PER_GROUP;
	if (!(~page_bitmap_nodes[groupno].bitmap)) {
		page_bitmap_nodes[groupno].link = page_bitmap_node_free_list;
		page_bitmap_node_free_list = (page_bitmap_nodes+groupno);
	}
	i = blockno % NBLOCKS_PER_GROUP;
	if (!(page_bitmap_nodes[groupno].bitmap & (1<<i))) {
		panic("mark_page_block_as_free: attempting to free block that is already free");
	}
	page_bitmap_nodes[groupno].bitmap ^= (1<<i);
}

void
serve_init(void)
{
	int i;
	for (i = PAGE_NGROUPS-1; i >= 0; --i) {
		page_bitmap_nodes[i].bitmap = 0;    // every block in the page swap space starts out free (indicated by 0 bits)
		page_bitmap_nodes[i].groupno = i;
		page_bitmap_nodes[i].link = page_bitmap_node_free_list;
		page_bitmap_node_free_list = (page_bitmap_nodes+i);
	}
	// Allocate the pagereq address, so the we create the page table ahead of time
	sys_page_alloc(0, pagereq, PTE_U|PTE_P|PTE_W);
	serve_stats_s.num_page_outs = 0;
	serve_stats_s.num_page_ins = 0;
	serve_stats_s.num_page_removes = 0;
}

int
serve_page_in(envid_t envid, uint32_t blockno, struct Pageipc *ipc, void **return_page)
{
	int r;
	if (blockno < 0 || blockno >= PAGE_NBLOCKS) {
		return -1;  // TODO handle incorrect block numbers
	}
	blockno += PAGE_BLOCKS_OFFSET;
	if ((r = ide_read(blockno*BLKSECTS, (void *)ipc, BLKSECTS)) < 0) {
		return r;   // TODO handle IDE write errors
	}
	mark_page_block_as_free(blockno);
	*return_page = (void *)ipc;
	++serve_stats_s.num_page_ins;
	return 0;
}

int
serve_page_remove(envid_t envid, uint32_t blockno, struct Pageipc *ipc, void **return_page)
{
	int r;
	if (blockno < 0 || blockno >= PAGE_NBLOCKS) {
		return -1;  // TODO handle incorrect block numbers
	}
	blockno += PAGE_BLOCKS_OFFSET;
	mark_page_block_as_free(blockno);
	++serve_stats_s.num_page_removes;
	return 0;
}

int
serve_page_out(envid_t envid, uint32_t blockno, struct Pageipc *ipc, void **return_page)
{
	int free_blockno, r;
	if ((free_blockno = get_free_page_block()) <= 0) {
		return free_blockno;
	}
	if ((r = ide_write(free_blockno*BLKSECTS, (void *)ipc, BLKSECTS)) < 0) {
		return r;   // TODO handle IDE write errors
	}
	mark_page_block_as_not_free(free_blockno);
	++serve_stats_s.num_page_outs;
	return free_blockno-PAGE_BLOCKS_OFFSET;
}

int
serve_page_stat(envid_t envid, uint32_t blockno, struct Pageipc *ipc, void **return_page)
{
	memset(ipc, 0, sizeof(struct Pageipc));
	*((struct Pageret_stat *)ipc) = serve_stats_s;
	*return_page = (void *)ipc;
	return 0;
}

typedef int (*pagehandler)(envid_t envid, uint32_t blockno, struct Pageipc *req, void **return_page);

pagehandler handlers[] = {
	[PAGEREQ_PAGE_IN] =		serve_page_in,
	[PAGEREQ_PAGE_OUT] =		serve_page_out,
	[PAGEREQ_PAGE_REMOVE] =		serve_page_remove,
	[PAGEREQ_PAGE_STAT] =		serve_page_stat,
};
#define NHANDLERS (sizeof(handlers)/sizeof(handlers[0]))

void
serve(void)
{
	uint32_t req, whom;
	int perm, r;
	void *pg;

	while (1) {
		perm = 0;
		req = ipc_recv((int32_t *) &whom, pagereq, &perm);
		if (debug)
			cprintf("page req %d from %08x [page %08x: %s]\n",
				req, whom, uvpt[PGNUM(pagereq)], pagereq);

		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		pg = NULL;

		// use the lower two bits for sending the handler number
		if (((req&3) < NHANDLERS) && handlers[req&3]) {
			r = handlers[req&3](whom, req>>2, pagereq, &pg);
		} else {
			cprintf("Invalid request code %d from %08x\n", req, whom);
			r = -E_INVAL;
		}
		if (pg && ((uintptr_t)pg)%PGSIZE) {
			panic("serve: the address being returned doesn't lie on a page boundary");
		}
		ipc_send(whom, r, pg, perm);
		sys_page_unmap(0, pagereq);
	}
}

void
umain(int argc, char **argv)
{
	binaryname = "page";
	cprintf("Page server is running\n");

	serve_init();
	page_init();
	serve();
}

