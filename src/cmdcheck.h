/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "cmdline.h"

/* ---------------------------------------------------------------------------------------------- */

/* Check operations list before execution and show details to the user */

int check_tape_operations(
	struct msg_filter *mf,					/* message buffer */
	struct cmd_line_args *cmd_line,		/* command line flags and operation list */
	HANDLE h_tape,						/* handle to tape device or INVALID_HANDLE_VALUE */
	TAPE_GET_DRIVE_PARAMETERS *drive,	/* drive information or NULL */
	TAPE_GET_MEDIA_PARAMETERS *media	/* media information or NULL */
	);

/* ---------------------------------------------------------------------------------------------- */
