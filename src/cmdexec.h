/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "util/msgfilt.h"
#include "tapeio/tapeio.h"
#include "cmdline.h"

/* ---------------------------------------------------------------------------------------------- */

/* Execute operation */
int tape_operation_execute(
	struct msg_filter *mf,
	struct tape_io_ctx *io_ctx,			/* Buffer for reading/writing tape */
	struct tape_operation *op,			/* Operation to execute */
	HANDLE h_tape,						/* Drive handle */
	TAPE_GET_DRIVE_PARAMETERS *drive	/* Drive parameters or NULL */
	);

/* ---------------------------------------------------------------------------------------------- */
