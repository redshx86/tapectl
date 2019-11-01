/* ---------------------------------------------------------------------------------------------- */

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <crtdbg.h>
#include "../util/fmt.h"
#include "bigbuff.h"

/* ---------------------------------------------------------------------------------------------- */

/* Write data to buffer. Buffer must have enough free space. */
int bigbuf_write(struct big_buffer *ctx, const void *src, size_t length, DWORD *p_err)
{
	const BYTE *src_ptr;
	unsigned __int64 buf_free, wr_pos;
	size_t remain, block_size;

	if(length == 0) {
		*p_err = NO_ERROR;
		return 1;
	}

	/* Get buffer pointers */
	EnterCriticalSection(&(ctx->buf_ptr_lock));
	buf_free = ctx->buf_size - ctx->buf_data_length;
	wr_pos = ctx->buf_data_offset + ctx->buf_data_length;
	if(wr_pos >= ctx->buf_size)
		wr_pos -= ctx->buf_size;
	LeaveCriticalSection(&(ctx->buf_ptr_lock));

	if(length > buf_free)
	{
		*p_err = ERROR_INVALID_PARAMETER;
		return 0;
	}

	src_ptr = src;

	/* Write data to buffer */
	for(remain = length; remain != 0U; remain -= block_size)
	{
		if(ctx->buf_addr != NULL)
		{
			/* Calculate size of block to write */
			block_size = (size_t)(ctx->buf_size - wr_pos);
			if(remain < block_size)
				block_size = remain;

			/* Write data to buffer */
			memcpy(ctx->buf_addr + wr_pos, src_ptr, block_size);
		}
		else
		{
			ULONG_PTR win_map_pos;
			size_t win_offset;
			HANDLE events[3];

			/* Calculate window pointers and block size */
			win_map_pos = (ULONG_PTR)((wr_pos / ctx->win_size) * ctx->win_page_cnt);
			win_offset = (size_t)(wr_pos - (unsigned __int64)win_map_pos * ctx->page_size);
			block_size = ctx->win_size - win_offset;
			if(remain < block_size)
				block_size = remain;

			/* Write block to buffer through mapped window */
			events[0] = ctx->win_a_wr_ev;
			events[1] = ctx->win_b_wr_ev;
			WaitForMultipleObjects(2, events, TRUE, INFINITE);
			for(;;)
			{
				/* Try writing through window A */
				if(win_map_pos == ctx->win_a_map_pos)
				{
					SetEvent(ctx->win_b_wr_ev);
					memcpy(ctx->win_a_addr + win_offset, src_ptr, block_size);
					SetEvent(ctx->win_a_wr_ev);
					break;
				}
				/* Try writing through window B */
				if(win_map_pos == ctx->win_b_map_pos)
				{
					SetEvent(ctx->win_a_wr_ev);
					memcpy(ctx->win_b_addr + win_offset, src_ptr, block_size);
					SetEvent(ctx->win_b_wr_ev);
					break;
				}
				/* Lock window A */
				if(WaitForSingleObject(ctx->win_a_rd_ev, 0) != WAIT_OBJECT_0)
				{
					SetEvent(ctx->win_a_wr_ev);
					SetEvent(ctx->win_b_wr_ev);
					events[2] = ctx->win_a_rd_ev;
					WaitForMultipleObjects(3, events, TRUE, INFINITE);
				}
				/* Remap window A if needed */
				if((win_map_pos != ctx->win_a_map_pos) && (win_map_pos != ctx->win_b_map_pos))
				{
					if( !MapUserPhysicalPages(ctx->win_a_addr,
							ctx->win_page_cnt, ctx->page_pfn + win_map_pos) )
					{
						*p_err = GetLastError();
						MapUserPhysicalPages(ctx->win_a_addr, ctx->win_page_cnt, NULL);
						ctx->win_a_map_pos = BIGBUF_WINDOW_NO_MAP;
						SetEvent(ctx->win_a_wr_ev);
						SetEvent(ctx->win_b_wr_ev);
						SetEvent(ctx->win_a_rd_ev);
						return 0;
					}
					ctx->win_a_map_pos = win_map_pos;
				}
				/* Unlock window A for reading */
				SetEvent(ctx->win_a_rd_ev);
			}

		}

		/* Update write pointers */
		wr_pos += block_size;
		if(wr_pos >= ctx->buf_size)
			wr_pos -= ctx->buf_size;
		src_ptr += block_size;
	}

	/* Update buffer state */
	EnterCriticalSection(&(ctx->buf_ptr_lock));
	
	ctx->buf_data_length += length;

	if( (ctx->buf_size - ctx->buf_data_length < ctx->thres_wr_free) && 
		(ctx->thres_flags & BIGBUF_WR_THRES_FLAG) )
	{
		ResetEvent(ctx->thres_wr_ev);
		ctx->thres_flags &= ~BIGBUF_WR_THRES_FLAG;
	}

	if( (ctx->buf_data_length >= ctx->thres_rd_avail) &&
		!(ctx->thres_flags & BIGBUF_RD_THRES_FLAG) )
	{
		SetEvent(ctx->thres_rd_ev);
		ctx->thres_flags |= BIGBUF_RD_THRES_FLAG;
	}

	LeaveCriticalSection(&(ctx->buf_ptr_lock));

	*p_err = NO_ERROR;
	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Read data from buffer. Buffer must have enough available data. */

int bigbuf_read(struct big_buffer *ctx, void *dst, size_t length, DWORD *p_err)
{
	BYTE *dst_ptr;
	unsigned __int64 buf_used, rd_pos;
	size_t remain, block_size;

	if(length == 0) {
		*p_err = NO_ERROR;
		return 1;
	}

	/* Get buffer pointers */
	EnterCriticalSection(&(ctx->buf_ptr_lock));
	buf_used = ctx->buf_data_length;
	rd_pos = ctx->buf_data_offset;
	LeaveCriticalSection(&(ctx->buf_ptr_lock));

	if(length > buf_used)
	{
		*p_err = ERROR_INVALID_PARAMETER;
		return 0;
	}

	dst_ptr = dst;

	/* Read data from buffer */
	for(remain = length; remain != 0U; remain -= block_size)
	{
		if(ctx->buf_addr != NULL)
		{
			/* Calculate size of block to read */
			block_size = (size_t)(ctx->buf_size - rd_pos);
			if(remain < block_size)
				block_size = remain;

			/* Read data from buffer */
			memcpy(dst_ptr, ctx->buf_addr + rd_pos, block_size);
		}
		else
		{
			ULONG_PTR win_map_pos;
			size_t win_offset;
			HANDLE events[3];

			/* Calculate window pointers and block size */
			win_map_pos = (ULONG_PTR)((rd_pos / ctx->win_size) * ctx->win_page_cnt);
			win_offset = (size_t)(rd_pos - (unsigned __int64)win_map_pos * ctx->page_size);
			block_size = ctx->win_size - win_offset;
			if(remain < block_size)
				block_size = remain;

			/* Read block from buffer through mapped window */
			events[0] = ctx->win_b_rd_ev;
			events[1] = ctx->win_a_rd_ev;
			WaitForMultipleObjects(2, events, TRUE, INFINITE);
			for(;;)
			{
				/* Try reading through window B */
				if(win_map_pos == ctx->win_b_map_pos)
				{
					SetEvent(ctx->win_a_rd_ev);
					memcpy(dst_ptr, ctx->win_b_addr + win_offset, block_size);
					SetEvent(ctx->win_b_rd_ev);
					break;
				}
				/* Try reading through window A */
				if(win_map_pos == ctx->win_a_map_pos)
				{
					SetEvent(ctx->win_b_rd_ev);
					memcpy(dst_ptr, ctx->win_a_addr + win_offset, block_size);
					SetEvent(ctx->win_a_rd_ev);
					break;
				}
				/* Lock window B */
				if(WaitForSingleObject(ctx->win_b_wr_ev, 0) != WAIT_OBJECT_0)
				{
					SetEvent(ctx->win_b_rd_ev);
					SetEvent(ctx->win_a_rd_ev);
					events[2] = ctx->win_b_wr_ev;
					WaitForMultipleObjects(3, events, TRUE, INFINITE);
				}
				/* Remap window B if needed */
				if((win_map_pos != ctx->win_b_map_pos) && (win_map_pos != ctx->win_a_map_pos))
				{
					if( !MapUserPhysicalPages(ctx->win_b_addr,
							ctx->win_page_cnt, ctx->page_pfn + win_map_pos) )
					{
						*p_err = GetLastError();
						MapUserPhysicalPages(ctx->win_a_addr, ctx->win_page_cnt, NULL);
						ctx->win_a_map_pos = BIGBUF_WINDOW_NO_MAP;
						SetEvent(ctx->win_b_rd_ev);
						SetEvent(ctx->win_a_rd_ev);
						SetEvent(ctx->win_b_wr_ev);
						return 0;
					}
					ctx->win_b_map_pos = win_map_pos;
				}
				/* Unlock window B for writing */
				SetEvent(ctx->win_b_wr_ev);
			}
		}

		/* Update read pointers */
		rd_pos += block_size;
		if(rd_pos >= ctx->buf_size)
			rd_pos -= ctx->buf_size;
		dst_ptr += block_size;
	}

	/* Update buffer state */
	EnterCriticalSection(&(ctx->buf_ptr_lock));
	
	ctx->buf_data_offset += length;
	if(ctx->buf_data_offset >= ctx->buf_size)
		ctx->buf_data_offset -= ctx->buf_size;
	
	ctx->buf_data_length -= length;

	if( (ctx->buf_size - ctx->buf_data_length >= ctx->thres_wr_free) && 
		!(ctx->thres_flags & BIGBUF_WR_THRES_FLAG) )
	{
		SetEvent(ctx->thres_wr_ev);
		ctx->thres_flags |= BIGBUF_WR_THRES_FLAG;
	}

	if( (ctx->buf_data_length < ctx->thres_rd_avail) &&
		(ctx->thres_flags & BIGBUF_RD_THRES_FLAG) )
	{
		ResetEvent(ctx->thres_rd_ev);
		ctx->thres_flags &= ~BIGBUF_RD_THRES_FLAG;
	}
	LeaveCriticalSection(&(ctx->buf_ptr_lock));

	*p_err = NO_ERROR;
	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Set free space threshold (buffer writable event). */
void bigbuf_set_thres_write(struct big_buffer *ctx, unsigned __int64 thres_wr_free)
{
	if(thres_wr_free != ctx->thres_wr_free)
	{
		EnterCriticalSection(&(ctx->buf_ptr_lock));
		if(ctx->buf_size - ctx->buf_data_length >= thres_wr_free) {
			if(!(ctx->thres_flags & BIGBUF_WR_THRES_FLAG)) {
				SetEvent(ctx->thres_wr_ev);
				ctx->thres_flags |= BIGBUF_WR_THRES_FLAG;
			}
		} else {
			if(ctx->thres_flags & BIGBUF_WR_THRES_FLAG) {
				ResetEvent(ctx->thres_wr_ev);
				ctx->thres_flags &= ~BIGBUF_WR_THRES_FLAG;
			}
		}
		ctx->thres_wr_free = thres_wr_free;
		LeaveCriticalSection(&(ctx->buf_ptr_lock));
	}
}

/* Set available data threshold (buffer readable event). */
void bigbuf_set_thres_read(struct big_buffer *ctx, unsigned __int64 thres_rd_avail)
{
	if(thres_rd_avail != ctx->thres_rd_avail)
	{
		EnterCriticalSection(&(ctx->buf_ptr_lock));
		if(ctx->buf_data_length >= thres_rd_avail) {
			if(!(ctx->thres_flags & BIGBUF_RD_THRES_FLAG)) {
				SetEvent(ctx->thres_rd_ev);
				ctx->thres_flags |= BIGBUF_RD_THRES_FLAG;
			}
		} else {
			if(ctx->thres_flags & BIGBUF_RD_THRES_FLAG) {
				ResetEvent(ctx->thres_rd_ev);
				ctx->thres_flags &= ~BIGBUF_RD_THRES_FLAG;
			}
		}
		ctx->thres_rd_avail = thres_rd_avail;
		LeaveCriticalSection(&(ctx->buf_ptr_lock));
	}
}

/* ---------------------------------------------------------------------------------------------- */

/* Get number of bytes available. */
unsigned __int64 bigbuf_data_avail(struct big_buffer *ctx)
{
	unsigned __int64 buf_used;

	EnterCriticalSection(&(ctx->buf_ptr_lock));
	buf_used = ctx->buf_data_length;
	LeaveCriticalSection(&(ctx->buf_ptr_lock));
	return buf_used;
}

/* Get number of bytes free. */
unsigned __int64 bigbuf_free_space(struct big_buffer *ctx)
{
	unsigned __int64 buf_free;

	EnterCriticalSection(&(ctx->buf_ptr_lock));
	buf_free = ctx->buf_size - ctx->buf_data_length;
	LeaveCriticalSection(&(ctx->buf_ptr_lock));
	return buf_free;
}

/* ---------------------------------------------------------------------------------------------- */

/* Reset buffer to initial state (delete buffered data). */
void bigbuf_reset(struct big_buffer *ctx)
{
	unsigned int i;
	HANDLE events[4];

	/* Lock all mapping windows */
	events[0] = ctx->win_a_wr_ev;
	events[1] = ctx->win_a_rd_ev;
	events[2] = ctx->win_b_wr_ev;
	events[3] = ctx->win_b_rd_ev;
	WaitForMultipleObjects(4, events, TRUE, INFINITE);

	/* Lock buffer pointers */
	EnterCriticalSection(&(ctx->buf_ptr_lock));

	/* Unmap mapping windows */
	if(ctx->win_a_map_pos != BIGBUF_WINDOW_NO_MAP) {
		MapUserPhysicalPages(ctx->win_a_addr, ctx->win_page_cnt, NULL);
		ctx->win_a_map_pos = BIGBUF_WINDOW_NO_MAP;
	}
	if(ctx->win_b_map_pos != BIGBUF_WINDOW_NO_MAP) {
		MapUserPhysicalPages(ctx->win_b_addr, ctx->win_page_cnt, NULL);
		ctx->win_b_map_pos = BIGBUF_WINDOW_NO_MAP;
	}

	/* Remove all remaining data from buffer */
	ctx->buf_data_offset = 0;
	ctx->buf_data_length = 0;

	/* Disable thresholds */
	ctx->thres_wr_free = 0;
	ctx->thres_rd_avail = 0;
	ctx->thres_flags = BIGBUF_WR_THRES_FLAG|BIGBUF_RD_THRES_FLAG;
	SetEvent(ctx->thres_wr_ev);
	SetEvent(ctx->thres_rd_ev);

	/* Unlock buffer pointers */
	LeaveCriticalSection(&(ctx->buf_ptr_lock));

	/* Unlock mapping windows */
	for(i = 0; i < 4; i++)
		SetEvent(events[i]);
}

/* ---------------------------------------------------------------------------------------------- */

/* Initialize buffer */
int bigbuf_init(struct msg_filter *mf, struct big_buffer *ctx, int use_vm_buffer,
				 unsigned __int64 buf_size_req, unsigned __int64 win_size_req)
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	memset(ctx, 0, sizeof(struct big_buffer)); 

	ctx->page_size = si.dwPageSize;
	ctx->win_a_map_pos = BIGBUF_WINDOW_NO_MAP;
	ctx->win_b_map_pos = BIGBUF_WINDOW_NO_MAP;

	/* Initialize buffer synchronization objects */
	InitializeCriticalSection(&(ctx->buf_ptr_lock));
	ctx->thres_wr_ev = CreateEvent(NULL, TRUE, TRUE, NULL);
	ctx->thres_rd_ev = CreateEvent(NULL, TRUE, TRUE, NULL);
	ctx->thres_flags = BIGBUF_WR_THRES_FLAG|BIGBUF_RD_THRES_FLAG;

	if((ctx->thres_wr_ev == NULL) || (ctx->thres_rd_ev == NULL))
	{
		bigbuf_free(ctx);
		return 0;
	}

	if(use_vm_buffer)
	{
		TCHAR fmt_buf[64];

		ctx->buf_size = ((buf_size_req + si.dwAllocationGranularity - 1U) / 
			si.dwAllocationGranularity) * si.dwAllocationGranularity;

		msg_print(mf, MSG_VERY_VERBOSE,
			_T("Allocating virtual memory buffer (%s)...\n"),
			fmt_block_size(fmt_buf, ctx->buf_size, 1));

		ctx->buf_addr = VirtualAlloc(NULL, (SIZE_T)(ctx->buf_size), MEM_COMMIT, PAGE_READWRITE);

		if(ctx->buf_addr == NULL)
		{
			DWORD error = GetLastError();
			msg_print(mf, MSG_ERROR,
				_T("Can't allocate %s of virtual memory for buffer: %s (%u).\n"),
				fmt_block_size(fmt_buf, ctx->buf_size, 1), msg_winerr(mf, error), error);
			bigbuf_free(ctx);
			return 0;
		}
	}
	else
	{
		TCHAR fmt_buf1[64];
		ULONG_PTR page_cnt_requested;
	
		/* Calculate number of pages for buffer and mapping window */
		ctx->win_size = (ULONG_PTR) (((win_size_req + si.dwAllocationGranularity - 1U) / 
			si.dwAllocationGranularity) * si.dwAllocationGranularity);
		ctx->win_page_cnt = ctx->win_size / si.dwPageSize;
		ctx->buf_page_cnt = (ULONG_PTR)((buf_size_req + ctx->win_size - 1U) / ctx->win_size) * ctx->win_page_cnt;
		ctx->buf_size = (unsigned __int64)(ctx->buf_page_cnt) * si.dwPageSize;

		msg_print(mf, MSG_VERBOSE,
			_T("Allocating userpage buffer (%s)...\n"),
			fmt_block_size(fmt_buf1, ctx->buf_size, 1));

		/* Allocate userpages for buffer */
		ctx->page_pfn = VirtualAlloc(NULL, ctx->buf_page_cnt * sizeof(ULONG_PTR),
			MEM_COMMIT, PAGE_READWRITE);
		if(ctx->page_pfn == NULL)
		{
			DWORD error = GetLastError();
			msg_print(mf, MSG_ERROR, _T("Can't allocate %s of virtual memory: %s (%u).\n"),
				fmt_block_size(fmt_buf1, ctx->buf_size, 1), msg_winerr(mf, error), error);
			bigbuf_free(ctx);
			return 0;
		}

		page_cnt_requested = ctx->buf_page_cnt;
		if(!AllocateUserPhysicalPages(GetCurrentProcess(), &(ctx->buf_page_cnt), ctx->page_pfn))
		{
			DWORD error = GetLastError();
			msg_print(mf, MSG_ERROR,
				_T("Can't allocate %s of user pages for buffer.\n%s (%u).\n"),
				fmt_block_size(fmt_buf1, (unsigned __int64)page_cnt_requested * si.dwPageSize, 1),
				msg_winerr(mf, error), error);
			VirtualFree(ctx->page_pfn, 0U, MEM_RELEASE);
			ctx->page_pfn = NULL;
			ctx->buf_page_cnt = 0U;
			bigbuf_free(ctx);
			return 0;
		}

		if(ctx->buf_page_cnt != page_cnt_requested)
		{
			TCHAR fmt_buf2[64];
			msg_print(mf, MSG_ERROR,
				_T("Can't allocate %s of user pages for buffer (%s available).\n"),
				fmt_block_size(fmt_buf1, (unsigned __int64)page_cnt_requested * si.dwPageSize, 1),
				fmt_block_size(fmt_buf2, (unsigned __int64)(ctx->buf_page_cnt) * si.dwPageSize, 1));
			bigbuf_free(ctx);
			return 0;
		}

		/* Allocate userpage mapping windows */
		ctx->win_a_addr = VirtualAlloc(NULL, ctx->win_size, MEM_RESERVE|MEM_PHYSICAL, PAGE_READWRITE);
		ctx->win_b_addr = VirtualAlloc(NULL, ctx->win_size, MEM_RESERVE|MEM_PHYSICAL, PAGE_READWRITE);
		ctx->win_a_wr_ev = CreateEvent(NULL, FALSE, TRUE, NULL);
		ctx->win_a_rd_ev = CreateEvent(NULL, FALSE, TRUE, NULL);
		ctx->win_b_wr_ev = CreateEvent(NULL, FALSE, TRUE, NULL);
		ctx->win_b_rd_ev = CreateEvent(NULL, FALSE, TRUE, NULL);

		if( (ctx->win_a_addr == NULL) || (ctx->win_a_wr_ev == NULL) || (ctx->win_a_rd_ev == NULL) ||
			(ctx->win_b_addr == NULL) || (ctx->win_b_wr_ev == NULL) || (ctx->win_b_rd_ev == NULL) )
		{
			DWORD error = GetLastError();
			msg_print(mf, MSG_ERROR, _T("Can't reserve %I64u virtual memory pages: %s (%u).\n"),
				(unsigned __int64)(ctx->win_page_cnt), msg_winerr(mf, error), error);
			bigbuf_free(ctx);
			return 0;
		}
	}

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Free buffer */
void bigbuf_free(struct big_buffer *ctx)
{
	DeleteCriticalSection(&(ctx->buf_ptr_lock));
	if(ctx->thres_wr_ev != NULL)
		CloseHandle(ctx->thres_wr_ev);
	if(ctx->thres_rd_ev != NULL)
		CloseHandle(ctx->thres_rd_ev);
	if(ctx->buf_addr != NULL)
		VirtualFree(ctx->buf_addr, 0U, MEM_RELEASE);
	if(ctx->buf_page_cnt != 0U)
	{
		FreeUserPhysicalPages(GetCurrentProcess(), &(ctx->buf_page_cnt), ctx->page_pfn);
		VirtualFree(ctx->page_pfn, 0U, MEM_RELEASE);
	}
	if(ctx->win_a_addr != NULL)
	{
		if(ctx->win_a_map_pos != BIGBUF_WINDOW_NO_MAP)
			MapUserPhysicalPages(ctx->win_a_addr, ctx->win_page_cnt, NULL);
		VirtualFree(ctx->win_a_addr, 0U, MEM_RELEASE);
	}
	if(ctx->win_b_addr != NULL)
	{
		if(ctx->win_b_map_pos != BIGBUF_WINDOW_NO_MAP)
			MapUserPhysicalPages(ctx->win_b_addr, ctx->win_page_cnt, NULL);
		VirtualFree(ctx->win_b_addr, 0U, MEM_RELEASE);
	}
	if(ctx->win_a_wr_ev != NULL)
		CloseHandle(ctx->win_a_wr_ev);
	if(ctx->win_a_rd_ev != NULL)
		CloseHandle(ctx->win_a_rd_ev);
	if(ctx->win_b_wr_ev != NULL)
		CloseHandle(ctx->win_b_wr_ev);
	if(ctx->win_b_rd_ev != NULL)
		CloseHandle(ctx->win_b_rd_ev);

	memset(ctx, 0, sizeof(struct big_buffer)); 
}

/* ---------------------------------------------------------------------------------------------- */
