/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "bigbuff.h"
#include "crcthrd.h"

/* ---------------------------------------------------------------------------------------------- */

/* I/O thread mode flags */
#define IO_THREAD_MODE_WRITE			0x0001	/* Start writing thread */
#define IO_THREAD_MODE_READ				0x0002	/* Start reading thread */
#define IO_THREAD_SUSTAIN				0x0004	/* Use buffering (write) / debuffering (read) */

/* Writing thread internal state */
#define WRITE_THREAD_BUFFERING			0x0100	/* In buffering state (sustain mode) */
#define WRITE_THREAD_FLUSHING			0x0200	/* Flushing remaining data from buffer */
#define WRITE_THREAD_END_OF_DATA		0x0400	/* No more data in buffer, flushing queue */
#define WRITE_THREAD_DRIVER_CONGESTION	0x1000	/* Device can't take more requests now */

/* Reading thread internal state */
#define READ_THREAD_DEBUFFERING			0x0100	/* In debuffering state (sustain mode) */
#define READ_THREAD_END_OF_FILE			0x0400	/* At EOF (or error ocurred), flushing queue */
#define READ_THREAD_DRIVER_CONGESTION	0x1000	/* Device can't take more requests now */

#define IO_THREAD_ABORT_TIMEOUT			5000

/* Async operaton queue entry */
struct io_queue_entry
{
	OVERLAPPED ov;
	BYTE *buf;

	int is_async;
	size_t data_size;
	size_t padded_size;
};

/* Thread data */
struct file_thread_ctx
{
	/* stream */
	struct big_buffer *cb;
	HANDLE h_file;

	/* parameters */
	unsigned __int64 thres_buf_debuf;
	size_t io_block_size;
	size_t io_block_align;

	/* state flags */
	unsigned int flags;

	/* Async IO queue */
	size_t queue_size;
	size_t queue_offset;
	size_t queue_nused;
	size_t queue_npend;
	struct io_queue_entry *queue_entry;
	unsigned __int64 queue_data_pos;

	/* Sync IO buffer */
	BYTE *io_buf;

	/* i/o stats */
	CRITICAL_SECTION total_bytes_lock;
	unsigned __int64 data_io_bytes;
	unsigned __int64 padded_io_bytes;
	unsigned int data_crc;
	unsigned int padded_crc;
	DWORD error;

	/* crc32 thread */
	struct crc32_thread crc_thrd;

	/* thread handles */
	HANDLE h_ev_abort;
	HANDLE h_ev_flush;
	HANDLE h_thread;
};

/* ---------------------------------------------------------------------------------------------- */

void file_thread_abort(struct file_thread_ctx *ctx);
void file_thread_flush(struct file_thread_ctx *ctx);

void file_thread_get_total_bytes(
	struct file_thread_ctx *ctx,
	unsigned __int64 *p_data_io_bytes,
	unsigned __int64 *p_padded_io_bytes);

/* spawn file I/O thread */
int file_thread_start(
	struct file_thread_ctx *ctx,
	struct big_buffer *cb,		/* buffer to read from / write to */
	HANDLE h_file,				/* file handle to write to / read from */
	unsigned int flags,			/* flags */
	unsigned __int64 thres_buf_debuf,	/* full buffer / free buffer threshold */
	size_t io_block_size,		/* I/O block size for file access */
	size_t io_block_align,		/* I/O block alignment for file access */
	size_t queue_size,			/* size of I/O queue (0 = sync) */
	size_t crc_buffer_size,		/* size of buffer for crc thread */
	size_t crc_block_size);		/* size of block to calculate crc */

/* wait for I/O thread exit and cleanup */
void file_thread_finish(struct file_thread_ctx *ctx);

/* ---------------------------------------------------------------------------------------------- */
