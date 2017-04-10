// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
void (*_pgfault_handler)(struct UTrapframe *utf);

// Array of pointers to currently installed C-language pgfault handlers.
int (*_pgfault_handlers[MAX_PGFAULT_HANDLERS])(struct UTrapframe *utf);
size_t num_pgfault_handlers=0;

// C-language wrapper to call into the installed pgfault handlers.
void _pgfault_handler_wrapper(struct UTrapframe *utf){
	int i, r;

	for(i = num_pgfault_handlers - 1; i >= 0; i--){
		if((r = (*_pgfault_handlers[i])(utf)))
			return;
	}

	panic("[%08x] user fault va %08x ip %08x\n", thisenv->env_id, utf->utf_fault_va, utf->utf_eip);
}

//
// Set the page fault handler function.
// If there isn't one yet, _pgfault_handler will be 0.
// The first time we register a handler, we need to
// allocate an exception stack (one page of memory with its top
// at UXSTACKTOP), and tell the kernel to call the assembly-language
// _pgfault_upcall routine when a page fault occurs.
//
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if(_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		sys_page_alloc(0, (void *)(UXSTACKTOP-PGSIZE), PTE_W|PTE_U|PTE_P);
		sys_env_set_pgfault_upcall(0, _pgfault_upcall);
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}


void
add_pgfault_handler(int (*handler)(struct UTrapframe *utf))
{
	int i, r;

	// Check if we've already added this handler
	for(i = 0; i < num_pgfault_handlers; i++)
		if(_pgfault_handlers[i] == handler)
			return;

	if (num_pgfault_handlers == MAX_PGFAULT_HANDLERS)
		panic("Too many pgfault handlers!");

	_pgfault_handlers[num_pgfault_handlers++] = handler;
	set_pgfault_handler(_pgfault_handler_wrapper);
}
