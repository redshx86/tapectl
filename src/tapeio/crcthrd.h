/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>

/* ---------------------------------------------------------------------------------------------- */
/* CRC32 computation thread context */

#define CRC_THREAD_WRITE_EV		0x0001
#define CRC_THREAD_READ_EV		0x0002

struct crc32_thread
{
	/* data buffer */
	CRITICAL_SECTION buf_ptr_lock;	/* critical section to accessing buffer pointers and events */
	BYTE *buffer;				/* data buffer */
	size_t buf_size;			/* size of data buffer */
	size_t buf_data_offset;		/* offset of data in buffer */
	size_t buf_data_length;		/* size of data available in buffer */

	/* read/write thresholds */
	size_t thres_write;			/* required free space to write current block */
	size_t chunk_size;			/* size of block to calculate crc */
	unsigned int event_flag;	/* active event mask */
	HANDLE h_ev_writable;		/* buffer writeable event (free space >= thres_write)  */
	HANDLE h_ev_readable;		/* buffer readable event (avail data >= chunk_size) */

	/* stop event */
	HANDLE h_ev_exit;			/* process remaining data and exit thread */

	/* thread handle */
	HANDLE h_thread;

	/* computed crc32 */
	unsigned int result;
};

/* ---------------------------------------------------------------------------------------------- */

int crc32_thread_init(struct crc32_thread *cs, size_t buf_size, size_t block_size, int priority);
void crc32_thread_write(struct crc32_thread *cs, const void *data, size_t length);
unsigned int crc32_thread_finish(struct crc32_thread *cs);

/* ---------------------------------------------------------------------------------------------- */
