/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include <string.h>
#include "getpath.h"

/* ---------------------------------------------------------------------------------------------- */

int get_program_filename(TCHAR *buf, size_t bufsize, const TCHAR *cfg_ext)
{
	TCHAR *fname = NULL, *ext_ptr, *p;

	/* Get exe path to buffer */
	if(!GetModuleFileName(NULL, buf, (DWORD)bufsize))
		return 0;

	/* Replace extension if requested */
	if(cfg_ext != NULL)
	{
		/* Seek to filename */
		for(p = buf; *p != 0; p++) {
			if((*p == _T('\\')) || (*p == _T('/')))
				fname = p + 1;
		}
		if(fname == NULL)
			return 0;

		/* Find file extension */
		ext_ptr = _tcsrchr(fname, _T('.'));
		if(ext_ptr == NULL)
			ext_ptr = fname + _tcslen(fname);

		/* Change extension to required */
		if((ext_ptr - buf) + _tcslen(cfg_ext) + 1 > bufsize)
			return 0;
		_tcscpy(ext_ptr, cfg_ext);
	}

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */
