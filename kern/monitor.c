// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
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
	{ "backtrace", "Display information about the backtrace", mon_backtrace },
	{ "showmappings", "Display memory mappings for a range of virtual addresses", mon_showmappings },
	{ "modpageperms", "Modify page permissions", mon_modpageperms },
	{ "showvm", "Show virtual memory content", mon_showvm },
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

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, eip;
	int i;

	cprintf("Stack backtrace:\n");

	ebp = read_ebp();
	while (ebp) {
		eip = ((uint32_t *) ebp)[1];
		cprintf("  ebp %08x eip %08x ", ebp, eip);
	
		cprintf("args ");
		for (i = 1; i <= 5; i++) {
			cprintf("%08x ", ((uint32_t *) ebp)[1 + i]);
		}
		cprintf("\n");

		struct Eipdebuginfo info;
		if (!debuginfo_eip(eip, &info))
			cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line,
				info.eip_fn_namelen, info.eip_fn_name,
				eip - info.eip_fn_addr);

		ebp = ((uint32_t *) ebp)[0];
	}
	return 0;
}

static void
pte_perms_show(int perms)
{
	int i, size;
	static const char* perm_strs[] = {
		"PTE_P", "PTE_W", "PTE_U", "PTE_PWT", "PTE_PCD", "PTE_A", "PTE_D", "PTE_PS", "PTE_G"
	};

	size = sizeof(perm_strs) / sizeof(char *);
	for (i = 0; i < size; i++) {
		cprintf("%s: 0x%x ", perm_strs[i], perms & (1 << i));
	}
	cprintf("\n");
}

static void
pte_show(pte_t pte)
{
	cprintf("pa 0x%x ", PTE_ADDR(pte));
	pte_perms_show(PGOFF(pte));
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	int i;
	uintptr_t va_arg[2], va;
	pde_t *pgdir;
	pte_t *pte;

	if (argc != 3) {
		cprintf("usage: showmappings begin_va end_va\n");
		return 0;
	}

	for (i = 1; i < argc; i++) {
		va_arg[i - 1] = (uintptr_t) strtol(argv[i], NULL, 0);
	}

	if (va_arg[0] > va_arg[1]) {
		cprintf("begin va (0x%x) is greater than end va (0x%x)\n", va_arg[0], va_arg[1]);
		return 0;
	}

	pgdir = (pde_t *) KADDR(rcr3());
	for (va = va_arg[0]; va <= va_arg[1]; va += PGSIZE) {
		pte = pgdir_walk(pgdir, (void *) va, 0);
		if (!pte || !(*pte & PTE_P)) {
			cprintf("va 0x%x is not mapped\n", va);
		} else {
			cprintf("va 0x%x ", va);
			pte_show(*pte);
		}

		if (va + PGSIZE < va)
			break;
	}
	return 0;
}

int
mon_modpageperms(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t va_arg;
	int perms;
	pde_t *pgdir;
	pte_t *pte;

	if (argc != 3) {
		cprintf("usage: modpageperms va perms\n");
		return 0;
	}

	va_arg = (uintptr_t) strtol(argv[1], NULL, 0);

	perms = (uintptr_t) strtol(argv[2], NULL, 0);
	if (PTE_ADDR(perms)) {
		cprintf("perms 0x%03x is invalid\n", perms);
		return 0;
	}

	pgdir = (pde_t *) KADDR(rcr3());
	pte = pgdir_walk(pgdir, (void *) va_arg, 0);
	if (!pte) {
		cprintf("va 0x%x is not mapped\n", va_arg);
		return 0;
	}

	cprintf("va 0x%x existing pte ");
	pte_show(*pte);
	*pte = PTE_ADDR(*pte) | perms;
	cprintf("va 0x%x changed pte ");
	pte_show(*pte);
	return 0;
}

static void
show_page(pde_t *pgdir, uintptr_t begin_va, uintptr_t end_va, bool valid, int *i, int *j)
{
	char *va;

	for (va = (char *) begin_va; va <= (char *) end_va; va++) {
		if (*i == 0 && *j == 0)
			cprintf("0x%08x: ", va);

		if (valid)
			cprintf("%02x", (uint8_t) *va);
		else
			cprintf("xx");

		(*i)++;
		if (*i == 4) {
	
		cprintf(" ");
			*i = 0;
			(*j)++;
		}
		if (*j == 4) {
			cprintf("\n");
			*i = *j = 0;
		}
	}
}

int
mon_showvm(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t begin_va, end_va, va, next_va;
	pde_t *pgdir;
	pte_t *pte;
	int i, j;

	if (argc != 3) {
		cprintf("usage: dumpvm begin_va end_va\n");
		return 0;
	}

	begin_va = (uintptr_t) strtol(argv[1], NULL, 0);
	end_va = (uintptr_t) strtol(argv[2], NULL, 0);
	if (begin_va > end_va) {
		cprintf("begin va (0x%x) is greater than end va (0x%x)\n", begin_va, end_va);
		return 0;
	}

	pgdir = (pde_t *) KADDR(rcr3());
	for (i = j = 0, va = begin_va; va <= end_va; va += PGSIZE) {
		pte = pgdir_walk(pgdir, (void *) va, 0);
		next_va = MIN(ROUNDDOWN(va + PGSIZE, PGSIZE) - 1, end_va);
		show_page(pgdir, va, next_va, *pte && (*pte & PTE_P), &i, &j);
	}
	if (i)
		cprintf("\n");
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


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
