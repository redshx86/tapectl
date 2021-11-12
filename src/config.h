/* ---------------------------------------------------------------------------------------------- */

#pragma once

/* ---------------------------------------------------------------------------------------------- */

#define VERSION						_T("0.92b")

#define DEFAULT_TAPE_NAME			_T("\\\\.\\Tape0")

#define DEFAULT_IO_BLOCK_SIZE		(   1UL << 20)
#define MIN_IO_BLOCK_SIZE			  512UL
#define MAX_IO_BLOCK_SIZE			( 256UL << 20)

#define DEFAULT_IO_QUEUE_SIZE		16
#define MAX_IO_QUEUE_SIZE			1024

#define DEFAULT_BUFFER_SIZE			( 128UL << 20)
#define MIN_BUFFER_BLOCKS			4UL
#define MIN_BUFFER_SIZE				(   4UL << 20)
#define MAX_HEAP_BUFFER_SIZE		( 512UL << 20)
#define PAGE_MAPPING_WINDOW_SIZE	(  64UL << 20)

#define CRC_BLOCK_SIZE				(  64UL << 10)
#define MIN_CRC_BUFFER				(   1UL << 20)
#define MAX_CRC_BUFFER				( 2 * MAX_IO_BLOCK_SIZE )

/* Give warning if less than ~3.6% of media capacity remaining after writing file */
#define CAP_THRES(full_cap)			((full_cap) - (full_cap) / 28UL)

#define STATS_REFRESH_INTERVAL		250
#define RATE_COUNT_POINTS			8

/* ---------------------------------------------------------------------------------------------- */
