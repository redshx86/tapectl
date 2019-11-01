/* ---------------------------------------------------------------------------------------------- */

#include "setpriv.h"

/* ---------------------------------------------------------------------------------------------- */

int set_privilegy(const TCHAR *name, int enable, int *p_prev_state, DWORD *p_err)
{
	TOKEN_PRIVILEGES priv_set, priv_get;
	HANDLE h_token;
	DWORD size;

	priv_set.PrivilegeCount = 1;
	priv_set.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

	if(!LookupPrivilegeValue(NULL, name, &(priv_set.Privileges[0].Luid)))
	{
		*p_err = GetLastError();
		return 0;
	}

	if( ! OpenProcessToken(GetCurrentProcess(),
		TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES, &h_token) )
	{
		*p_err = GetLastError();
		return 0;
	}

	priv_get.PrivilegeCount = 1;

	if(!AdjustTokenPrivileges(h_token, FALSE, &priv_set, sizeof(priv_get), &priv_get, &size))
	{
		*p_err = GetLastError();
		CloseHandle(h_token);
		return 0;
	}

	if(p_prev_state)
		*p_prev_state = (priv_get.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED) ? 1 : 0;

	CloseHandle(h_token);

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */
