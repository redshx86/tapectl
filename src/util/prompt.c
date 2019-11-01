/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include <stdio.h>
#include "prompt.h"

/* ---------------------------------------------------------------------------------------------- */

/* Ask user to confirm something */
int prompt(const TCHAR *title, int yes_default)
{
	TCHAR answer[16];

	_tprintf(yes_default ? _T("%s [Y/n] ") : _T("%s [y/N] "), title);
	_fgetts(answer, 16, stdin);
	return ( (answer[0] == 'y') || (answer[0] == _T('Y')) || 
		((answer[0] == _T('\n')) && yes_default) );
}

/* ---------------------------------------------------------------------------------------------- */

/* Wait few seconds before continuing to allow user to cancel */

static int confirm_prompt;

static BOOL WINAPI prompt_abort_handler(DWORD code)
{
	if((code == CTRL_C_EVENT) && (confirm_prompt != 0)) {
		confirm_prompt = 0;
		return TRUE;
	}

	return FALSE;
}

int prompt_with_countdown(const TCHAR *title)
{
	int count;

	confirm_prompt = 1;

	/* let user think for some 11 seconds */
	SetConsoleCtrlHandler(prompt_abort_handler, TRUE);
	for(count = 10; count >= 0; count--)
	{
		_tprintf(_T("\r%s Press Ctrl+C to abort [%u]... "), title, count);
		Sleep(1000);
		if(!confirm_prompt)
			break;
	}
	SetConsoleCtrlHandler(prompt_abort_handler, FALSE);

	if(!confirm_prompt) {
		_putts(_T("Aborted."));
	} else {
		_putts(_T("Continuing.\n"));
	}

	return confirm_prompt;
}


/* ---------------------------------------------------------------------------------------------- */
