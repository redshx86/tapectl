/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <tchar.h>

/* ---------------------------------------------------------------------------------------------- */

/* Ask user to confirm something */
int prompt(const TCHAR *title, int yes_default);

/* Wait few seconds before continuing to allow user to cancel */
int prompt_with_countdown(const TCHAR *title);

/* ---------------------------------------------------------------------------------------------- */
