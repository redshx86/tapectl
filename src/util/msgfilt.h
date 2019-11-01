/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <tchar.h>

/* ---------------------------------------------------------------------------------------------- */

/* Message filtering levels */
enum
{
	MSG_MESSAGE,
	MSG_ERROR,
	MSG_WARNING,
	MSG_INFO,
	MSG_VERBOSE,
	MSG_VERY_VERBOSE
};

/* Message entry buffer */
struct msg_entry
{
	int level;
	TCHAR *str;
};

/* Message filtering list */
struct msg_filter
{
	FILE *stream;
	int report_level;

	TCHAR *winerrbuf;
	size_t winerrcap;
	unsigned int langid;

	TCHAR *tmpbuf;
	size_t tmpcap;

	struct msg_entry *items;
	size_t itemcnt;
	size_t itemcap;

	int out_of_memory;
};

/* ---------------------------------------------------------------------------------------------- */

/* Initialize/free message filter */
void msg_init(struct msg_filter *mf);
void msg_free(struct msg_filter *mf);

/* Get windows error message to temp buffer and return pointer */
const TCHAR* msg_winerr(struct msg_filter *mf, unsigned int msgid);

/* Print message to output stream */
int msg_print(struct msg_filter *mf, int level, const TCHAR *fmt, ...);

/* Add message to buffer */
int msg_append(struct msg_filter *mf, int level, const TCHAR *fmt, ...);

/* Print messages from buffer */
int msg_flush(struct msg_filter *mf);

/* ---------------------------------------------------------------------------------------------- */

