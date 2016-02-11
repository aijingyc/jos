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
#define E1000_TCTL     0x00400  /* TX Control - RW */
#define E1000_TIPG     0x00410  /* TX Inter-packet gap -RW */
#define E1000_TDBAL    0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808  /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810  /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818  /* TX Descripotr Tail - RW */
#define E1000_TXDCTL   0x03828  /* TX Descriptor Control - RW */

/* Transmit control */
#define E1000_TCTL_RST    0x00000001    /* software reset */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_BCE    0x00000004    /* busy check enable */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */
#define E1000_TCTL_SWXOFF 0x00400000    /* SW Xoff transmission */
#define E1000_TCTL_PBE    0x00800000    /* Packet Burst Enable */
#define E1000_TCTL_RTLC   0x01000000    /* Re-transmit on late collision */
#define E1000_TCTL_NRTU   0x02000000    /* No Re-transmit on underrun */
#define E1000_TCTL_MULR   0x10000000    /* Multiple request support */

/* Transmit IPG */
#define E1000_TIPG_IPGT   0x000003ff    /* IPG transmit time */
#define E1000_TIPG_IPGR1  0x000ffc00    /* IPG receive time 1 */
#define E1000_TIPG_IPGR2  0x3ff00000    /* IPG receive time 2 */

/* Transmit descriptor command */
#define E1000_TXD_CMD_EOP    0x01000000 /* End of Packet */
#define E1000_TXD_CMD_IFCS   0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_IC     0x04000000 /* Insert Checksum */
#define E1000_TXD_CMD_RS     0x08000000 /* Report Status */
#define E1000_TXD_CMD_RPS    0x10000000 /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT   0x20000000 /* Descriptor extension (0 = legacy) */
#define E1000_TXD_CMD_VLE    0x40000000 /* Add VLAN tag */
#define E1000_TXD_CMD_IDE    0x80000000 /* Enable Tidv register */

/* Transmit descriptor status */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_TXD_STAT_EC    0x00000002 /* Excess Collisions */
#define E1000_TXD_STAT_LC    0x00000004 /* Late Collisions */
#define E1000_TXD_STAT_TU    0x00000008 /* Transmit underrun */

/* Maximum transmit descriptors (must be a multiple of 8 value) */
#define E1000_MAX_TDESC   64

/* Maximum ethernet packet size on the wire */
#define MAX_PACKET_SIZE   1518

struct tx_desc
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
};

volatile uint32_t *e1000;

static struct tx_desc tx_descs[E1000_MAX_TDESC] __attribute__((aligned(16)));
static char tx_pkts[E1000_MAX_TDESC][MAX_PACKET_SIZE];

static uint32_t
e1000_read_reg(uint32_t offset)
{
	return e1000[offset / sizeof(uint32_t)];
}

static void
e1000_write_reg(uint32_t offset, uint32_t value)
{
	e1000[offset / sizeof(uint32_t)] = value;
}

static uint8_t
mask_shift(uint32_t mask)
{
	int i;

	if (!mask)
		return 32;

	for (i = 0; !(mask & 0x1); i++) {
		mask >>= 1;
	}
	return i;
}

static uint32_t
masked_value(uint32_t mask, uint32_t value)
{
	return (value << mask_shift(mask)) & mask;
}

int
e1000_attach(struct pci_func *pcif)
{
	int i;

        // Enable e1000 PCI device
	pci_func_enable(pcif);

        // Map e1000 registers to MMIO region
	e1000 = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

        // Transmit initialization
	assert((uintptr_t) tx_descs % 16 == 0);
	for (i = 0; i < E1000_MAX_TDESC; i++) {
		tx_descs[i].addr = PADDR(tx_pkts[i]);
	}
	e1000_write_reg(E1000_TDBAL, PADDR(tx_descs));
	e1000_write_reg(E1000_TDLEN, E1000_MAX_TDESC * sizeof(struct tx_desc));
	assert(e1000_read_reg(E1000_TDLEN) % 128 == 0);
	e1000_write_reg(E1000_TDH, 0);
	e1000_write_reg(E1000_TDT, 0);
	e1000_write_reg(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
			masked_value(E1000_TCTL_CT, 0x10) |
			masked_value(E1000_TCTL_COLD, 0x40));
        e1000_write_reg(E1000_TIPG, masked_value(E1000_TIPG_IPGT, 10) |
			masked_value(E1000_TIPG_IPGR1, 8) |
			masked_value(E1000_TIPG_IPGR2, 6));
	return 0;
}

