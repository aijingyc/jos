#include <kern/e1000.h>

// LAB 6: Your driver code here
#include <kern/pci.h>
#include <kern/pmap.h>

/* Register Set. (82543, 82544)
 *
 * Registers are defined to be 32 bits and should be accessed as 32 bit values.
 * These registers are physically located on the NIC, but are mapped into the
 * host memory address space.
 *
 * RW - register is both readable and writable
 * RO - register is read only
 * WO - register is write only
 * R/clr - register is read only and is cleared when read
 * A - register array
 */
#define E1000_CTRL     0x00000  /* Device Control - RW */
#define E1000_CTRL_DUP 0x00004  /* Device Control Duplicate (Shadow) - RW */
#define E1000_STATUS   0x00008  /* Device Status - RO */

volatile uint32_t *e1000;

static uint32_t
e1000_read_reg(uint32_t offset)
{
	return e1000[offset / sizeof(uint32_t)];
}

int
e1000_attach(struct pci_func *pcif)
{
	pci_func_enable(pcif);
	e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	return 0;
}

