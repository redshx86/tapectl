/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <crtdbg.h>
#include "msgfilt.h"

/* ---------------------------------------------------------------------------------------------- */

/* Initialize message filter */
void msg_init(struct msg_filter *mf)
{
	memset(mf, 0, sizeof(struct msg_filter));
	mf->stream = stdout;
	mf->report_level = 0x7fffffff; /* use max before verbosity mode set */
	mf->langid = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US); /* fall back to defaul on error */
}

/* Free message filter */
void msg_free(struct msg_filter *mf)
{
	size_t i;

	for(i = 0; i < mf->itemcnt; i++)
		free(mf->items[i].str);
	free(mf->items);
	free(mf->winerrbuf);
	free(mf->tmpbuf);
}

/* ---------------------------------------------------------------------------------------------- */

/* Get windows error message to temp buffer and return pointer */
const TCHAR* msg_winerr(struct msg_filter *mf, unsigned int msgid)
{
	DWORD len, err;

	for(;;)
	{
		/* initialize buffer at first call */
		if(mf->winerrcap == 0) {
			len = 0;
			err = ERROR_INSUFFICIENT_BUFFER;
		} else {
			/* Get message from system and check for error */
			len = FormatMessage(
				FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				msgid,
				mf->langid,
				mf->winerrbuf,
				(DWORD)(mf->winerrcap - 1),
				NULL);
			if(len != 0) {
				/* Trim period and crlf at end of message */
				while((len > 0) && ((mf->winerrbuf[len - 1] == '\r') || (mf->winerrbuf[len - 1] == '\n')))
					len--;
				if((len > 0) && (mf->winerrbuf[len - 1] == '.'))
					len--;
				mf->winerrbuf[len] = 0;
				break;
			}
			err = GetLastError();
		}

		/* Expand buffer iteratively until it fit
		 * (256 should be enough for any message, but who knows...) */
		if(err == ERROR_INSUFFICIENT_BUFFER) {
			free(mf->winerrbuf);
			mf->winerrcap = (mf->winerrcap == 0) ? 256 : (mf->winerrcap * 2);
			if( (mf->winerrbuf = malloc(mf->winerrcap * sizeof(TCHAR))) == NULL ) {
				mf->winerrcap = 0;
				mf->out_of_memory = 1;
				break;
			}
		}
		/* Fall back to default language if system can't use english */
		else if(((err == 15100) || (err == ERROR_RESOURCE_LANG_NOT_FOUND)) && (mf->langid != 0)) {
			mf->langid = 0;
		}
		/* or use default message  */
		else {
			break;
		}
	}

	/* return empty string if buffer not available */
	if(mf->winerrcap == 0)
		return _T("");

	/* use error code as default message */
	if(len == 0)
		_stprintf(mf->winerrbuf, _T("error %u"), msgid);

	return mf->winerrbuf;
}

/* ---------------------------------------------------------------------------------------------- */

/* Format message to temp buffer */
static int msg_format(struct msg_filter *mf, const TCHAR *fmt, va_list ap)
{
	/* Format message to temp buffer */
	for(;;)
	{
		/* Try fitting mesage to existing buffer */
		if( (mf->tmpcap != 0) && (_vsntprintf(mf->tmpbuf, mf->tmpcap - 1, fmt, ap) >= 0) )
		{
			mf->tmpbuf[mf->tmpcap - 1] = 0;
			break;
		}

		/* Expand buffer iteratively until message fit */
		free(mf->tmpbuf);
		mf->tmpcap = (mf->tmpcap == 0) ? 256 : (mf->tmpcap * 2);
		mf->tmpbuf = malloc(mf->tmpcap * sizeof(TCHAR));
		if(mf->tmpbuf == NULL)
		{
			mf->tmpcap = 0;
			mf->out_of_memory = 1;
			return 0;
		}
	}

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Format message and add to temp list */
static int msg_append_v(struct msg_filter *mf, int level, const TCHAR *fmt, va_list ap)
{
	TCHAR *str_tmp;

	/* Check message level */
	if(level > mf->report_level)
		return 1;

	/* Format message to temp buffer */
	if(!msg_format(mf, fmt, ap))
		return 0;

	/* Expand message list if required */
	if(mf->itemcnt == mf->itemcap)
	{
		size_t itemcap_tmp;
		struct msg_entry *items_tmp;
		
		itemcap_tmp = mf->itemcap + 32;
		if( (items_tmp = realloc(mf->items, itemcap_tmp * sizeof(struct msg_entry))) == NULL ) {
			mf->out_of_memory = 1;
			return 0;
		}
		mf->itemcap = itemcap_tmp;
		mf->items = items_tmp;
	}

	/* Copy message to list entry */
	if( (str_tmp = _tcsdup(mf->tmpbuf)) == NULL ) {
		mf->out_of_memory = 1;
		return 0;
	}
	mf->items[mf->itemcnt].level = level;
	mf->items[mf->itemcnt].str = str_tmp;
	mf->itemcnt++;

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Print title before message */
static void print_msg_title(struct msg_filter *mf, int level)
{
	switch(level)
	{
	case MSG_ERROR:
		_fputts(_T("ERROR: "), mf->stream);
		break;
	case MSG_WARNING:
		_fputts(_T("Warning: "), mf->stream);
		break;
	}
}

/* ---------------------------------------------------------------------------------------------- */

/* Print message to output stream */
int msg_print(struct msg_filter *mf, int level, const TCHAR *fmt, ...)
{
	va_list ap;
	int success;

	msg_flush(mf);

	if(level > mf->report_level)
		return 1;

	va_start(ap, fmt);
	success = msg_format(mf, fmt, ap);
	va_end(ap);

	if(success) {
		print_msg_title(mf, level);
		_fputts(mf->tmpbuf, mf->stream);
	} else {
		_fputts(_T("Not enough memory to format message?"), mf->stream);
	}

	return 0;
}

/* ---------------------------------------------------------------------------------------------- */

/* Add message to buffer */
int msg_append(struct msg_filter *mf, int level, const TCHAR *fmt, ...)
{
	va_list ap;
	int success;

	va_start(ap, fmt);
	success = msg_append_v(mf, level, fmt, ap);
	va_end(ap);

	return success;
}

/* ---------------------------------------------------------------------------------------------- */

/* Print messages from buffer */
int msg_flush(struct msg_filter *mf)
{
	size_t i;

	/* Display messages */
	for(i = 0; i < mf->itemcnt; i++)
	{
		if(mf->items[i].level <= mf->report_level)
		{
			/* Write message text */
			print_msg_title(mf, mf->items[i].level);
			_fputts(mf->items[i].str, mf->stream);
		}
		free(mf->items[i].str); /* free messages */
	}
	mf->itemcnt = 0; /* clear message count */

	/* Write OOM message */
	if(mf->out_of_memory) {
		_fputts(_T("Not enough memory to format message?"), mf->stream);
		mf->out_of_memory = 0;
		return 0;
	}

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */
