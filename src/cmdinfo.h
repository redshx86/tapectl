/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include "util/msgfilt.h"
#include "cmdline.h"

/* ---------------------------------------------------------------------------------------------- */

/* Display detailed operation info (before command execution) */

void tape_operation_info(
	struct msg_filter *mf,				/* message buffer */
	struct tape_operation *op		/* operation to show details */
	);

/* ---------------------------------------------------------------------------------------------- */
