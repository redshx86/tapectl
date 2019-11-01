/* ---------------------------------------------------------------------------------------------- */

#include "util/fmt.h"
#include "cmdinfo.h"

/* ---------------------------------------------------------------------------------------------- */

/* Display detailed operation info (before command execution) */

void tape_operation_info(struct msg_filter *mf, struct tape_operation *op)
{
	TCHAR size_str_buf[64];

	switch(op->code)
	{
	/* Drive commands */
	case OP_SET_COMPRESSION: /* Enable or disable data compression */
		msg_print(mf, MSG_MESSAGE, _T("%s data compression.\n"),
			op->enable ? _T("Enable") : _T("Disable"));
		break;
	case OP_SET_DATA_PADDING: /* Enable or disable data padding */
		msg_print(mf, MSG_MESSAGE, _T("%s data padding.\n"),
			op->enable ? _T("Enable") : _T("Disable"));
		break;
	case OP_SET_ECC: /* Enable or disable ECC */
		msg_print(mf, MSG_MESSAGE, _T("%s ECC.\n"),
			op->enable ? _T("Enable") : _T("Disable"));
		break;
	case OP_SET_REPORT_SETMARKS: /* Enable or disable setmark reporting */
		msg_print(mf, MSG_MESSAGE, _T("%s setmark reporting.\n"),
			op->enable ? _T("Enable") : _T("Disable"));
		break;
	case OP_SET_EOT_WARNING_ZONE: /* Set EOT warning zone size */
		msg_print(mf, MSG_MESSAGE, _T("Set EOT warning zone size to %s.\n"),
			fmt_block_size(size_str_buf, op->size, 2));
		break;
	case OP_SET_BLOCK_SIZE: /* Set block size */
		msg_print(mf, MSG_MESSAGE, _T("Set block size to %s.\n"),
			fmt_block_size(size_str_buf, op->size, 2));
		break;
	case OP_LOCK_TAPE_EJECT: /* Lock media ejection */
	case OP_UNLOCK_TAPE_EJECT: /* Unlock media ejection */
		msg_print(mf, MSG_MESSAGE, _T("%s media ejection.\n"),
			(op->code == OP_LOCK_TAPE_EJECT) ? _T("Lock") : _T("Unlock"));
		break;

	/* Tape commands */
	case OP_LOAD_MEDIA: /* Load media */
		msg_print(mf, MSG_MESSAGE, _T("Load media into the drive.\n"));
		break;
	case OP_UNLOAD_MEDIA: /* Unload media */
		msg_print(mf, MSG_MESSAGE, _T("Unload media from the drive.\n"));
		break;
	case OP_MAKE_PARTITION: /* Create tape partition */
		msg_print(mf, MSG_MESSAGE,
			_T("Create partition (method=%u, count=%I64u, size=%I64u).\n"),
			op->partition, op->count, op->size);
		break;
	case OP_ERASE_TAPE: /* Erase media */
		msg_print(mf, MSG_MESSAGE, _T("Erase media.\n"));
		break;
	case OP_LIST_TAPE_CAPACITY: /* Show media capacity */
		msg_print(mf, MSG_MESSAGE, _T("Show media capacity.\n"));
		break;
	case OP_TAPE_TENSION: /* Tension tape */
		msg_print(mf, MSG_MESSAGE, _T("Tension tape.\n"));
		break;

	/* Navigation commands */
	case OP_LIST_CURRENT_POSITION: /* List current position */
		msg_print(mf, MSG_MESSAGE, _T("List current position.\n"));
		break;
	case OP_MOVE_TO_ORIGIN: /* Move to origin */
		msg_print(mf, MSG_MESSAGE, _T("Move to origin.\n"));
		break;
	case OP_MOVE_TO_EOD: /* Move to end of data */
		msg_print(mf, MSG_MESSAGE, _T("Seek to the end of data.\n"));
		break;
	case OP_SET_ABS_POSITION: /* Move to absolute position */
		msg_print(mf, MSG_MESSAGE, _T("Seek to absolute block address %I64u.\n"), op->count);
		break;
	case OP_SET_TAPE_POSITION: /* Move to logical position */
		if(op->partition != 0) {
			msg_print(mf, MSG_MESSAGE, _T("Seek to logical block address %u.%I64u.\n"),
				op->partition, op->count);
		} else {
			msg_print(mf, MSG_MESSAGE, _T("Seek to logical block address %I64u.\n"), op->count);
		}
		break;
	case OP_MOVE_BLOCK_NEXT: /* Move to next block */
	case OP_MOVE_BLOCK_PREV: /* Move to previous block */
		msg_print(mf, MSG_MESSAGE, _T("Seek %I64u %s %s.\n"),
			op->count, (op->count == 1) ? _T("block") : _T("blocks"),
			(op->code == OP_MOVE_BLOCK_NEXT) ? _T("forward") : _T("backward"));
		break;
	case OP_MOVE_FILE_NEXT: /* Move to file forward */
	case OP_MOVE_FILE_PREV: /* Move to file backward */
		msg_print(mf, MSG_MESSAGE, _T("Seek %I64u %s %s.\n"),
			op->count, (op->count == 1) ? _T("filemark") : _T("filemarks\n"), 
			(op->code == OP_MOVE_FILE_NEXT) ? _T("forward") : _T("backward"));
		break;
	case OP_MOVE_SMK_NEXT: /* Move to setmark forward */
	case OP_MOVE_SMK_PREV: /* Move to setmark backward */
		msg_print(mf, MSG_MESSAGE, _T("Seek %I64u %s %s.\n"),
			op->count, (op->count == 1) ? _T("setmark") : _T("setmarks"),
			(op->code == OP_MOVE_SMK_NEXT) ? _T("forward") : _T("backward"));
		break;

	/* Read/write operations */
	case OP_READ_DATA: /* Read files from media */
		msg_print(mf, MSG_MESSAGE, _T("Read data to \"%s\".\n"), op->filename);
		break;
	case OP_WRITE_DATA: /* Write file to media */
	case OP_WRITE_DATA_AND_FMK: /* Write file and filemark */
		msg_print(mf, MSG_MESSAGE, _T("Write data from \"%s\"%s.\n"),
			op->filename, (op->code == OP_WRITE_DATA_AND_FMK) ? _T(" and set filemark") : _T(""));
		break;
	case OP_WRITE_FILEMARK: /* Write filemarks */
	case OP_WRITE_SETMARK: /* Write setmarks */
		if(op->count == 1) {
			msg_print(mf, MSG_MESSAGE, _T("Write %s.\n"),
				(op->code == OP_WRITE_FILEMARK) ? _T("filemark") : _T("setmark"));
		} else {
			msg_print(mf, MSG_MESSAGE, _T("Write %I64u %s.\n"),
				op->count, (op->code == OP_WRITE_FILEMARK) ? _T("filemarks") : _T("setmarks"));
		}
		break;
	case OP_TRUNCATE: /* Truncate data at current position */
		msg_print(mf, MSG_MESSAGE, _T("Truncate at current position.\n"));
		break;
	}
}

/* ---------------------------------------------------------------------------------------------- */
