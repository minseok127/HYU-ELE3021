#include "addrstack.h"

void InitAddrStack(struct AddrStack* s) {
	s->top = -1;
	s->size = 0;
}

void PushAddrStack(struct AddrStack* s, unsigned int x) {
	// If stack is full
	if (s->size == SIZE_ADDRSTACK) {
		return;
	}

	// Push it
	s->arr[++s->top] = x;
	s->size++;
}

unsigned int GetTopAddrStack(struct AddrStack* s) {
	// If stack is empty
	if (s->size == 0) {
		return 0; // nullptr
	}

	return s->arr[s->top];
}

void PopAddrStack(struct AddrStack* s) {
	// If stack is empty
	if (s->size == 0) {
		return;
	}

	s->size--;
	s->top--;
}
