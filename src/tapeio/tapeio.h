/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include "bigbuff.h"
#include "../util/msgfilt.h"

/* ---------------------------------------------------------------------------------------------- */

struct tape_io_ctx
{
	int lock_pages_changed;
	int lock_pages_prev_state;

	struct big_buffer cb;

	unsigned int io_block_size;
	unsigned int io_queue_size;
	int use_windows_buffering;

	unsigned int file_block_align;
	unsigned int file_block_size;
	unsigned int file_open_flags;

	unsigned int crc_block_size;
	unsigned int crc_buffer_size;
};

/* ---------------------------------------------------------------------------------------------- */

/* Write file to tape */
int tape_file_write(struct msg_filter *mf, struct tape_io_ctx *ctx,
	HANDLE h_tape, const TCHAR *filename);

/* Read file from tape */
int tape_file_read(struct msg_filter *mf, struct tape_io_ctx *ctx,
	HANDLE h_tape, const TCHAR *filename);

/* Initialize buffer for reading/writing to tape */
int tape_io_init_buffer(struct msg_filter *mf, struct tape_io_ctx *ctx,
	unsigned __int64 buffer_size, unsigned int io_block_size, unsigned int io_queue_size,
	int use_windows_buffering);

/* Free buffer */
void tape_io_cleanup(struct tape_io_ctx *ctx);

/* ---------------------------------------------------------------------------------------------- */
