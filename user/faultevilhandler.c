// test evil pointer for user-level fault handler

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	page_alloc(0, (void*) (UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W, 1);
	sys_env_set_pgfault_upcall(0, (void*) 0xF0100020);
	*(int*)0 = 0;
}
