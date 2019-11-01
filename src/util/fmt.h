/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <tchar.h>

/* ---------------------------------------------------------------------------------------------- */

/* Parse block size in format <integer>[k/M/G] */
int parse_block_size(const TCHAR *str, unsigned __int64 *p_size);

/* Format block size as <number> <bytes/KB/MB/GB>
 * level == 0 : 1999.99k
 * level == 1 : 1999.99 KB
 * level == 2 : 1999.99 KB (2097141513 bytes)
 **/
const TCHAR* fmt_block_size(TCHAR *buf, unsigned __int64 n, int level);

/* Display elapsed time of operation 
 * precise == 0 : 3d5h
 * precise == 1 : 3d 5:01:23
 **/

const TCHAR *fmt_elapsed_time(TCHAR *buf, unsigned int seconds, int precise);

/* ---------------------------------------------------------------------------------------------- */
