/* ---------------------------------------------------------------------------------------------- */

#include <process.h>
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <crtdbg.h>
#include "crc32.h"
#include "filethrd.h"

/* ---------------------------------------------------------------------------------------------- */

/* Sync zero-writing thread */
static unsigned int __stdcall write_thread_sync_nullbuf(struct file_thread_ctx *ctx)
{
	memset(ctx->io_buf, 0, ctx->io_block_size);
	
	for(;;)
	{
		DWORD cb_written;
		
		if(WaitForSingleObject(ctx->h_ev_abort, 0) != WAIT_TIMEOUT) {
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* Write zeroes to output stream */
		if(!WriteFile(ctx->h_file, ctx->io_buf, (DWORD)(ctx->io_block_size), &cb_written, NULL))
			ctx->error = GetLastError();

		/* Update length and CRC32 of stream */
		EnterCriticalSection(&(ctx->total_bytes_lock));
		ctx->data_io_bytes += cb_written;
		ctx->padded_io_bytes += cb_written;
		LeaveCriticalSection(&(ctx->total_bytes_lock));
		crc32_thread_write(&(ctx->crc_thrd), ctx->io_buf, cb_written);

		/* Check for error */
		if((ctx->error != NO_ERROR) || (cb_written < ctx->io_block_size))
			break;
	}
	
	return ctx->error;
}

/* Sync crc-only thread */
static unsigned int __stdcall read_thread_sync_nullbuf(struct file_thread_ctx *ctx)
{
	for(;;)
	{
		DWORD cb_read;
		
		if(WaitForSingleObject(ctx->h_ev_abort, 0) != WAIT_TIMEOUT) {
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* Read data from input stream */
		if(!ReadFile(ctx->h_file, ctx->io_buf, (DWORD)(ctx->io_block_size), &cb_read, NULL))
			ctx->error = GetLastError();

		/* Update length and CRC32 of stream */
		EnterCriticalSection(&(ctx->total_bytes_lock));
		ctx->data_io_bytes += cb_read;
		ctx->padded_io_bytes += cb_read;
		LeaveCriticalSection(&(ctx->total_bytes_lock));
		crc32_thread_write(&(ctx->crc_thrd), ctx->io_buf, cb_read);

		/* Check for error or EOF */
		if((ctx->error != NO_ERROR) || (cb_read < ctx->io_block_size))
			break;
	}
	
	return ctx->error;
}

/* ---------------------------------------------------------------------------------------------- */

enum {
	SYNC_EV_ID_ABORT,
	SYNC_EV_ID_FLUSH,
	SYNC_EV_ID_BUFFER,
	
	SYNC_EV_COUNT
};

/* Sync data write thread */
static unsigned int __stdcall write_thread_sync(struct file_thread_ctx *ctx)
{
	/* Select events to handle */
	HANDLE ev_arr[SYNC_EV_COUNT];
	ev_arr[SYNC_EV_ID_ABORT] = ctx->h_ev_abort;
	ev_arr[SYNC_EV_ID_FLUSH] = ctx->h_ev_flush;
	ev_arr[SYNC_EV_ID_BUFFER] = ctx->cb->thres_rd_ev;

	/* Set buffer threshold */
	if(ctx->flags & IO_THREAD_SUSTAIN) {
		bigbuf_set_thres_read(ctx->cb, ctx->thres_buf_debuf);
		ctx->flags |= WRITE_THREAD_BUFFERING;
	} else {
		bigbuf_set_thres_read(ctx->cb, ctx->io_block_size);
	}

	for(;;)
	{
		/* Wait for events */
		DWORD event_id = WaitForMultipleObjects(SYNC_EV_COUNT, ev_arr, FALSE, INFINITE);
		if(event_id >= SYNC_EV_COUNT) {
			ctx->error = GetLastError();
			break;
		}

		/* Handle abort command */
		if(event_id == SYNC_EV_ID_ABORT)
		{
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* Handle flushing command */
		if(event_id == SYNC_EV_ID_FLUSH)
		{
			ctx->flags &= ~WRITE_THREAD_BUFFERING;
			ctx->flags |= WRITE_THREAD_FLUSHING;
			bigbuf_set_thres_read(ctx->cb, 0);
			ResetEvent(ctx->h_ev_flush);
		}

		/* Handle buffer readable */
		if(event_id == SYNC_EV_ID_BUFFER)
		{
			unsigned __int64 buf_avail;
			size_t data_size;

			/* Calculate size of block to write */
			buf_avail = bigbuf_data_avail(ctx->cb);
			if(ctx->flags & WRITE_THREAD_FLUSHING) {
				/* Get all available data in flushing state */
				data_size = ctx->io_block_size;
				if(buf_avail < data_size)
					data_size = (size_t)buf_avail;
			} else if(ctx->flags & WRITE_THREAD_BUFFERING) {
				/* In buffering state, buffer threshold should be set to full buffer */
				assert((ctx->flags & IO_THREAD_SUSTAIN) && (buf_avail >= ctx->thres_buf_debuf));
				/* Set threshold to zero to allow detect buffer underrun */
				bigbuf_set_thres_read(ctx->cb, 0);
				data_size = ctx->io_block_size;
				ctx->flags &= ~WRITE_THREAD_BUFFERING;
			} else {
				/* Check for full block available */
				if(buf_avail >= ctx->io_block_size) {
					data_size = ctx->io_block_size;
				} else {
					/* Zero buffer threshold can be set only in sustain mode */
					assert(ctx->flags & IO_THREAD_SUSTAIN);
					/* Enter buffering state again */
					bigbuf_set_thres_read(ctx->cb, ctx->thres_buf_debuf);
					ctx->flags |= WRITE_THREAD_BUFFERING;
					data_size = 0;
				}
			}

			/* Check for data available */
			if(data_size != 0)
			{
				DWORD cb_wr, error;
				size_t padded_size;

				/* Read data from buffer */
				if(!bigbuf_read(ctx->cb, ctx->io_buf, data_size, &error)) {
					ctx->error = error;
					break;
				}

				/* Add padding */
				padded_size = data_size;
				if((ctx->io_block_align > 1) && (data_size < ctx->io_block_size)) {
					padded_size = ((data_size + ctx->io_block_align - 1) / 
						ctx->io_block_align) * ctx->io_block_align;
					memset(ctx->io_buf + data_size, 0, padded_size - data_size);
				}

				/* Write to file */
				if(!WriteFile(ctx->h_file, ctx->io_buf, (DWORD)padded_size, &cb_wr, NULL))
					ctx->error = GetLastError();

				if(cb_wr < data_size)
					data_size = cb_wr;

				/* Update length and CRC32 of written data */
				EnterCriticalSection(&(ctx->total_bytes_lock));
				ctx->data_io_bytes += data_size;
				ctx->padded_io_bytes += cb_wr;
				LeaveCriticalSection(&(ctx->total_bytes_lock));
				crc32_thread_write(&(ctx->crc_thrd), ctx->io_buf, data_size);
			}

			/* Check for end of data or error */
			if( ((ctx->flags & WRITE_THREAD_FLUSHING) && (data_size < ctx->io_block_size)) ||
				(ctx->error != NO_ERROR) )
			{
				break;
			}
		}
	}
	
	return ctx->error;
}

/* Sync data read thread */
static unsigned int __stdcall read_thread_sync(struct file_thread_ctx *ctx)
{
	/* Select events to handle */
	HANDLE ev_arr[SYNC_EV_COUNT];
	ev_arr[SYNC_EV_ID_ABORT] = ctx->h_ev_abort;
	ev_arr[SYNC_EV_ID_FLUSH] = ctx->h_ev_flush;
	ev_arr[SYNC_EV_ID_BUFFER] = ctx->cb->thres_wr_ev;

	/* Set buffer threshold */
	if(ctx->flags & IO_THREAD_SUSTAIN) {
		bigbuf_set_thres_write(ctx->cb, 0);
	} else {
		bigbuf_set_thres_write(ctx->cb, ctx->io_block_size);
	}

	for(;;)
	{
		/* Wait for events */
		DWORD event_id = WaitForMultipleObjects(SYNC_EV_COUNT, ev_arr, FALSE, INFINITE);
		if(event_id >= SYNC_EV_COUNT) {
			ctx->error = GetLastError();
			break;
		}

		/* Handle abort command */
		if((event_id == SYNC_EV_ID_ABORT) || (event_id == SYNC_EV_ID_FLUSH))
		{
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* Handle buffer writeable */
		if(event_id == SYNC_EV_ID_BUFFER)
		{
			unsigned __int64 buf_free;
			int can_read = 1;

			/* Get buffer free space */
			buf_free = bigbuf_free_space(ctx->cb);

			/* Check for debuffering completion */
			if(ctx->flags & READ_THREAD_DEBUFFERING) {
				/* Buffer threshold should be set to empty buffer in debuffering state. */
				assert((ctx->flags & IO_THREAD_SUSTAIN) && (buf_free >= ctx->thres_buf_debuf));
				/* Exit debuffering state */
				bigbuf_set_thres_write(ctx->cb, 0);
				ctx->flags &= ~READ_THREAD_DEBUFFERING;
			} 

			/* Check for full block of free space available */
			if(buf_free < ctx->io_block_size) {
				/* Zero buffer threshold can be set only in sustain mode */
				assert(ctx->flags & IO_THREAD_SUSTAIN);
				/* Enter debuffering state */
				bigbuf_set_thres_write(ctx->cb, ctx->thres_buf_debuf);
				ctx->flags |= READ_THREAD_DEBUFFERING;
				can_read = 0;
			}

			if(can_read)
			{
				DWORD cb_rd;

				/* Read data from file */
				if(!ReadFile(ctx->h_file, ctx->io_buf, (DWORD)(ctx->io_block_size), &cb_rd, NULL))
					ctx->error = GetLastError();

				if(cb_rd > 0)
				{
					DWORD error;

					/* Update length and CRC32 of read data */
					EnterCriticalSection(&(ctx->total_bytes_lock));
					ctx->data_io_bytes += cb_rd;
					ctx->padded_io_bytes += cb_rd;
					LeaveCriticalSection(&(ctx->total_bytes_lock));
					crc32_thread_write(&(ctx->crc_thrd), ctx->io_buf, cb_rd);

					/* Write data to buffer */
					if(!bigbuf_write(ctx->cb, ctx->io_buf, cb_rd, &error)) {
						ctx->error = error;
						break;
					}
				}

				/* Check for end of file */
				if((cb_rd < ctx->io_block_size) || (ctx->error != NO_ERROR))
					break;
			}
		}
	}

	return ctx->error;
}

/* ---------------------------------------------------------------------------------------------- */

/* Async read/write thread */

enum {
	ASYNC_EV_ID_ABORT,
	ASYNC_EV_ID_FLUSH,
	ASYNC_EV_ID_BUFFER,
	ASYNC_EV_ID_COMPLETE,

	ASYNC_EV_COUNT
};

/* Get i-th queue entry from offset */
static struct io_queue_entry *get_entry(struct file_thread_ctx *ctx, size_t i)
{
	size_t pos;

	pos = ctx->queue_offset + i;
	if(pos >= ctx->queue_size)
		pos -= ctx->queue_size;
	return ctx->queue_entry + pos;
}

/* 
 * Async file writing thread
 * 
 * |<----------------------- queue_nused -------------->|
 * |<---- queue_npend ----->|                           |
 * |                        |                           |
 * +------------------------+---------------------------+---------------+
 * | write pending entries  | filled, unpending entries |     unused    |
 * +------------------------+---------------------------+---------------+
 * |                                                                    |
 * |<--------------------------- queue_size --------------------------->|
 * ^-- queue_offset
 *
 * Normal state              : threshold = full block available
 * Buffering, queue not full : same as normal, not writing to file
 * Buffering, queue full     : threshold = buffer full
 * Flushing buffer           : threshold disabled, can't enter buffering state
 * Flushing done/end of data : no buffer event, can't enter buffering state
 * Write error/queue flsuhing: same as for end of data
 */

static unsigned int __stdcall write_thread_async(struct file_thread_ctx *ctx)
{
	/* Set avail data threshold to full block. Need full blocks unless
	 * flushing all remaining data from buffer (no threshold)
	 * or in buffering state with queue full (full buffer threshold). */
	bigbuf_set_thres_read(ctx->cb, ctx->io_block_size);
	if(ctx->flags & IO_THREAD_SUSTAIN)
		ctx->flags |= WRITE_THREAD_BUFFERING;

	for(;;)
	{
		HANDLE ev_arr[ASYNC_EV_COUNT];
		DWORD ev_ids[ASYNC_EV_COUNT];
		DWORD result, ev_cnt, event_id;

		/* ---------------------------------- */
		/* Select events to handle */

		/* Handle abort command always */
		ev_arr[0] = ctx->h_ev_abort;
		ev_ids[0] = ASYNC_EV_ID_ABORT;
		ev_cnt = 1;

		/* Handle flush command unless already in flush state */
		if(!(ctx->flags & WRITE_THREAD_FLUSHING)) {
			ev_arr[ev_cnt] = ctx->h_ev_flush;
			ev_ids[ev_cnt] = ASYNC_EV_ID_FLUSH;
			ev_cnt++;
		}

		/* Read new entries from buffer (if have some unused queue, disabled after flushing done)
		 * Check for buffering completion (in buffering state with queue full) */
		if( ((ctx->queue_nused < ctx->queue_size) && !(ctx->flags & WRITE_THREAD_END_OF_DATA)) ||
				(ctx->flags & WRITE_THREAD_BUFFERING) ) {
			ev_arr[ev_cnt] = ctx->cb->thres_rd_ev;
			ev_ids[ev_cnt] = ASYNC_EV_ID_BUFFER;
			ev_cnt++;
		}

		/* Handle pending write completion */
		if(ctx->queue_npend > 0) {
			struct io_queue_entry *first_pending;
			first_pending = get_entry(ctx, 0);
			ev_arr[ev_cnt] = first_pending->ov.hEvent;
			ev_ids[ev_cnt] = ASYNC_EV_ID_COMPLETE;
			ev_cnt++;
		}

		/* Wait for selected events */
		result = WaitForMultipleObjects(ev_cnt, ev_arr, FALSE, INFINITE);
		if(result >= ev_cnt) {
			ctx->error = GetLastError();
			break;
		}
		event_id = ev_ids[result];

		/* ---------------------------------- */
		/* Handle abort command */

		if(event_id == ASYNC_EV_ID_ABORT)
		{
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* ---------------------------------- */
		/* Handle start flushing command */

		if(event_id == ASYNC_EV_ID_FLUSH)
		{
			assert(!(ctx->flags & WRITE_THREAD_FLUSHING));
			/* Exit buffering state and disable avail data threshold
			 * to retrieve remaining data from buffer */
			ctx->flags |= WRITE_THREAD_FLUSHING;
			ctx->flags &= ~WRITE_THREAD_BUFFERING;
			bigbuf_set_thres_read(ctx->cb, 0);
		}

		/* ---------------------------------- */
		/* Handle buffer readable / buffering complete */

		if(event_id == ASYNC_EV_ID_BUFFER)
		{
			unsigned __int64 buf_avail;

			/* Can't be here after the end of source data */
			assert(!(ctx->flags & WRITE_THREAD_END_OF_DATA));

			/* Get size of buffered data */
			buf_avail = bigbuf_data_avail(ctx->cb);

			/* Check for buffering completion */
			if((ctx->flags & WRITE_THREAD_BUFFERING) && (ctx->queue_nused == ctx->queue_size)) {
				/* In buffering state with queue full, threshold must be set to full buffer */
				assert((buf_avail >= ctx->thres_buf_debuf) && !(ctx->flags & WRITE_THREAD_FLUSHING));
				/* Exit buffering state and reset threshold to full block */
				ctx->flags &= ~WRITE_THREAD_BUFFERING;
				bigbuf_set_thres_read(ctx->cb, ctx->io_block_size);
			}

			/* Refill queue from buffer */
			if(ctx->queue_nused < ctx->queue_size)
			{
				size_t data_size;

				/* Full block should be available unless flushing remaining data */
				data_size = ctx->io_block_size;
				if(buf_avail < ctx->io_block_size) {
					assert(ctx->flags & WRITE_THREAD_FLUSHING);
					data_size = (size_t)buf_avail;
				}

				/* Don't add entry if no more data available */
				if(data_size > 0)
				{
					DWORD error;
					size_t padded_size;
					struct io_queue_entry *entry;

					/* Get first unused entry from queue */
					entry = get_entry(ctx, ctx->queue_nused);

					/* Fill entry from buffer */
					if(!bigbuf_read(ctx->cb, entry->buf, data_size, &error)) {
						ctx->error = error;
						break;
					}

					/* Pad last block with zeroes */
					padded_size = data_size;
					if((ctx->io_block_align > 1) && (data_size < ctx->io_block_size)) {
						padded_size = ((data_size + ctx->io_block_align - 1) / 
							ctx->io_block_align) * ctx->io_block_align;
						memset(entry->buf + data_size, 0, padded_size - data_size);
					}

					/* Initialize entry fields and mark entry as ready to write */
					entry->ov.Offset = (DWORD)(ctx->queue_data_pos);
					entry->ov.OffsetHigh = (DWORD)(ctx->queue_data_pos >> 32);
					entry->data_size = data_size;
					entry->padded_size = padded_size;
					ctx->queue_nused++;

					/* Move queue data counter */
					ctx->queue_data_pos += padded_size;
				}

				/* Set buffering threshold after queue full (buffering state) */
				if((ctx->flags & WRITE_THREAD_BUFFERING) && (ctx->queue_nused == ctx->queue_size))
					bigbuf_set_thres_read(ctx->cb, ctx->thres_buf_debuf);

				/* Check for end of data (should be in flushing state) */
				if(data_size < ctx->io_block_size)
					ctx->flags |= WRITE_THREAD_END_OF_DATA;
			}
		}

		/* ---------------------------------- */
		/* Handle write complete */

		if(event_id == ASYNC_EV_ID_COMPLETE)
		{
			struct io_queue_entry *entry;
			DWORD error, cb_written;
			size_t cb_data;

			/* Can't be here without pending entries */
			assert(ctx->queue_npend > 0);

			/* Get first pending entry from queue which should be complete now */
			entry = get_entry(ctx, 0);

			/* Check operation result */
			if(entry->is_async) {
				error = NO_ERROR;
				if(!GetOverlappedResult(ctx->h_file, &(entry->ov), &cb_written, FALSE))
					error = GetLastError();
				if((cb_written < entry->padded_size) && (error == NO_ERROR))
					error = ERROR_HANDLE_DISK_FULL;
			} else {
				error = (DWORD) entry->ov.Internal;
				cb_written = (DWORD) entry->ov.InternalHigh;
			}

			/* Check for incomplete written data (not padded data) */
			cb_data = entry->data_size;
			if(cb_written < cb_data)
				cb_data = cb_written;

			/* Update byte counter and CRC32 of written data */
			if(cb_written > 0) {
				EnterCriticalSection(&(ctx->total_bytes_lock));
				ctx->data_io_bytes += cb_data;
				ctx->padded_io_bytes += cb_written;
				LeaveCriticalSection(&(ctx->total_bytes_lock));
				crc32_thread_write(&(ctx->crc_thrd), entry->buf, cb_data);
			}

			/* Check for write error (kill unpending entries and handle as end of data) */
			if((error != NO_ERROR) && (ctx->error == NO_ERROR)) {
				ctx->queue_nused = ctx->queue_npend;
				ctx->flags |= WRITE_THREAD_FLUSHING|WRITE_THREAD_END_OF_DATA;
				ctx->flags &= ~WRITE_THREAD_BUFFERING;
				ctx->error = error;
			}

			/* Remove completed entry from queue */
			ctx->queue_offset++;
			if(ctx->queue_offset == ctx->queue_size)
				ctx->queue_offset = 0;
			ctx->queue_npend--;
			ctx->queue_nused--;

			/* Reenable writing after some operation completes */
			ctx->flags &= ~WRITE_THREAD_DRIVER_CONGESTION;
		}

		/* ---------------------------------- */
		/* Initiate write operations when ... */

		if( !(ctx->flags & WRITE_THREAD_DRIVER_CONGESTION) &&	/* ... writing allowed, and ... */
			!(ctx->flags & WRITE_THREAD_BUFFERING) )			/* ... not in buffering state. */
		{
			/* Enter buffering state when have no prepared entries to write nor pending entries */
			if( (ctx->queue_nused == 0) && !(ctx->flags & WRITE_THREAD_FLUSHING) &&
					(ctx->flags & IO_THREAD_SUSTAIN) )
			{
				ctx->flags |= WRITE_THREAD_BUFFERING;
			}

			/* Initiate write operation for prepared entries */
			while(ctx->queue_npend < ctx->queue_nused)
			{
				DWORD cb_written, error;
				struct io_queue_entry *entry;

				/* Get first filled unpending entry */
				entry = get_entry(ctx, ctx->queue_npend);
				ResetEvent(entry->ov.hEvent);

				/* Start writing to file */
				error = NO_ERROR;
				if(!WriteFile(ctx->h_file, entry->buf,
					(DWORD)(entry->padded_size), &cb_written, &(entry->ov)))
				{
					error = GetLastError();
				}
				assert((cb_written == entry->padded_size) || (error != NO_ERROR));

				/* Check status */
				if(error == ERROR_IO_PENDING)
				{
					/* Async success: add entry to queue */
					entry->is_async = 1;
					ctx->queue_npend++;
				}
				else if( (ctx->queue_npend >= 1) &&
					((error == ERROR_INVALID_USER_BUFFER) || (error == ERROR_NOT_ENOUGH_MEMORY)) )
				{
					/* If driver can't process more requests,
					 * disable writing until some request completes 
					 * (handle as error if no pending requests) */
					ctx->flags |= WRITE_THREAD_DRIVER_CONGESTION;
					break;
				}
				else
				{
					/* Sync success or error (some data still can be written) */

					if((cb_written < entry->padded_size) && (error == NO_ERROR))
						error = ERROR_HANDLE_DISK_FULL;

					/* Just add to queue and deal as with async completion */
					SetEvent(entry->ov.hEvent);
					entry->ov.Internal = error;
					entry->ov.InternalHigh = cb_written;
					entry->is_async = 0;
					ctx->queue_npend++;
					
					/* Kill unpending entries and stop writing on error */
					if(error != NO_ERROR) {
						ctx->queue_nused = ctx->queue_npend;
						ctx->flags |= WRITE_THREAD_FLUSHING|WRITE_THREAD_END_OF_DATA;
						ctx->flags &= ~WRITE_THREAD_BUFFERING;
						break;
					}
				}
			}
		}

		/* ---------------------------------- */
		/* Exit after buffer flushing completed and queue empty */

		if((ctx->flags & WRITE_THREAD_END_OF_DATA) && (ctx->queue_nused == 0))
			break;
	}

	/* Cancel pending requests on error/abort */
	if(ctx->queue_npend > 0)
		CancelIo(ctx->h_file);

	return ctx->error;
}

/* 
 * Async file reading thread
 * 
 * |<---------------------- queue_nused ------------->|
 * |                        |<----- queue_npend ----->|
 * |                        |                         |
 * +------------------------+-------------------------+---------------+
 * | read completed entries | read pending entries    |     unused    |
 * +------------------------+-------------------------+---------------+
 * |                                                                  |
 * |<--------------------------- queue_size ------------------------->|
 * ^-- queue_offset
 *
 * Startup, queue empty         : threshold disabled
 * Normal, buffer not full      : threshold disabled
 * Normal, buffer full          : threshold = size of first completed entry
 * Debuffering, queue not empty : as in normal states
 * Debuffering, queue empty     : threshold = empty buffer
 */

static unsigned int __stdcall read_thread_async(struct file_thread_ctx *ctx)
{
	/* Disable free space threshold for startup */
	bigbuf_set_thres_write(ctx->cb, 0);

	for(;;)
	{
		HANDLE ev_arr[ASYNC_EV_COUNT];
		DWORD ev_ids[ASYNC_EV_COUNT];
		DWORD result, ev_cnt, event_id;

		/* ---------------------------------- */
		/* Select events to handle */

		/* Handle abort command always */
		ev_arr[0] = ctx->h_ev_abort;
		ev_ids[0] = ASYNC_EV_ID_ABORT;
		ev_cnt = 1;

		/* Write completed entries to buffer
		 * Check for debuffering completion (queue empty and threshold set to empty buffer)
		 * Fall through to queue filling (startup, queue empty and no threshold) */
		if((ctx->queue_nused > ctx->queue_npend) || (ctx->queue_nused == 0)) {
			ev_arr[ev_cnt] = ctx->cb->thres_wr_ev;
			ev_ids[ev_cnt] = ASYNC_EV_ID_BUFFER;
			ev_cnt++;
		}

		/* Handle pending read completion */
		if(ctx->queue_npend > 0) {
			struct io_queue_entry *first_pending;
			first_pending = get_entry(ctx, ctx->queue_nused - ctx->queue_npend);
			ev_arr[ev_cnt] = first_pending->ov.hEvent;
			ev_ids[ev_cnt] = ASYNC_EV_ID_COMPLETE;
			ev_cnt++;
		}

		/* Wait for selected events */
		result = WaitForMultipleObjects(ev_cnt, ev_arr, FALSE, INFINITE);
		if(result >= ev_cnt) {
			ctx->error = GetLastError();
			break;
		}
		event_id = ev_ids[result];

		/* ---------------------------------- */
		/* Handle abort command */

		if(event_id == ASYNC_EV_ID_ABORT)
		{
			ctx->error = ERROR_OPERATION_ABORTED;
			break;
		}

		/* ---------------------------------- */
		/* Handle buffer writeable / debuffering complete / queue startup */

		if(event_id == ASYNC_EV_ID_BUFFER)
		{
			unsigned __int64 buf_free, thres_write;
			
			/* Get available space size in buffer and set threshold to zero for now */
			buf_free = bigbuf_free_space(ctx->cb);
			thres_write = 0;

			/* Should have some completed entries, debuffering completion or startup state */
			assert( (ctx->queue_nused > ctx->queue_npend) ||
				((ctx->flags & READ_THREAD_DEBUFFERING) && (buf_free >= ctx->thres_buf_debuf)) ||
				((ctx->queue_nused == 0) && (ctx->data_io_bytes == 0)) );

			/* Check for debuffering completion */
			if((ctx->flags & READ_THREAD_DEBUFFERING) && (ctx->queue_nused == 0))
				ctx->flags &= ~READ_THREAD_DEBUFFERING;

			/* Write completed entries to buffer */
			if(ctx->queue_nused > ctx->queue_npend)
			{
				/* Get first completed entry from queue */
				struct io_queue_entry *entry = get_entry(ctx, 0);
				/* Check for buffer overrun */
				if(buf_free < entry->data_size) {
					/* Set buffer threshold to size of current entry
					 * and enter debuffering state if sustain mode enabled. */
					thres_write = entry->data_size;
					if(ctx->flags & IO_THREAD_SUSTAIN)
						ctx->flags |= READ_THREAD_DEBUFFERING;
				} else {
					/* Copy data to buffer */
					DWORD error;
					if(!bigbuf_write(ctx->cb, entry->buf, entry->data_size, &error)) {
						ctx->error = error;
						break;
					}
					/* Remove completed entry from queue */
					ctx->queue_offset++;
					if(ctx->queue_offset == ctx->queue_size)
						ctx->queue_offset = 0;
					ctx->queue_nused--;
				}

				/* Set debuffering thrshold after queue empty (debuffering state) */
				if((ctx->flags & READ_THREAD_DEBUFFERING) && (ctx->queue_nused == 0))
					thres_write = ctx->thres_buf_debuf;
			}

			/* Update buffer threshold for next event */
			bigbuf_set_thres_write(ctx->cb, thres_write);
		}

		/* ---------------------------------- */
		/* Handle reading complete */

		if(event_id == ASYNC_EV_ID_COMPLETE)
		{
			struct io_queue_entry *entry;
			DWORD error, cb_read;

			/* Can't be here without pending entries */
			assert((ctx->queue_nused >= ctx->queue_npend) && (ctx->queue_npend > 0));

			/* Get first pending entry from queue which should be complete now */
			entry = get_entry(ctx, ctx->queue_nused - ctx->queue_npend);
			if(entry->is_async) {
				error = NO_ERROR;
				if(!GetOverlappedResult(ctx->h_file, &(entry->ov), &cb_read, FALSE))
					error = GetLastError();
				if((cb_read < ctx->io_block_size) && (error == NO_ERROR))
					error = ERROR_HANDLE_EOF;
			} else {
				error = (DWORD) entry->ov.Internal;
				cb_read = (DWORD) entry->ov.InternalHigh;
			}
			assert((cb_read == ctx->io_block_size) || (error != NO_ERROR));

			/* Update byte counter and CRC32 of read data */
			if(cb_read > 0) {
				EnterCriticalSection(&(ctx->total_bytes_lock));
				ctx->data_io_bytes += cb_read;
				ctx->padded_io_bytes += cb_read;
				LeaveCriticalSection(&(ctx->total_bytes_lock));
				crc32_thread_write(&(ctx->crc_thrd), entry->buf, cb_read);
			}

			/* Check for EOF or read error */
			if((error != NO_ERROR) && (ctx->error == NO_ERROR)) {
				ctx->flags |= READ_THREAD_END_OF_FILE;
				ctx->error = error;
			}

			/* Move entry to completed part of queue */
			entry->data_size = cb_read;
			ctx->queue_npend--;

			/* Reenable reading after some operation completes */
			ctx->flags &= ~READ_THREAD_DRIVER_CONGESTION;
		}

		/* ---------------------------------- */
		/* Initiate read operations when ... */

		if( !(ctx->flags & READ_THREAD_DRIVER_CONGESTION) &&	/* ... read allowed, and ... */
			!(ctx->flags & READ_THREAD_END_OF_FILE) &&			/* ... not at EOF, and ... */
			!(ctx->flags & READ_THREAD_DEBUFFERING) )			/* ... not in debuffering state. */
		{
			/* Initiate read operations for free queue entries */
			while(ctx->queue_nused < ctx->queue_size)
			{
				struct io_queue_entry *entry;
				DWORD cb_read, error;

				/* Get and initialize first free entry from queue */
				entry = get_entry(ctx, ctx->queue_nused);
				entry->ov.Offset = (DWORD)(ctx->queue_data_pos);
				entry->ov.OffsetHigh = (DWORD)(ctx->queue_data_pos >> 32);
				ctx->queue_data_pos += ctx->io_block_size;

				ResetEvent(entry->ov.hEvent);

				/* Start read operation */
				error = NO_ERROR;
				if(!ReadFile(ctx->h_file, entry->buf,
					(DWORD)(ctx->io_block_size), &cb_read, &(entry->ov)))
				{
					error = GetLastError();
				}
				
				if(error == ERROR_IO_PENDING)
				{
					/* Async success: add entry to queue */
					entry->is_async = 1;
					ctx->queue_nused++;
					ctx->queue_npend++;
				}
				else if( (ctx->queue_npend >= 1) &&
					((error == ERROR_INVALID_USER_BUFFER) || (error == ERROR_NOT_ENOUGH_MEMORY)) )
				{
					/* If driver can't process the request,
					 * disable writing until some operation completes 
					 * (handle as error if driver can't take one operation) */
					ctx->flags |= READ_THREAD_DRIVER_CONGESTION;
					break;
				}
				else
				{
					/* Sync success or error (some data still can be read) */

					if((cb_read < ctx->io_block_size) && (error == NO_ERROR))
						error = ERROR_HANDLE_EOF;

					/* Just add to queue and deal as with async completion */
					SetEvent(entry->ov.hEvent);
					entry->ov.Internal = error;
					entry->ov.InternalHigh = cb_read;
					entry->is_async = 0;
					ctx->queue_nused++;
					ctx->queue_npend++;

					/* Stop reading on error or EOF */
					if(error != NO_ERROR) {
						ctx->flags |= READ_THREAD_END_OF_FILE;
						break;
					}
				}
			}
		}

		/* ---------------------------------- */
		/* Exit after EOF reached and queue flushed to buffer */

		if((ctx->flags & READ_THREAD_END_OF_FILE) && (ctx->queue_nused == 0))
			break;
	}

	/* Cancel pending requests on error/abort */
	if(ctx->queue_npend > 0)
		CancelIo(ctx->h_file);

	return ctx->error;
}

/* ---------------------------------------------------------------------------------------------- */

void file_thread_abort(struct file_thread_ctx *ctx)
{
	SetEvent(ctx->h_ev_abort);
	if(WaitForSingleObject(ctx->h_thread, IO_THREAD_ABORT_TIMEOUT) == WAIT_TIMEOUT)
		TerminateThread(ctx->h_thread, 0);
	if(ctx->error == NO_ERROR)
		ctx->error = ERROR_OPERATION_ABORTED;
}

/* ---------------------------------------------------------------------------------------------- */

void file_thread_flush(struct file_thread_ctx *ctx)
{
	SetEvent(ctx->h_ev_flush);
}

/* ---------------------------------------------------------------------------------------------- */

void file_thread_get_total_bytes(
	struct file_thread_ctx *ctx,
	unsigned __int64 *p_data_io_bytes,
	unsigned __int64 *p_padded_io_bytes)
{
	EnterCriticalSection(&(ctx->total_bytes_lock));
	if(p_data_io_bytes != NULL)
		*p_data_io_bytes = ctx->data_io_bytes;
	if(p_padded_io_bytes != NULL)
		*p_padded_io_bytes = ctx->padded_io_bytes;
	LeaveCriticalSection(&(ctx->total_bytes_lock));
}

/* ---------------------------------------------------------------------------------------------- */

/* spawn file I/O thread */
int file_thread_start(struct file_thread_ctx *ctx, struct big_buffer *cb,
	HANDLE h_file, unsigned int flags, unsigned __int64 thres_buf_debuf,
	size_t io_block_size, size_t io_block_align, size_t queue_size,
	size_t crc_buffer_size, size_t crc_block_size)
{
	if( ! ((flags & IO_THREAD_MODE_READ) && !(flags & IO_THREAD_MODE_WRITE)) &&
		! ((flags & IO_THREAD_MODE_WRITE) && !(flags & IO_THREAD_MODE_READ)) )
	{
		return 0;
	}

	/* Check I/O block alignment */
	if((io_block_align > 1) && (io_block_size % io_block_align != 0))
		return 0;
	
	/* Check buffering threshold */
	if((flags & IO_THREAD_SUSTAIN) && (thres_buf_debuf < io_block_size))
		return 0;

	/* Initialize context */

	ctx->cb = cb;
	ctx->h_file = h_file;
	
	ctx->flags = flags;
	ctx->thres_buf_debuf = thres_buf_debuf;
	ctx->io_block_size = io_block_size;
	ctx->io_block_align = io_block_align;
	
	ctx->queue_size = queue_size;
	ctx->queue_offset = 0;
	ctx->queue_nused = 0;
	ctx->queue_npend = 0;
	ctx->queue_entry = NULL;
	ctx->queue_data_pos = 0;

	ctx->io_buf = NULL;

	ctx->data_io_bytes = 0;
	ctx->padded_io_bytes = 0;
	ctx->data_crc = 0;
	ctx->padded_crc = 0;
	ctx->error = NO_ERROR;

	/* Spawn CRC thread */
	if( ! crc32_thread_init(&(ctx->crc_thrd), crc_buffer_size, crc_block_size,
		(flags & IO_THREAD_SUSTAIN) ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL) )
	{
		return 0;
	}

	InitializeCriticalSection(&(ctx->total_bytes_lock));

	/* Create events */
	ctx->h_ev_abort = CreateEvent(NULL, TRUE, FALSE, NULL);
	ctx->h_ev_flush = CreateEvent(NULL, TRUE, FALSE, NULL);
	ctx->h_thread = INVALID_HANDLE_VALUE;

	if((ctx->h_ev_abort == NULL) || (ctx->h_ev_flush == NULL))
		goto error_cleanup;

	if(queue_size == 0)
	{
		unsigned int thread_id;

		/* Allocate data buffer */
		ctx->io_buf = _aligned_malloc(io_block_size, 4096);
		if(ctx->io_buf == NULL)
			goto error_cleanup;

		/* Spawn thread */
		if(flags & IO_THREAD_MODE_WRITE)
		{
			ctx->h_thread = (HANDLE) _beginthreadex(NULL, 0,
				write_thread_sync, ctx, 0, &thread_id);
		}
		if(flags & IO_THREAD_MODE_READ)
		{
			ctx->h_thread = (HANDLE) _beginthreadex(NULL, 0,
				read_thread_sync, ctx, 0, &thread_id);
		}
	}
	else
	{
		unsigned int i, thread_id;

		/* Allocate I/O queue */
		ctx->queue_entry = calloc(queue_size, sizeof(struct io_queue_entry));
		if(ctx->queue_entry == NULL)
			goto error_cleanup;
		for(i = 0; i < queue_size; i++)
		{
			ctx->queue_entry[i].ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			ctx->queue_entry[i].buf = _aligned_malloc(io_block_size, 4096);
			if((ctx->queue_entry[i].ov.hEvent == NULL) || (ctx->queue_entry[i].buf == NULL))
				goto error_cleanup;
		}

		/* Spawn thread */
		if(flags & IO_THREAD_MODE_WRITE)
		{
			ctx->h_thread = (HANDLE) _beginthreadex(NULL, 0, 
				write_thread_async, ctx, 0, &thread_id);
		}
		if(flags & IO_THREAD_MODE_READ)
		{
			ctx->h_thread = (HANDLE) _beginthreadex(NULL, 0,
				read_thread_async, ctx, 0, &thread_id);
		}
	}

	if(ctx->h_thread == INVALID_HANDLE_VALUE)
		goto error_cleanup;

	if(flags & IO_THREAD_SUSTAIN)
		SetThreadPriority(ctx->h_thread, THREAD_PRIORITY_ABOVE_NORMAL);

	return 1;

error_cleanup:

	/* Destroy I/O queue */
	if(ctx->queue_entry != NULL)
	{
		unsigned int i;
		for(i = 0; i < queue_size; i++)
		{
			if(ctx->queue_entry[i].ov.hEvent != NULL)
				CloseHandle(ctx->queue_entry[i].ov.hEvent);
			if(ctx->queue_entry[i].buf != NULL)
				_aligned_free(ctx->queue_entry[i].buf);
		}
		free(ctx->queue_entry);
	}

	/* Free data buffer */
	if(ctx->io_buf != NULL)
		_aligned_free(ctx->io_buf);

	/* Delete events */
	if(ctx->h_ev_flush != NULL)
		CloseHandle(ctx->h_ev_flush);
	if(ctx->h_ev_abort != NULL)
		CloseHandle(ctx->h_ev_abort);
	DeleteCriticalSection(&(ctx->total_bytes_lock));

	/* End CRC thread */
	crc32_thread_finish(&(ctx->crc_thrd));

	return 0;
}

/* ---------------------------------------------------------------------------------------------- */

/* wait for I/O thread exit and cleanup */
void file_thread_finish(struct file_thread_ctx *ctx)
{
//	WaitForSingleObject(ctx->h_thread, INFINITE);

	/* Free I/O queue */
	if(ctx->queue_entry != NULL)
	{
		unsigned int i;
		for(i = 0; i < ctx->queue_size; i++) {
			CloseHandle(ctx->queue_entry[i].ov.hEvent);
			_aligned_free(ctx->queue_entry[i].buf);
		}
		free(ctx->queue_entry);
	}

	/* Free I/O buffer */
	if(ctx->io_buf != NULL)
		_aligned_free(ctx->io_buf);

	/* Close events and thread */
	CloseHandle(ctx->h_ev_flush);
	CloseHandle(ctx->h_ev_abort);
	CloseHandle(ctx->h_thread);

	/* End CRC thread */
	ctx->padded_crc = ctx->data_crc = 
		crc32_thread_finish(&(ctx->crc_thrd));

	/* Update CRC of padded data */
	if(ctx->padded_io_bytes > ctx->data_io_bytes)
	{
		size_t bs, rem;
		unsigned char zero[256];
		memset(zero, 0, sizeof(zero));
		for(rem = (size_t)(ctx->padded_io_bytes - ctx->data_io_bytes); rem != 0; rem -= bs) {
			bs = (rem <= 256) ? rem : 256;
			ctx->padded_crc = crc32_update(ctx->padded_crc, zero, bs);
		}
	}

	DeleteCriticalSection(&(ctx->total_bytes_lock));
}

/* ---------------------------------------------------------------------------------------------- */
