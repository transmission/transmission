/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#ifndef IO_HEADER_INCLUDED
#define	IO_HEADER_INCLUDED

#include <assert.h>
#include <stddef.h>

/*
 * I/O buffer descriptor
 */
struct io {
	char		*buf;		/* IO Buffer			*/
	size_t		size;		/* IO buffer size		*/
	size_t		head;		/* Bytes read			*/
	size_t		tail;		/* Bytes written		*/
	size_t		total;		/* Total bytes read		*/
};

static __inline void
io_clear(struct io *io)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	io->total = io->tail = io->head = 0;
}

static __inline char *
io_space(struct io *io)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->head <= io->size);
	return (io->buf + io->head);
}

static __inline char *
io_data(struct io *io)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->tail <= io->size);
	return (io->buf + io->tail);
}

static __inline size_t
io_space_len(const struct io *io)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->head <= io->size);
	return (io->size - io->head);
}

static __inline size_t
io_data_len(const struct io *io)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->head <= io->size);
	assert(io->tail <= io->head);
	return (io->head - io->tail);
}

static __inline void
io_inc_tail(struct io *io, size_t n)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->tail <= io->head);
	assert(io->head <= io->size);
	io->tail += n;
	assert(io->tail <= io->head);
	if (io->tail == io->head)
		io->head = io->tail = 0;
}

static __inline void
io_inc_head(struct io *io, size_t n)
{
	assert(io->buf != NULL);
	assert(io->size > 0);
	assert(io->tail <= io->head);
	io->head += n;
	io->total += n;
	assert(io->head <= io->size);
}

#endif /* IO_HEADER_INCLUDED */
