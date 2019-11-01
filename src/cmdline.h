/* ---------------------------------------------------------------------------------------------- */

#pragma once

#include <tchar.h>
#include "util/msgfilt.h"

/* ---------------------------------------------------------------------------------------------- */

enum tape_operation_code
{
	/* No operation */
	OP_NONE,

	/* Drive commands */
	OP_SET_COMPRESSION,				/* -C <on/off> */
	OP_SET_DATA_PADDING,			/* -D <on/off> */
	OP_SET_ECC,						/* -E <on/off> */
	OP_SET_REPORT_SETMARKS,			/* -R <on/off> */
	OP_SET_EOT_WARNING_ZONE,		/* -W <size> */
	OP_SET_BLOCK_SIZE,				/* -k <size> */
	OP_LOCK_TAPE_EJECT,				/* -x */
	OP_UNLOCK_TAPE_EJECT,			/* -u */

	/* Media commands */
	OP_LOAD_MEDIA,					/* -L */
	OP_UNLOAD_MEDIA,					/* -j */
	OP_MAKE_PARTITION,				/* -K <method,count,size> */
	OP_ERASE_TAPE,					/* -X */
	OP_LIST_TAPE_CAPACITY,			/* -c */
	OP_TAPE_TENSION,				/* -T */

	/* Navigation */
	OP_LIST_CURRENT_POSITION,		/* -l */
	OP_MOVE_TO_ORIGIN,				/* -o */
	OP_MOVE_TO_EOD,					/* -e */
	OP_SET_ABS_POSITION,			/* -a <address> */
	OP_SET_TAPE_POSITION,			/* -s [P.]<address> */
	OP_MOVE_BLOCK_NEXT,				/* -n [count] */
	OP_MOVE_BLOCK_PREV,				/* -p [count] */
	OP_MOVE_FILE_NEXT,				/* -f [count] */
	OP_MOVE_FILE_PREV,				/* -b [count] */
	OP_MOVE_SMK_NEXT,				/* -F [count] */
	OP_MOVE_SMK_PREV,				/* -B [count] */
	
	/* Read/Write */
	OP_READ_DATA,					/* -r <file> */
	OP_WRITE_DATA,					/* -w <file> */
	OP_WRITE_DATA_AND_FMK,			/* -W <file> */
	OP_WRITE_FILEMARK,				/* -m [count] */
	OP_WRITE_SETMARK,				/* -M [count] */
	OP_TRUNCATE						/* -t */
};

struct tape_operation
{
	struct tape_operation *next;
	enum tape_operation_code code;
	unsigned int enable;
	unsigned int partition;
	unsigned __int64 count;
	unsigned __int64 size;
	TCHAR * filename;
};

/* ---------------------------------------------------------------------------------------------- */

#define MODE_EXIT					0x0001
#define MODE_SHOW_HELP				0x0002
#define MODE_VERBOSE				0x0004
#define MODE_VERY_VERBOSE			0x0008
#define MODE_SHOW_OPERATIONS		0x0010
#define MODE_QUIET					0x0020
#define MODE_NO_EXTRA_CHECKS		0x0040
#define MODE_NO_OVERWRITE_CHECK		0x0080
#define MODE_PROMPT_OVERWRITE		0x0100
#define MODE_TEST					0x0200
#define MODE_LIST_DRIVE_INFO		0x0400
#define MODE_WINDOWS_BUFFERING		0x0800

struct cmd_line_args
{
	unsigned int flags;

	TCHAR tape_device[16];

	unsigned __int64 buffer_size;
	unsigned int io_block_size;
	unsigned int io_queue_size;
	
	struct tape_operation *op_list;
	struct tape_operation **next_op_ptr;
	unsigned int op_count;
};

/* ---------------------------------------------------------------------------------------------- */

void usage_help(struct msg_filter *mf);

/* Parse and fill program invokation parameters */
int parse_command_line(struct msg_filter *mf, struct cmd_line_args *cmd_line);

/* Free parsed invokation parameters */
void free_cmd_line_args(struct cmd_line_args *cmd_line);

/* ---------------------------------------------------------------------------------------------- */
