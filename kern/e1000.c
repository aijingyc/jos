#include <kern/e1000.h>

// LAB 6: Your driver code here
#include <inc/error.h>
#include <inc/string.h>
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
#define E1000_IMS      0x000D0  /* Interrupt Mask Set - RW */
#define E1000_RCTL     0x00100  /* RX Control - RW */
#define E1000_TCTL     0x00400  /* TX Control - RW */
#define E1000_TIPG     0x00410  /* TX Inter-packet gap -RW */
#define E1000_RDBAL    0x02800  /* RX Descriptor Base Address Low - RW */
#define E1000_RDBAH    0x02804  /* RX Descriptor Base Address High - RW */
#define E1000_RDLEN    0x02808  /* RX Descriptor Length - RW */
#define E1000_RDH      0x02810  /* RX Descriptor Head - RW */
#define E1000_RDT      0x02818  /* RX Descriptor Tail - RW */
#define E1000_TDBAL    0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808  /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810  /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818  /* TX Descripotr Tail - RW */
#define E1000_TXDCTL   0x03828  /* TX Descriptor Control - RW */
#define E1000_MTA      0x05200  /* Multicast Table Array - RW Array */
#define E1000_RAL      0x05400  /* Receive Address Low- RW */
#define E1000_RAH      0x05404  /* Receive Address High- RW */

/* Receive Control */
#define E1000_RCTL_RST            0x00000001    /* Software reset */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_SBP            0x00000004    /* store bad packet */
#define E1000_RCTL_UPE            0x00000008    /* unicast promiscuous enable */
#define E1000_RCTL_MPE            0x00000010    /* multicast promiscuous enab */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM_NO         0x00000000    /* no loopback mode */
#define E1000_RCTL_LBM_MAC        0x00000040    /* MAC loopback mode */
#define E1000_RCTL_LBM_SLP        0x00000080    /* serial link loopback mode */
#define E1000_RCTL_LBM_TCVR       0x000000C0    /* tcvr loopback mode */
#define E1000_RCTL_DTYP_MASK      0x00000C00    /* Descriptor type mask */
#define E1000_RCTL_DTYP_PS        0x00000400    /* Packet Split descriptor */
#define E1000_RCTL_RDMTS_HALF     0x00000000    /* rx desc min threshold size */
#define E1000_RCTL_RDMTS_QUAT     0x00000100    /* rx desc min threshold size */
#define E1000_RCTL_RDMTS_EIGTH    0x00000200    /* rx desc min threshold size */
#define E1000_RCTL_MO_SHIFT       12            /* multicast offset shift */
#define E1000_RCTL_MO_0           0x00000000    /* multicast offset 11:0 */
#define E1000_RCTL_MO_1           0x00001000    /* multicast offset 12:1 */
#define E1000_RCTL_MO_2           0x00002000    /* multicast offset 13:2 */
#define E1000_RCTL_MO_3           0x00003000    /* multicast offset 15:4 */
#define E1000_RCTL_MDR            0x00004000    /* multicast desc ring 0 */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
/* These buffer sizes are valid if E1000_RCTL_BSEX is 0 */
#define E1000_RCTL_SZ_2048        0x00000000    /* rx buffer size 2048 */
#define E1000_RCTL_SZ_1024        0x00010000    /* rx buffer size 1024 */
#define E1000_RCTL_SZ_512         0x00020000    /* rx buffer size 512 */
#define E1000_RCTL_SZ_256         0x00030000    /* rx buffer size 256 */
/* These buffer sizes are valid if E1000_RCTL_BSEX is 1 */
#define E1000_RCTL_SZ_16384       0x00010000    /* rx buffer size 16384 */
#define E1000_RCTL_SZ_8192        0x00020000    /* rx buffer size 8192 */
#define E1000_RCTL_SZ_4096        0x00030000    /* rx buffer size 4096 */
#define E1000_RCTL_VFE            0x00040000    /* vlan filter enable */
#define E1000_RCTL_CFIEN          0x00080000    /* canonical form enable */
#define E1000_RCTL_CFI            0x00100000    /* canonical form indicator */
#define E1000_RCTL_DPF            0x00400000    /* discard pause frames */
#define E1000_RCTL_PMCF           0x00800000    /* pass MAC control frames */
#define E1000_RCTL_BSEX           0x02000000    /* Buffer size extension */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */
#define E1000_RCTL_FLXBUF_MASK    0x78000000    /* Flexible buffer size */
#define E1000_RCTL_FLXBUF_SHIFT   27            /* Flexible buffer shift */


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
#define E1000_TXD_CMD_EOP    0x00000001 /* End of Packet */
#define E1000_TXD_CMD_IFCS   0x00000002 /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_IC     0x00000004 /* Insert Checksum */
#define E1000_TXD_CMD_RS     0x00000008 /* Report Status */
#define E1000_TXD_CMD_RPS    0x00000010 /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT   0x00000020 /* Descriptor extension (0 = legacy) */
#define E1000_TXD_CMD_VLE    0x00000040 /* Add VLAN tag */
#define E1000_TXD_CMD_IDE    0x00000080 /* Enable Tidv register */

/* Transmit descriptor status */
#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */
#define E1000_TXD_STAT_EC    0x00000002 /* Excess Collisions */
#define E1000_TXD_STAT_LC    0x00000004 /* Late Collisions */
#define E1000_TXD_STAT_TU    0x00000008 /* Transmit underrun */

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */
#define E1000_RXD_STAT_IXSM     0x04    /* Ignore checksum */
#define E1000_RXD_STAT_VP       0x08    /* IEEE VLAN Packet */
#define E1000_RXD_STAT_UDPCS    0x10    /* UDP xsum caculated */
#define E1000_RXD_STAT_TCPCS    0x20    /* TCP xsum calculated */
#define E1000_RXD_STAT_IPCS     0x40    /* IP xsum calculated */
#define E1000_RXD_STAT_PIF      0x80    /* passed in-exact filter */
#define E1000_RXD_STAT_IPIDV    0x200   /* IP identification valid */
#define E1000_RXD_STAT_UDPV     0x400   /* Valid UDP checksum */
#define E1000_RXD_STAT_ACK      0x8000  /* ACK Packet indication */
#define E1000_RXD_ERR_CE        0x01    /* CRC Error */
#define E1000_RXD_ERR_SE        0x02    /* Symbol Error */
#define E1000_RXD_ERR_SEQ       0x04    /* Sequence Error */
#define E1000_RXD_ERR_CXE       0x10    /* Carrier Extension Error */
#define E1000_RXD_ERR_TCPE      0x20    /* TCP/UDP Checksum Error */
#define E1000_RXD_ERR_IPE       0x40    /* IP Checksum Error */
#define E1000_RXD_ERR_RXE       0x80    /* Rx Data Error */
#define E1000_RXD_SPC_VLAN_MASK 0x0FFF  /* VLAN ID is in lower 12 bits */
#define E1000_RXD_SPC_PRI_MASK  0xE000  /* Priority is in upper 3 bits */
#define E1000_RXD_SPC_PRI_SHIFT 13
#define E1000_RXD_SPC_CFI_MASK  0x1000  /* CFI is bit 12 */
#define E1000_RXD_SPC_CFI_SHIFT 12

/* Receive Address */
#define E1000_RAH_AV  0x80000000        /* Receive descriptor valid */

/* Maximum transmit descriptors (must be a multiple of 8 value) */
#define E1000_MAX_TDESC   64

/* Maximum ethernet packet size on the wire */
#define MAX_PACKET_SIZE   1518

/* Maximum receive descriptors (must be a multiple of 8 value) */
#define E1000_MAX_RDESC   128

/* Maximum buffer size */
#define MAX_BUFFER_SIZE   2048

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

struct rx_desc
{
	uint64_t addr;
	uint16_t length;
	uint16_t csum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
};

volatile uint32_t *e1000;

static struct tx_desc tx_descs[E1000_MAX_TDESC] __attribute__((aligned(16)));
static uint8_t tx_pkts[E1000_MAX_TDESC][MAX_PACKET_SIZE];

static struct rx_desc rx_descs[E1000_MAX_RDESC] __attribute__((aligned(16)));
static uint8_t rx_pkts[E1000_MAX_RDESC][MAX_BUFFER_SIZE];

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
		tx_descs[i].status |= E1000_TXD_STAT_DD;
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

        // Receive initialization
	assert((uintptr_t) rx_descs % 16 == 0);
	for (i = 0; i < E1000_MAX_RDESC; i++) {
		rx_descs[i].addr = PADDR(rx_pkts[i]);
		rx_descs[i].status |= E1000_RXD_STAT_DD;
	}
	e1000_write_reg(E1000_RAL, 0x12005452);
	e1000_write_reg(E1000_RAH, 0x5634 | E1000_RAH_AV);
	e1000_write_reg(E1000_MTA, 0);
	e1000_write_reg(E1000_IMS, 0);
	e1000_write_reg(E1000_RDBAL, PADDR(rx_descs));
	e1000_write_reg(E1000_RDLEN, E1000_MAX_RDESC * sizeof(struct rx_desc));
	assert(e1000_read_reg(E1000_RDLEN) % 128 == 0);
	e1000_write_reg(E1000_RDH, 0);
	e1000_write_reg(E1000_RDT, E1000_MAX_RDESC - 1);
	e1000_write_reg(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
	return 0;
}

int
e1000_transmit(void *data, size_t size)
{
	uint32_t tdt;

	if (size > MAX_PACKET_SIZE)
		return -E_INVAL;

	tdt = e1000_read_reg(E1000_TDT);
	if (!(tx_descs[tdt].status & E1000_TXD_STAT_DD))
		return -E_INVAL;

	memcpy(tx_pkts[tdt], data, size);
	tx_descs[tdt].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
	tx_descs[tdt].length = size;
	tx_descs[tdt].status &= ~E1000_TXD_STAT_DD;
	e1000_write_reg(E1000_TDT, (tdt + 1) % E1000_MAX_TDESC);
	return 0;
}

int
e1000_receive(void *data)
{
	return 0;
}
