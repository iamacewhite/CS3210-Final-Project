// test bad pointer for user-level fault handler
// this is going to fault in the fault handler accessing eip (always!)
// so eventually the kernel kills it (PFM_KILL) because
// we outrun the stack with invocations of the user-level handler

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	page_alloc(0, (void*) (UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W, 1);
	sys_env_set_pgfault_upcall(0, (void*) 0xDeadBeef);
	*(int*)0 = 0;
}
