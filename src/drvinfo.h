/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <windows.h>
#include "cmdline.h"

/* ---------------------------------------------------------------------------------------------- */

void list_drive_info(struct msg_filter *mf, TAPE_GET_DRIVE_PARAMETERS *drive, int features, int verbose);

void list_media_info(struct msg_filter *mf, TAPE_GET_DRIVE_PARAMETERS *drive, TAPE_GET_MEDIA_PARAMETERS *media);

/* ---------------------------------------------------------------------------------------------- */
