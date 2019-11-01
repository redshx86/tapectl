/* ---------------------------------------------------------------------------------------------- */

#include "ratectr.h"

/* ---------------------------------------------------------------------------------------------- */

/* reset or initialize transfer rate counter */
void rate_reset(struct rate_counter *ctr)
{
	ctr->nextpt = 0;
	ctr->length = 0;
}

/* ---------------------------------------------------------------------------------------------- */

/* add rate counting point and return current transfer rate per second */
unsigned __int64 rate_update(struct rate_counter *ctr,
	unsigned int msecs, unsigned __int64 count)
{
	size_t base;

	ctr->msecs_pt[ctr->nextpt] = msecs;
	ctr->count_pt[ctr->nextpt] = count;

	ctr->nextpt++;
	if(ctr->nextpt == RATE_COUNT_POINTS)
		ctr->nextpt = 0;
	base = ctr->nextpt;

	if(ctr->length < RATE_COUNT_POINTS)
	{
		ctr->length++;
		base = 0;
	}

	if(msecs == ctr->msecs_pt[base])
		return 0;

	return 1000ULL * (count - ctr->count_pt[base]) / (msecs - ctr->msecs_pt[base]);
}

/* ---------------------------------------------------------------------------------------------- */
