/* ---------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include "fmt.h"

/* ---------------------------------------------------------------------------------------------- */

/* Parse block size in format <integer>[k/M/G] */
int parse_block_size(const TCHAR *str, unsigned __int64 *p_size)
{
	TCHAR *p;
	unsigned __int64 num, mult;

	/* Parse unsigned integer, check for anything parsed */
	num = _tcstoui64(str, &p, 10);
	if(p == str)
		return 0;

	/* Skip whitespace before multiplier */
	while((*p == _T(' ')) || (*p == _T('\t')))
		p++;

	/* Process multiplier and check for end of string */
	mult = 0;
	switch(*p)
	{
	case 0:
		mult = 1ULL;
		break;
	case _T('k'):
	case _T('K'):
		mult = 1ULL << 10;
		p++;
		break;
	case _T('M'):
		mult = 1ULL << 20;
		p++;
		break;
	case _T('G'):
		mult = 1ULL << 30;
		p++;
		break;
	case _T('T'):
		mult = 1ULL << 40;
		p++;
		break;
	}
	if((mult > 1ULL) && (*p == _T('B')))
		p++;
	if((mult == 0) || (*p != 0))
		return 0;

	*p_size = num * mult;
	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Format block size as <number> <bytes/KB/MB/GB>
 * level == 0 : 1999.99k
 * level == 1 : 1999.99 KB
 * level == 2 : 1999.99 KB (2097141513 bytes)
 **/

const TCHAR* fmt_block_size(TCHAR *buf, unsigned __int64 n, int level)
{
	const TCHAR *title;
	int fractional = 0;
	double value_fract;
	unsigned int value_int;

	if(n < (1ULL << 10)) {
		title = (level >= 1) ? ((n == 1ULL) ? _T(" byte") : _T(" bytes")) : _T("b");
		value_fract = (double)n;
		value_int = (unsigned int) n;
		fractional = 0;
		if(level == 2)
			level = 1;
	} else if(n < (1ULL << 20)) {
		title = (level >= 1) ? _T(" KB") : _T("k");
		value_fract = (double)n / (double)(1ULL << 10);
		value_int = (unsigned int)(n >> 10);
		fractional = ((n & ((1ULL << 10) - 1)) != 0);
	} else if(n < (1ULL << 30)) {
		title = (level >= 1) ? _T(" MB") : _T("M");
		value_fract = (double)n / (double)(1ULL << 20);
		value_int = (unsigned int)(n >> 20);
		fractional = ((n & ((1ULL << 20) - 1)) != 0);
	} else if(n < (1ULL << 40)) {
		title = (level >= 1) ? _T(" GB") : _T("G");
		value_fract = (double)n / (double)(1ULL << 30);
		value_int = (unsigned int)(n >> 30);
		fractional = ((n & ((1ULL << 30) - 1)) != 0);
	} else {
		title = (level >= 1) ? _T(" TB") : _T("T");
		value_fract = (double)n / (double)(1ULL << 40);
		value_int = (unsigned int)(n >> 40);
		fractional = ((n & ((1ULL << 40) - 1)) != 0);
	}

	/* use fractional output for level == 0
	 * to prevent jerking in stats display */
	if((level == 0) && (n >= (1ULL << 10)))
		fractional = 1;

	if(!fractional) {
		switch(level) {
		case 0:
		case 1:
			_stprintf(buf, _T("%u%s"), value_int, title);
			break;
		case 2:
			_stprintf(buf, _T("%u%s (%I64u bytes)"), value_int, title, n);
			break;
		}
	} else {
		switch(level) {
		case 0:
		case 1:
			_stprintf(buf, _T("%.2f%s"), value_fract, title);
			break;
		case 2:
			_stprintf(buf, _T("%.2f%s (%I64u bytes)"), value_fract, title, n);
			break;
		}
	}

	return buf;
}

/* ---------------------------------------------------------------------------------------------- */

/* Display elapsed time of operation 
 * precise == 0 : 3d5h
 * precise == 1 : 3d 5:01:23
 **/

const TCHAR *fmt_elapsed_time(TCHAR *buf, unsigned int seconds, int precise)
{
	unsigned int minutes, hours, days;

	if(seconds < 60UL)
	{
		_stprintf(buf, _T("%us"), seconds);
	}
	else if(seconds < 3600UL)
	{
		if(!precise) {
			_stprintf(buf, _T("%um%us"),
				seconds / 60UL, seconds % 60UL);
		} else {
			_stprintf(buf, _T("%u:%02u"),
				seconds / 60UL,seconds % 60UL);
		}
	}
	else if(seconds < 86400UL)
	{
		minutes = seconds / 60UL;
		if(!precise) {
			_stprintf(buf, _T("%uh%um"),
				minutes / 60UL, minutes % 60UL);
		} else {
			_stprintf(buf, _T("%u:%02u:%02u"),
				minutes / 60UL, minutes % 60UL, seconds % 60UL);
		}
	}
	else
	{
		minutes = seconds / 60UL;
		hours = minutes / 60UL;
		days = hours / 24UL;
		if(!precise) {
			_stprintf(buf, _T("%ud%uh"), days, hours % 24UL);
		} else {
			_stprintf(buf, _T("%u %s %u:%02u:%02u"),
				days, (days == 1) ? _T("day") : _T("days"),
				hours % 24UL, minutes % 60UL, seconds % 60UL);
		}
	}
	return buf;
}

/* ---------------------------------------------------------------------------------------------- */
