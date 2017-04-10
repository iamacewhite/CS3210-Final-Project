// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/types.h>
#include <inc/mmu.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Displays a stack backtrace", mon_backtrace },           // Defines a shell command backtrace that calls mon_backtrace to print stack backtrace information.
	{ "showmappings", "Displays all of the physical page mappings that apply to a particular range of virtual/linear addresses in the currently active address space", mon_showmappings },
	{ "changemappingpermissions", "Explicitly set, clear, or change the permissions of any mapping in the current address space", mon_changemappingpermissions },
	{ "dumpmemory", "Dump the contents of a range of memory given either a virtual or physical address range", mon_dumpmemory },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

// Displays a stack backtrace. Returns 0 on success.
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t *ebp;              // ebp base pointer for a function in the backtrace (a pointer to the ebp base pointer for the previous function on the backtrace).
	uintptr_t eip;              // Return eip instruction pointer for a function in the backtrace.

	// eip instruction pointer debugging information struct.
	// Must be a struct, not a struct pointer, so that space for the struct is automatically allocated in memory.
	// This space is used to store the debugging information that is computed in debuginfo_eip().
	struct Eipdebuginfo info;
	cprintf("Stack backtrace:\n");

	/**
	 ** initialization: ebp is equal to the ebp base pointer of this call to mon_backtrace, eip is equal to the return eip instruction pointer of this call to mon_backtrace.
	 ** loop-invariant: Each loop represents a single function in the backtrace.
	 **                 ebp is always the ebp base pointer for that function, and eip is always the return eip instruction pointer for that function.
	 ** condition:      ebp != 0. At the start of the stack trace, ebp is initialized to 0. Check for this, and when ebp == 0, stop traversing the stack.
	 ** increment:      The next ebp base pointer is the value pointed to by the current ebp base pointer.
	 **                 The next eip instruction pointer is the 1-st element in the stack above the next ebp base pointer.
	 */
	for (ebp = (uint32_t *) read_ebp(), eip = (uintptr_t) (ebp[1]); ebp; ebp = (uint32_t *) (ebp[0]), eip = (uintptr_t) (ebp[1])) {

		// Print the ebp base pointer, the eip instruction pointer, and the first five arguments to the current function, all as 8-digit hexidecimal numbers (32-bit integers).
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x \n", (uint32_t) ebp, (uint32_t) eip, ebp[2], ebp[3], ebp[4], ebp[5], ebp[6]);
		debuginfo_eip(eip, &info);  // Get the eip instruction pointer debugging information for the calling function, and store it in info.

		// Print the file name that the calling function is defined in; the line number of return instruction, the name of the calling function,
		// and the byte offset of the return instruction from the start of the calling function.
		cprintf("         %s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);
	}
	return 0;
}

// Displays physical page mappings. Returns 0 on success.
// On input error, displays usage message.
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	int i, j, error = 0;
	long ends[2];
	uintptr_t va;
	pte_t *pte;
	static char * header       = "VPN        Flags                      Physical page";
	static char * flags_header = "           AVL G PS D A CD WT U W P";
	static char * boundary     = "---------------------------------------------------";

	// expects two args in addition to the command name
	if (argc != 3) {
		error = 1;
	}

	// skip additional error checking if an error already exists
	// otherwise, make sure that the two args are hex strings
	// if they are, convert them to integers
	if (!error) {
		for (i = 0; i < 2; ++i) {
			if (strlen(argv[i+1])>10 || !strisl(argv[i+1], 16, ends+i)) {
				error = 1;
				break;
			}
		}
	}

	// the first argument must be <= the second argument
	if (!error && ends[0] > ends[1]) {
		error = 1;
	}

	// if there are any formatting errors, print usage message and return
	if (error) {
		cprintf("Proper usage is 'showmappings <start> <end>' (e.g. 'showmappings 0x3000 0x5000'), which displays mappings for linear (hexadecimal) addresses <start> through <end>, inclusive.\n");
		return 0;
	}

	// round the start/end down/up to the nearest page boundaries
	ends[0] = ROUNDDOWN(ends[0], PGSIZE);
	ends[1] = ROUNDUP(ends[1], PGSIZE);
	for (va = (uintptr_t)ends[0], i=0; ; va += PGSIZE, i = (i+1)%30) {
		if (!i) {
			cprintf("%s\n", boundary);
			cprintf("%s\n", header);
			cprintf("%s\n", flags_header);
			cprintf("%s\n", boundary);
		}
		cprintf("0x%05x    ", va>>PTXSHIFT);
		pte = pgdir_walk(kern_pgdir, (void *)va, 0);
		if (pte) {
			cprintf("%01x", (PGOFF(*pte)>>11)&0x1);
			cprintf("%01x", (PGOFF(*pte)>>10)&0x1);
			cprintf("%01x ", (PGOFF(*pte)>>9)&0x1);
			cprintf("%c ", *pte & PTE_G ? 'G' : '-');
			cprintf("%s ", *pte & PTE_PS ? "PS" : "--");
			cprintf("%c ", *pte & PTE_D ? 'D' : '-');
			cprintf("%c ", *pte & PTE_A ? 'A' : '-');
			cprintf("%s ", *pte & PTE_PCD ? "CD" : "--");
			cprintf("%s ", *pte & PTE_PWT ? "WT" : "--");
			cprintf("%c ", *pte & PTE_U ? 'U' : '-');
			cprintf("%c ", *pte & PTE_W ? 'W' : '-');
			cprintf("%c    ", *pte & PTE_P ? 'P' : '-');
			cprintf("0x%05x\n", PGNUM(*pte));
		}
		else {
			cprintf("<no physical page mapping>\n");
		}

		// break when we get to the end
		// this will always happen, since ends[0] and ends[1] were both page aligned
		// can't do this check in the for-loop, because if ends[1]==0xfffff000,
		//   va will overflow and therefore always be <= ends[1]
		if (va == ends[1]) {
			break;
		}
	}
	cprintf("%s\n", boundary);
	return 0;
}

int
mon_changemappingpermissions(int argc, char **argv, struct Trapframe *tf)
{
	char change[] = {0, 0}, new_perms[] = {0, 0}, error = 0, perm, value, perms;
	int i;
	uintptr_t va;
	long va_long;
	pte_t *pte, pte_temp;
	char *argv_new[3];

	// expects 1-3 args in addition to the command name
	if (!(2 <= argc && argc <= 4)) {
		error = 1;
	}

	// skip additional error checking if an error already exists
	// otherwise, make sure that the first args is a hex strings
	// if it is, convert it to an integer
	if (!error) {
		if (strlen(argv[1])>10 || !strisl(argv[1], 16, &va_long)) {
			error = 1;
		}
		else {
			va = (uintptr_t)va_long;
		}
	}

	// skip additional error checking if an error already exists
	// otherwise, make sure the permissions are specified correctly
	if (!error) {
		for (i = 0; i < argc-2; ++i) {
			if (*(argv[i+2]++) != '-') {
				error = 1;
				break;
			}
			perm = *(argv[i+2]++);
			if ('a' <= perm && perm <= 'z') {
				perm += 'A' - 'a';
			}
			if (perm == 'W' || perm == 'U') {
				change[(perm == 'U' ? 1 : 0)] = 1;
			}
			else {
				error = 1;
				break;
			}
			value = *argv[i+2];
			if (value && (value != '0') && (value != '1')) {
				error = 1;
				break;
			}
			else if (value == '0') {
				value = 0;
			}
			else {
				value = 1;
			}
			new_perms[(perm == 'U' ? 1 : 0)] = value;
		}
	}

	// if there are any formatting errors, print usage message and return
	if (error) {
		cprintf("Proper usage is\n\
	'changemappingpermissions <page virtual address> [-W[0|1]] [-U[0|1]]'\n\
(e.g. 'changemappingpermissions 0x3000 -W1 -U').\n\
-W corresponds to the write permission, -U to the user permission.\n\
If a permission is omitted, the value is unchanged.\n\
If a permission is included, with a value of 1 or with no value, the permission is set.\n\
If a permission is included with a value of 0, the permission is cleared.\n");
		return 0;
	}

	// round fa down/up to the nearest page boundary
	va = ROUNDDOWN(va, PGSIZE);
	// get the PTE for va
	pte = pgdir_walk(kern_pgdir, (void *)va, 0);
	if (!pte) {
		cprintf("no physical page mapping at 0x%05x\n", va>>PTXSHIFT);
		return 0;
	}
	// compute and set the new permissions
	perms = (*pte)&(PTE_W|PTE_U);
	for (i = 0; i < 2; ++i) {
		if (change[i]) {
			perms &= ~(1<<(i+1)) & (PTE_W|PTE_U);
			perms |= ((new_perms[i]<<(i+1)) & (PTE_W|PTE_U));
		}
	}
	pte_temp = *pte;
	pte_temp &= ~(PTE_W|PTE_U);
	pte_temp |= perms;

	// call showmappings() to print permission information
	argv_new[0] = "showmappings";
	argv_new[1] = argv_new[2] = argv[1];
	if (pte_temp == *pte) {
		cprintf("No change to page mapping permissions\n");
		mon_showmappings(3, argv_new, tf);
	}
	else {
		cprintf("Old permissions:\n");
		mon_showmappings(3, argv_new, tf);
		*pte = pte_temp;
		cprintf("\nNew permissions:\n");
		mon_showmappings(3, argv_new, tf);
	}
	return 0;
}

// Dumps the contents of a range of memory. Returns 0 on success.
// On input error, displays usage message.
int
mon_dumpmemory(int argc, char **argv, struct Trapframe *tf)
{
	int i, physical = 0, error = 0;
	long ends[2];
	uintptr_t va, ends_rounded[2];
	uint32_t *m, *page_end;
	pte_t *pte;

	// expects 2-3 args in addition to the command name
	if (!(3 <= argc && argc <= 4)) {
		error = 1;
	}

	// skip additional error checking if an error already exists
	// otherwise, make sure that the first two args are hex strings
	// if they are, convert them to integers
	if (!error) {
		for (i = 0; i < 2; ++i) {
			if (strlen(argv[i+1])>10 || !strisl(argv[i+1], 16, ends+i)) {
				error = 1;
				break;
			}
		}
	}

	// the first argument must be <= the second argument
	if (!error && ends[0] > ends[1]) {
		error = 1;
	}

	// check if the user is using physical addresses
	if (!error && argc == 4) {
		if (strlen(argv[3]) != 2 || argv[3][0] != '-' || (argv[3][1]!='P' && argv[3][1]!='p')) {
			error = 1;
		}
		else {
			physical = 1;
		}
	}

	// if there are any formatting errors, print usage message and return
	if (error) {
		cprintf("Proper usage is\n\
		'dumpmemory <start> <end> [-P]'\n\
(e.g. 'dumpmemory 0x3000 0x5000'),\n\
which dumps the contents of the given range of memory.\n\
The -P flag indicates that the addresses should be interpretted as physical addresses.\n\
Otherwise, they will be interpretted as virtual addresses.\n");
		return 0;
	}

	if (physical) {
		for (i = 0; i < 2; ++i) {
			ends[i] = (long)KADDR(ends[i]);
		}
	}
	// round the start/end down/up to the nearest page boundaries
	ends_rounded[0] = ROUNDDOWN(ends[0], PGSIZE);
	ends_rounded[1] = ROUNDUP(ends[1]+1, PGSIZE);

	//iterate through the pages
	for (va = ends_rounded[0]; va < ends_rounded[1]; va += PGSIZE) {
		pte = pgdir_walk(kern_pgdir, (void *)va, 0);

		// if this page isn't in use, skip it
		if (!pte || !(*pte & PTE_P)) {
			continue;
		}

		// adjust the start of end of where we read this page
		// in the two cases where we are starting or ending at a non-boundary
		if (va == ends_rounded[0]) {
			m = (uint32_t *)((va&(~0xfff))|PGOFF(ends[0]));
		}
		else {
			m = (uint32_t *)va;
		}
		if (va+PGSIZE == ends_rounded[1]) {
			page_end = (uint32_t *)((va&(~0xfff))|PGOFF(ends[1]));
		}
		else {
			page_end = (uint32_t *)(va+PGSIZE);
		}

		// iterate through 32-bit units of memory at a time,
		// printing four on each line,
		// and printing the address of the first of these four
		for (i = 0; m <= page_end; ++m, i = (i+1)%4) {
			if (!i) {
				cprintf("\n%s 0x%08x:    ", (physical ? "PA" : "VA"), (physical ? (uint32_t *)PADDR(m) : m));
			}
			cprintf("0x%08x    ", *m);
		}
		cprintf("\n");
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
