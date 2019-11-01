/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "../util/msgfilt.h"

/* ---------------------------------------------------------------------------------------------- */

#define BIGBUF_WR_THRES_FLAG	0x0001	/* Buffer writability flag / free space threshold */
#define BIGBUF_RD_THRES_FLAG	0x0002	/* Buffer readability flag / avail data threshold */

#define BIGBUF_WINDOW_NO_MAP	((ULONG_PTR)-1)

struct big_buffer
{
	/* ---------------------------------- */
	/* Buffer data pointers */

	CRITICAL_SECTION buf_ptr_lock;		/* Critical section for buffer pointers access */

	unsigned __int64 buf_size;			/* Size of buffer in bytes */
	unsigned __int64 buf_data_offset;	/* Data offset in bytes */
	unsigned __int64 buf_data_length;	/* Data length in bytes */

	/* ---------------------------------- */
	/* Read/write thresholds */

	unsigned __int64 thres_wr_free;		/* Buffer writability threshold (free space bytes) */
	unsigned __int64 thres_rd_avail;		/* Buffer readability threshold (avail data bytes) */
	unsigned int thres_flags;			/* Current state of threshold events */
	HANDLE thres_wr_ev;					/* Buffer writability threeshold event */
	HANDLE thres_rd_ev;					/* Buffer readability threshold event */

	/* ---------------------------------- */
	/* Virtual memory buffer */

	BYTE *buf_addr;						/* Allocated memory */

	/* ---------------------------------- */
	/* Userpage buffer  */

	DWORD page_size;					/* Size of memory page in bytes */

	/* Buffer PFN array */
	ULONG_PTR buf_page_cnt;				/* Number of pages allocated */
	ULONG_PTR * page_pfn;				/* PFNs of allocated pages */

	/* Mapping windows */
	ULONG_PTR win_size;					/* Window size in bytes */
	ULONG_PTR win_page_cnt;				/* Window size in pages */
	ULONG_PTR win_a_map_pos;			/* Window A current mapping position in pages */
	ULONG_PTR win_b_map_pos;			/* Window B current mapping position in pages */
	BYTE *win_a_addr;					/* Window A virtual address */
	BYTE *win_b_addr;					/* Window B virtual address */
	HANDLE win_a_wr_ev;					/* Window A writable event */
	HANDLE win_a_rd_ev;					/* Window A readable event */
	HANDLE win_b_wr_ev;					/* Window B writable event */
	HANDLE win_b_rd_ev;					/* Window B readable event */
};


/* Write data to buffer. Buffer should have enough free space. */
int bigbuf_write(struct big_buffer *ctx, const void *src, size_t length, DWORD *p_err);

/* Read data from buffer. Buffer must have enough available data. */
int bigbuf_read(struct big_buffer *ctx, void *dst, size_t length, DWORD *p_err);

/* Set free space threshold (buffer writable event). */
void bigbuf_set_thres_write(struct big_buffer *ctx, unsigned __int64 thres_wr_free);

/* Set available data threshold (buffer readable event). */
void bigbuf_set_thres_read(struct big_buffer *ctx, unsigned __int64 thres_rd_avail);

/* Get number of bytes available. */
unsigned __int64 bigbuf_data_avail(struct big_buffer *ctx);

/* Get number of bytes free. */
unsigned __int64 bigbuf_free_space(struct big_buffer *ctx);

/* Reset buffer to initial state (delete buffered data). */
void bigbuf_reset(struct big_buffer *ctx);

/* Initialize buffer */
int bigbuf_init(
	struct msg_filter *mf,
	struct big_buffer *ctx,
	int use_vm_buffer,					/* use buffer in virtual memory */
	unsigned __int64 buf_size_req,		/* size of buffer (aligned to read/write window) */
	unsigned __int64 win_size_req);		/* size of read/write window (aligned to page size) */

/* Free buffer */
void bigbuf_free(struct big_buffer *ctx);

/* ---------------------------------------------------------------------------------------------- */
