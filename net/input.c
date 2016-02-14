#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	uint8_t buf[2048];
	size_t size;
	int perm = PTE_P|PTE_U|PTE_W;;

	while (1) {
		while (sys_net_try_receive(buf, &size) < 0) {
			sys_yield();
		}

		while (sys_page_alloc(0, &nsipcbuf, perm) < 0);

		nsipcbuf.pkt.jp_len = size;
		memmove(nsipcbuf.pkt.jp_data, buf, size);

		while (sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, perm) < 0);
	}
}
