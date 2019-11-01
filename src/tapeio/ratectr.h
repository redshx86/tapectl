/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include "../config.h"

/* ---------------------------------------------------------------------------------------------- */

struct rate_counter
{
	unsigned __int64 count_pt[RATE_COUNT_POINTS];
	unsigned int msecs_pt[RATE_COUNT_POINTS];
	size_t nextpt;
	size_t length;
};

/* ---------------------------------------------------------------------------------------------- */

/* reset or initialize transfer rate counter */
void rate_reset(struct rate_counter *ctr);

/* add rate counting point and return current transfer rate per second */
unsigned __int64 rate_update(
	struct rate_counter *ctr,
	unsigned int msecs,			/* current millisecond counter */
	unsigned __int64 count);	/* current byte counter */

/* ---------------------------------------------------------------------------------------------- */
