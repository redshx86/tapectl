/* ---------------------------------------------------------------------------------------------- */

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <crtdbg.h>
#include "../config.h"
#include "../util/fmt.h"
#include "ratectr.h"
#include "filethrd.h"
#include "filecopy.h"

/* ---------------------------------------------------------------------------------------------- */

struct file_copy_ctx
{
	struct file_thread_ctx write_thread;
	struct file_thread_ctx read_thread;
	struct rate_counter write_rate_ctr;
	struct rate_counter read_rate_ctr;
	unsigned int flags;
	TCHAR msg_buf[256];
};

/* ---------------------------------------------------------------------------------------------- */

/* Show transfer statistics */

static void display_copy_progress(struct msg_filter *mf, struct file_copy_ctx *ctx,
	unsigned __int64 src_data_size, unsigned int msecs_cur)
{
	unsigned int write_flags, read_flags;
	unsigned __int64 write_total, read_total;
	unsigned __int64 write_rate, read_rate;
	TCHAR fmt_buf1[64], fmt_buf2[64], *msg_ptr;

	/* Acquire data from I/O threads */
	write_flags = ctx->write_thread.flags;
	read_flags = ctx->read_thread.flags;
	file_thread_get_total_bytes(&(ctx->write_thread), &write_total, NULL);
	file_thread_get_total_bytes(&(ctx->read_thread), &read_total, NULL);

	/* Calculate current read/write speed */
	if(!(write_flags & WRITE_THREAD_BUFFERING)) {
		write_rate = rate_update(&(ctx->write_rate_ctr), msecs_cur, write_total);
	} else {
		rate_reset(&(ctx->write_rate_ctr));
		write_rate = 0;
	}
	if(!(read_flags & READ_THREAD_DEBUFFERING)) {
		read_rate = rate_update(&(ctx->read_rate_ctr), msecs_cur, read_total);
	} else {
		rate_reset(&(ctx->read_rate_ctr));
		read_rate = 0;
	}

	msg_ptr = ctx->msg_buf;

	/* Display written data size */
	msg_ptr += _stprintf(msg_ptr, _T("%s"), fmt_block_size(fmt_buf1, write_total, 0));

	/* Display total data size and progress if known */
	if(src_data_size != 0) {
		msg_ptr += _stprintf(msg_ptr, _T(" / %s (%.1f%%)"),
			fmt_block_size(fmt_buf1, src_data_size, 0),
			100.0 * write_total / src_data_size);
	}

	/* Display buffering mode and speed */
	if(write_flags & WRITE_THREAD_FLUSHING) {
		msg_ptr += _stprintf(msg_ptr, _T(" Flushing:%s/s"),
			fmt_block_size(fmt_buf1, write_rate, 0));
	} else if(write_flags & WRITE_THREAD_BUFFERING) {
		msg_ptr += _stprintf(msg_ptr, _T(" Buffering:%s/s"),
			fmt_block_size(fmt_buf1, read_rate, 0));
	} else if(read_flags & READ_THREAD_DEBUFFERING) {
		msg_ptr += _stprintf(msg_ptr, _T(" Debuffering:%s/s"),
			fmt_block_size(fmt_buf1, write_rate, 0));
	} else {
		msg_ptr += _stprintf(msg_ptr, _T(" R:%s/s W:%s/s"),
			fmt_block_size(fmt_buf1, read_rate, 0),
			fmt_block_size(fmt_buf2, write_rate, 0));
	}

	/* Display buffer status */
	msg_ptr += _stprintf(msg_ptr, _T(" Buf:%s"),
		fmt_block_size(fmt_buf1, bigbuf_data_avail(ctx->write_thread.cb), 0));

	/* Display ETA */
	if((write_rate != 0) && (src_data_size != 0)) {
		unsigned __int64 eta = (src_data_size - write_total) / write_rate;
		if(eta < 31536000ULL) { /* don't display yearwise times */
			msg_ptr += _stprintf(msg_ptr, _T(" ETA %s"),
				fmt_elapsed_time(fmt_buf1, (unsigned int)eta, 0));
		}
	}

	/* Output message */
	msg_print(mf, MSG_INFO, _T("%-79s\r"), ctx->msg_buf);
}

/* ---------------------------------------------------------------------------------------------- */

/* Check copy result and show final statistics */

static int check_copy_result(struct msg_filter *mf, struct file_copy_ctx *ctx,
	unsigned int seconds_elapsed)
{
	int success = 1;

	/* Check for read error */
	switch(ctx->read_thread.error)
	{
	case NO_ERROR:
		break;
	case ERROR_HANDLE_EOF:
		msg_print(mf, MSG_VERBOSE, _T("End of file reached.\n"));
		break;
	case ERROR_SETMARK_DETECTED:
		msg_print(mf, MSG_INFO, _T("Setmark reached.\n"));
		break;
	case ERROR_FILEMARK_DETECTED:
		msg_print(mf, MSG_INFO, _T("Filemark reached.\n"));
		break;
	case ERROR_NO_DATA_DETECTED:
		msg_print(mf, MSG_INFO, _T("End of data reached.\n"));
		break;
	case ERROR_END_OF_MEDIA:
		msg_print(mf, MSG_INFO, _T("End of media reached.\n"));
		break;
	case ERROR_INVALID_BLOCK_LENGTH:
		msg_print(mf, MSG_ERROR, _T("Can't read data: block size mismatch.\n"));
		success = 0;
		break;
	case ERROR_NO_MEDIA_IN_DRIVE:
		msg_print(mf, MSG_ERROR, _T("Can't read data: no media in drive.\n"));
		success = 0;
		break;
	case ERROR_OPERATION_ABORTED:
		success = 0; /* already shown when aborted */
		break;
	default:
		msg_print(mf, MSG_ERROR, _T("Can't read: %s (%u).\n"),
			msg_winerr(mf, ctx->read_thread.error), ctx->read_thread.error);
		success = 0;
		break;
	}

	/* Check for write error */
	switch(ctx->write_thread.error)
	{
	case NO_ERROR:
		break;
	case ERROR_DISK_FULL:
	case ERROR_HANDLE_DISK_FULL:
	case ERROR_END_OF_MEDIA:
		msg_print(mf, MSG_ERROR, _T("Can't write data: not enough free space.\n"));
		success = 0;
		break;
	case ERROR_INVALID_BLOCK_LENGTH:
		msg_print(mf, MSG_ERROR, _T("Can't write data: invalid block size.\n"));
		success = 0;
		break;
	case ERROR_OPERATION_ABORTED:
		success = 0; /* already shown when aborted */
		break;
	default:
		msg_print(mf, MSG_ERROR, _T("Can't write: %s (%u).\n"),
			msg_winerr(mf, ctx->write_thread.error), ctx->write_thread.error);
		success = 0;
		break;
	}

	/* Check read/write crc */
	if(success && (ctx->write_thread.data_crc != ctx->read_thread.data_crc))
	{
		msg_print(mf, MSG_ERROR,
			_T("Read/write CRC mismatch!\n")
			_T("Looks like internal program error or memory error.\n")
			_T("Written data INVALID.\n"));
		success = 0;
	}

	/* Display statistics */
	if(success && (mf->report_level >= MSG_INFO))
	{
		TCHAR fmt_buf[64];

		/* Show written data size */
		if( (ctx->write_thread.padded_io_bytes > ctx->write_thread.data_io_bytes) &&
			!(ctx->flags & COPY_NO_PADDING_INFO) )
		{
			unsigned int padding_bytes = (unsigned int)
				(ctx->write_thread.padded_io_bytes - ctx->write_thread.data_io_bytes);
			msg_print(mf, MSG_INFO, _T("Data size    : %s (padding: %u byte%s)\n"),
				fmt_block_size(fmt_buf, ctx->write_thread.padded_io_bytes, 1),
				padding_bytes, (padding_bytes == 1) ? _T("") : _T("s"));
		}
		else
		{
			msg_print(mf, MSG_INFO, _T("Data size    : %s\n"),
				fmt_block_size(fmt_buf, ctx->write_thread.data_io_bytes, 1));
		}

		/* Show data CRC32 */
		if( (ctx->write_thread.padded_crc != ctx->write_thread.data_crc) &&
			!(ctx->flags & COPY_NO_PADDING_INFO) )
		{
			msg_print(mf, MSG_INFO, _T("CRC32        : %08X (unpadded: %08X)\n"),
				ctx->write_thread.padded_crc,
				ctx->write_thread.data_crc);
		}
		else
		{
			msg_print(mf, MSG_INFO, _T("CRC32        : %08X\n"),
				ctx->write_thread.data_crc);
		}

		/* Show elapsed time */
		if(seconds_elapsed > 0) {
			msg_print(mf, MSG_INFO, _T("Elapsed time : %s\n"),
				fmt_elapsed_time(fmt_buf, seconds_elapsed, 1));
		}
	}

	return success;
}

/* ---------------------------------------------------------------------------------------------- */

enum {
	EVENT_ID_ABORT,
	EVENT_ID_WRITE_END,
	EVENT_ID_READ_END,

	EVENT_COUNT
};

static HANDLE copy_abort_event;

static BOOL WINAPI copy_abort_handler(DWORD code)
{
	if( ((code == CTRL_C_EVENT) || (code == CTRL_CLOSE_EVENT)) &&
		(WaitForSingleObject(copy_abort_event, 0) == WAIT_TIMEOUT) )
	{
		SetEvent(copy_abort_event);
		return TRUE;
	}

	return FALSE;
}

int copy_file(struct msg_filter *mf, struct big_buffer *cb, unsigned int flags,
	HANDLE h_dst, size_t dst_queue_size, size_t dst_block_size, size_t dst_block_align,
	HANDLE h_src, size_t src_queue_size, size_t src_block_size, unsigned __int64 src_data_size,
	size_t crc_buffer_size, size_t crc_block_size,
	unsigned __int64 *p_data_size, unsigned __int64 *p_padded_size)
{
	struct file_copy_ctx *ctx;
	unsigned int write_flags, read_flags;
	HANDLE events[EVENT_COUNT];
	DWORD msecs_begin, seconds_elapsed;
	int flushing = 0, success = 0;

	/* Check transfer parameters */

	msg_print(mf, MSG_VERY_VERBOSE,
		_T("Copy parameters:\n")
		_T("Destination queue size  : %Iu\n")
		_T("Destination block size  : %Iu\n")
		_T("Destination block align : %Iu\n")
		_T("Source queue size       : %Iu\n")
		_T("Source block size       : %Iu\n")
		_T("Source data size        : %I64u\n")
		_T("CRC buffer size         : %Iu\n")
		_T("CRC block size          : %Iu\n"),
		dst_queue_size, dst_block_size, dst_block_align,
		src_queue_size, src_block_size, src_data_size,
		crc_buffer_size, crc_block_size);

	if( ((flags & COPY_SUSTAIN_WRITE) && (flags & COPY_SUSTAIN_READ)) ||
		((dst_block_align > 1) && (dst_block_size % dst_block_align != 0)) ||
		(cb->buf_size < src_block_size) || (crc_buffer_size < src_block_size) ||
		(cb->buf_size < dst_block_size) || (crc_buffer_size < dst_block_size) ||
		(crc_buffer_size < crc_block_size) )
	{
		msg_print(mf, MSG_ERROR, _T("Copy parameters invalid (use -V to check).\n"));
		return 0;
	}

	/* Allocate context */
	ctx = malloc(sizeof(struct file_copy_ctx));
	events[EVENT_ID_ABORT] = CreateEvent(NULL, TRUE, FALSE, NULL);
	if((ctx == NULL) || (events[EVENT_ID_ABORT] == NULL))
		goto cleanup;

	ctx->flags = flags;

	/* Spawn writing thread */
	write_flags = IO_THREAD_MODE_WRITE;
	if(flags & COPY_SUSTAIN_WRITE)
		write_flags |= IO_THREAD_SUSTAIN;
	if( ! file_thread_start(
		&(ctx->write_thread),
		cb,
		h_dst,
		write_flags,
		cb->buf_size - (src_block_size - 1),
		dst_block_size,
		dst_block_align,
		dst_queue_size,
		crc_buffer_size,
		crc_block_size) )
	{
		msg_print(mf, MSG_ERROR, _T("Can't spawn data writing thread (out of memory?)"));
		goto cleanup;
	}

	/* Spawn reading thread */
	read_flags = IO_THREAD_MODE_READ;
	if(flags & COPY_SUSTAIN_READ)
		read_flags |= IO_THREAD_SUSTAIN;
	if( ! file_thread_start(
		&(ctx->read_thread),
		cb,
		h_src,
		read_flags,
		cb->buf_size - (dst_block_size - 1),
		src_block_size,
		0,
		src_queue_size,
		crc_buffer_size,
		crc_block_size) )
	{
		file_thread_abort(&(ctx->write_thread));
		file_thread_finish(&(ctx->write_thread));

		msg_print(mf, MSG_ERROR, _T("Can't spawn data reading thread (out of memory?)"));
		goto cleanup;
	}


	/* Initialize transfer speed counters */
	msecs_begin = GetTickCount();
	rate_reset(&(ctx->write_rate_ctr));
	rate_reset(&(ctx->read_rate_ctr));

	/* Set abort handler */
	copy_abort_event = events[EVENT_ID_ABORT];
	SetConsoleCtrlHandler(copy_abort_handler, TRUE);

	/* Select events */
	events[EVENT_ID_WRITE_END] = ctx->write_thread.h_thread;
	events[EVENT_ID_READ_END] = ctx->read_thread.h_thread;

	for(;;)
	{
		/* Wait for copy abort / finish / use timeout to display stats */
		DWORD event_id;
		if(!flushing) {
			event_id = WaitForMultipleObjects(3, events, FALSE, STATS_REFRESH_INTERVAL);
		} else {
			event_id = WaitForMultipleObjects(2, events, FALSE, STATS_REFRESH_INTERVAL);
		}
		
		/* Handle abort (handle wait error as abort) */
		if((event_id == EVENT_ID_ABORT) || (event_id == WAIT_FAILED))
		{
			if(event_id == EVENT_ID_ABORT)
				msg_print(mf, MSG_MESSAGE, _T("\nCanceling transfer...\n"));
			file_thread_abort(&(ctx->read_thread));
			file_thread_abort(&(ctx->write_thread));
			break;
		}

		/* Handle read completion */
		if(event_id == EVENT_ID_READ_END)
		{
			assert(!flushing);
			/* Start flushing data */
			file_thread_flush(&(ctx->write_thread));
			flushing = 1;
		}

		if(event_id == EVENT_ID_WRITE_END)
		{
			/* Abort reading if write ended prematurely */
			if(!flushing)
				file_thread_abort(&(ctx->read_thread));
			break;
		}

		/* Show transfer statistics */
		if(mf->report_level >= MSG_INFO)
			display_copy_progress(mf, ctx, src_data_size, GetTickCount());
	}

	/* Remove abort handler */
	SetConsoleCtrlHandler(copy_abort_handler, FALSE);

	/* Calculate elapsed time */
	seconds_elapsed = (GetTickCount() - msecs_begin) / 1000UL;

	/* Free read/write thread data and reset copy buffer */
	file_thread_finish(&(ctx->read_thread));
	file_thread_finish(&(ctx->write_thread));
	bigbuf_reset(cb);

	/* Wipe stats string, check result and show stats */
	msg_print(mf, MSG_INFO, _T("%-79s\r"), _T(""));
	success = check_copy_result(mf, ctx, seconds_elapsed);

	if(success) {
		if(p_data_size)
			*p_data_size = ctx->write_thread.data_io_bytes;
		if(p_padded_size)
			*p_padded_size = ctx->write_thread.padded_io_bytes;
	}

	/* Free memory */
cleanup:
	if(events[EVENT_ID_ABORT] != NULL)
		CloseHandle(events[EVENT_ID_ABORT]);
	free(ctx);

	return success;
}

/* ---------------------------------------------------------------------------------------------- */
