#include <kern/e1000.h>

// LAB 6: Your driver code here
#include <kern/pci.h>
#include <kern/pmap.h>

volatile uint32_t *e1000;

int
e1000_attach(struct pci_func *pcif)
{
	pci_func_enable(pcif);
	e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	return 0;
}

