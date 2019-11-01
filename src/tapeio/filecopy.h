/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "../util/msgfilt.h"
#include "bigbuff.h"

/* ---------------------------------------------------------------------------------------------- */

#define COPY_SUSTAIN_WRITE				0x0001
#define COPY_SUSTAIN_READ				0x0002
#define COPY_NO_PADDING_INFO			0x0004

int copy_file(struct msg_filter *mf, struct big_buffer *cb, unsigned int flags,
	HANDLE h_dst, size_t dst_queue_size, size_t dst_block_size, size_t dst_block_align,
	HANDLE h_src, size_t src_queue_size, size_t src_block_size, unsigned __int64 src_data_size,
	size_t crc_buffer_size, size_t crc_block_size,
	unsigned __int64 *p_data_size, unsigned __int64 *p_padded_size);

/* ---------------------------------------------------------------------------------------------- */
