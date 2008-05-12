/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef LLIST_HEADER_INCLUDED
#define	LLIST_HEADER_INCLUDED

/*
 * Linked list macros.
 */
struct llhead {
	struct llhead	*prev;
	struct llhead	*next;
};

#define	LL_INIT(N)	((N)->next = (N)->prev = (N))

#define LL_HEAD(H)	struct llhead H = { &H, &H }

#define LL_ENTRY(P,T,N) ((T *)((char *)(P) - offsetof(T, N)))

#define	LL_ADD(H, N)							\
	do {								\
		((H)->next)->prev = (N);				\
		(N)->next = ((H)->next);				\
		(N)->prev = (H);					\
		(H)->next = (N);					\
	} while (0)

#define	LL_TAIL(H, N)							\
	do {								\
		((H)->prev)->next = (N);				\
		(N)->prev = ((H)->prev);				\
		(N)->next = (H);					\
		(H)->prev = (N);					\
	} while (0)

#define	LL_DEL(N)							\
	do {								\
		((N)->next)->prev = ((N)->prev);			\
		((N)->prev)->next = ((N)->next);			\
		LL_INIT(N);						\
	} while (0)

#define	LL_EMPTY(N)	((N)->next == (N))

#define	LL_FOREACH(H,N)	for (N = (H)->next; N != (H); N = (N)->next)

#define LL_FOREACH_SAFE(H,N,T)						\
	for (N = (H)->next, T = (N)->next; N != (H);			\
			N = (T), T = (N)->next)

#endif /* LLIST_HEADER_INCLUDED */
