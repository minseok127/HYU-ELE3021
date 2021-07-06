#ifndef ADDRSTACK_H
#define ADDRSTACK_H

#define SIZE_ADDRSTACK (100)

struct AddrStack {
	int top;
	int size;
	unsigned int arr[SIZE_ADDRSTACK];
};

void InitAddrStack(struct AddrStack* s);
void PushAddrStack(struct AddrStack* s, unsigned int x);
unsigned int GetTopAddrStack(struct AddrStack* s);
void PopAddrStack(struct AddrStack* s);

#endif // STACK_H
