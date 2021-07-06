#ifndef TICKETBOX_H
#define TICKETBOX_H

#include "spinlock.h"

struct TicketBox {
	int ticket;
	struct spinlock lock;
};

#endif // TICKETBOX_H
