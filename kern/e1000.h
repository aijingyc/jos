#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <inc/types.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100e

struct pci_func;

int e1000_attach(struct pci_func *f);
int e1000_transmit(void *data, size_t size);
int e1000_receive(void *data);

#endif	// JOS_KERN_E1000_H
