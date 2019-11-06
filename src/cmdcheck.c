/* ---------------------------------------------------------------------------------------------- */

#include "util/fmt.h"
#include "util/prompt.h"
#include "cmdinfo.h"
#include "cmdcheck.h"
#include "config.h"

/* ---------------------------------------------------------------------------------------------- */
/* Operation list simulation state */

/* Status of the media */
#define ST_SINGLE_PARTITION	0x0001	/* media has single partition */
#define ST_CAPACITY			0x0002	/* media full capacity known */
#define ST_LOADED			0x0004	/* media is loaded */
#define ST_UNLOADED			0x0008	/* media is unloaded */

/* Data on the media */
#define ST_REMAINING		0x0010	/* media remaining capacity known */
#define ST_EMPTY			0x0020	/* no data on the media */
#define ST_DIRTY			0x0040	/* data has been written to the media */

/* Current position */
#define ST_POSITION			0x0100	/* current position known */
#define ST_AT_END_OF_DATA	0x0200	/* currently at end of data */
#define ST_NO_FILEMARK		0x0400	/* after file without filemark */
#define ST_AT_FILEMARK		0x0800	/* after file with filemark */

/* Message flags */
#define ST_OVERWRITE		0x1000	/* give overwrite message */
#define ST_WARNING			0x2000	/* give warning message */
#define ST_ERROR			0x4000	/* give error message and terminate */

struct cmd_sim_state
{
	unsigned int flags;
	unsigned __int64 capacity;
	unsigned __int64 remaining;
	unsigned __int64 position;
	TAPE_GET_DRIVE_PARAMETERS *drive;
	TAPE_GET_MEDIA_PARAMETERS *media;
};

/* ---------------------------------------------------------------------------------------------- */

/* Check source file exists and available */
static int check_src_file(struct msg_filter *mf, struct cmd_sim_state *st,
	const TCHAR *filename, unsigned __int64 *p_filesize)
{
	HANDLE h_file;
	DWORD error;
	ULARGE_INTEGER file_size;

	/* Try open file for reading */
	h_file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(h_file == INVALID_HANDLE_VALUE)
	{
		error = GetLastError();
		switch(error)
		{
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND: /* give warning if input file not found */
			msg_print(mf, MSG_ERROR,
				_T("Can't open \"%s\": file not found.\n"), filename);
			break;
		case ERROR_ACCESS_DENIED: /* give warning if access denied */
			msg_print(mf, MSG_ERROR,
				_T("Can't open \"%s\": access denied.\n"), filename);
			break;
		case ERROR_SHARING_VIOLATION: /* give warning if file locked */
			msg_print(mf, MSG_ERROR,
				_T("Can't open \"%s\": file is locked by another program.\n"), filename);
			break;
		case ERROR_INVALID_NAME:
		case ERROR_BAD_PATHNAME:
			msg_print(mf, MSG_ERROR,
				_T("Can't open \"%s\": incorrect filename.\n"), filename);
			break;
		default: /* other error */
			msg_print(mf, MSG_ERROR, _T("Can't open \"%s\": %s (%u).\n"),
				filename, msg_winerr(mf, error), error);
			break;
		}
		st->flags |= ST_ERROR;
		return 0;
	}

	/* Get size of file */
	file_size.LowPart = GetFileSize(h_file, &(file_size.HighPart));
	if((file_size.LowPart == INVALID_FILE_SIZE) && ((error = GetLastError()) != NO_ERROR)) {
		msg_print(mf, MSG_ERROR, _T("Can't check size of \"%s\": %s (%u).\n"),
			filename, msg_winerr(mf, error), error);
		st->flags |= ST_ERROR;
		CloseHandle(h_file);
		return 0;
	}

	CloseHandle(h_file);

	*p_filesize = file_size.QuadPart;
	return 1;
}

/* Check for destination file overwriting */
static void check_dest_file(struct msg_filter *mf, struct cmd_sim_state *st,
	const TCHAR *filename, int overwrite_check)
{
	DWORD attr;

	/* Check file attributes */
	attr = GetFileAttributes(filename);
	if(attr == INVALID_FILE_ATTRIBUTES) {
		DWORD error = GetLastError();
		switch(error)
		{
		case ERROR_FILE_NOT_FOUND: /* file not found, this is okay */
			break;
		case ERROR_PATH_NOT_FOUND: /* path not found, file probably can't be writen */
			msg_print(mf, MSG_ERROR,
				_T("Can't save to \"%s\": path does not exists.\n"),
				filename);
			st->flags |= ST_ERROR;
			break;
		case ERROR_INVALID_NAME:
		case ERROR_BAD_PATHNAME:
			msg_print(mf, MSG_ERROR,
				_T("Can't save to \"%s\": incorrect filename.\n"), filename);
			break;
		default:
			msg_print(mf, MSG_ERROR,
				_T("Can't check \"%s\": %s (%u).\n"),
				filename, msg_winerr(mf, error), error);
			st->flags |= ST_ERROR;
			break;
		}
	} else {
		if(attr & FILE_ATTRIBUTE_READONLY) { /* will not overwrite readonly files */
			msg_print(mf, MSG_ERROR,
				_T("Can't save to \"%s\": it's read-only!\n"),
				filename);
			st->flags |= ST_ERROR;
		} else if(attr & FILE_ATTRIBUTE_DIRECTORY) { /* directory isn't good too */
			msg_print(mf, MSG_ERROR,
				_T("Can't save to \"%s\": it's a directory!\n"),
				filename);
			st->flags |= ST_ERROR;
		} else if(overwrite_check) { /* give overwrite warning */
			msg_print(mf, MSG_WARNING,
				_T("Destination \"%s\": will be overwritten!\n"),
				filename);
			st->flags |= ST_OVERWRITE;
		}
	}
}

/* ---------------------------------------------------------------------------------------------- */

/* Check feature support by the drive */
static int check_feature(struct cmd_sim_state *st, DWORD value)
{
	if(st->drive == NULL)
		return 1;

	return ((value & 0x80000000) ? 
		(st->drive->FeaturesHigh & value) : 
		(st->drive->FeaturesLow & value)) != 0;
}

/* Check tape operation before execution */
static void tape_operation_check(struct msg_filter *mf, struct cmd_line_args *cmd_line,
	struct cmd_sim_state *st, struct tape_operation *op, int *p_media_required)
{
	unsigned __int64 file_size;
	int file_size_known;

	*p_media_required = 0;

	switch(op->code)
	{
	/* Drive commands */
	case OP_SET_COMPRESSION: /* Enable or disable data compression */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_SET_COMPRESSION)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support switching data compression.\n"));
			st->flags |= ST_ERROR;
		}
		break;
	case OP_SET_DATA_PADDING: /* Enable or disable data padding */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_SET_PADDING)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support switching data padding.\n"));
			st->flags |= ST_ERROR;
		}
		break;
	case OP_SET_ECC: /* Enable or disable ECC */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_SET_ECC)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support switching ECC.\n"));
			st->flags |= ST_ERROR;
		}
		break;
	case OP_SET_REPORT_SETMARKS: /* Enable or disable setmark reporting */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_SET_REPORT_SMKS)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support switching setmark reporting.\n"));
			st->flags |= ST_ERROR;
		}
		break;
	case OP_SET_EOT_WARNING_ZONE: /* Set EOT warning zone size */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_SET_EOT_WZ_SIZE)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support setting EOT warning zone size.\n"));
			st->flags |= ST_ERROR;
		}
		if(op->size > 0xFFFFFFFFUL) {
			msg_print(mf, MSG_ERROR, _T("EOT warning zone size can't exceed DWORD value range.\n"));
			st->flags |= ST_ERROR;
		}
		break;
	case OP_SET_BLOCK_SIZE: /* Set block size */
		if( !(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && (op->size != 0) && (st->drive != NULL) &&
			((op->size < st->drive->MinimumBlockSize) || (op->size > st->drive->MaximumBlockSize)) )
		{
			TCHAR size_str_buf[64];
			msg_print(mf, MSG_ERROR,
				_T("Block size of %s is out of limit (%u to %u bytes).\n"),
				fmt_block_size(size_str_buf, op->size, 2),
				st->drive->MinimumBlockSize, st->drive->MaximumBlockSize);
			st->flags |= ST_ERROR;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_LOCK_TAPE_EJECT: /* Lock media ejection */
	case OP_UNLOCK_TAPE_EJECT: /* Unlock media ejection */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_LOCK_UNLOCK)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support locking media ejection.\n"));
			st->flags |= ST_ERROR;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;

	/* Tape command group */
	case OP_LOAD_MEDIA: /* Load media */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_LOAD_UNLOAD)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support load command.\n"));
			st->flags |= ST_ERROR;
		}
		if(st->flags & ST_UNLOADED) {
			st->flags &= ~ST_UNLOADED;
			st->flags |= ST_POSITION;
			st->position = 0;
		}
		st->flags |= ST_LOADED;
		break;
	case OP_UNLOAD_MEDIA: /* Unload media */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_LOAD_UNLOAD)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support unload command.\n"));
			st->flags |= ST_ERROR;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		st->flags &= ~(ST_LOADED|ST_POSITION|ST_AT_END_OF_DATA|ST_AT_FILEMARK|ST_NO_FILEMARK);
		st->flags |= ST_UNLOADED;
		break;
	case OP_MAKE_PARTITION: /* Create tape partition */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			/* Check for partition support */
			if((st->drive != NULL) && (st->drive->MaximumPartitionCount <= 1)) {
				msg_print(mf, MSG_WARNING, _T("Drive does not support creating partitions.\n"));
				st->flags |= ST_WARNING;
			}
			/* Check for formatting after some data written */
			if(st->flags & ST_DIRTY) {
				msg_print(mf, MSG_WARNING, _T("Suspicious formatting of media after writing data to it.\n"));
				st->flags |= ST_WARNING;
			}
		}
		/* Overwrite warning */
		if(!(cmd_line->flags & MODE_NO_OVERWRITE_CHECK) && !(st->flags & ST_EMPTY)) {
			msg_print(mf, MSG_WARNING, _T("Creating partitions will destory all data on the media.\n"));
			st->flags |= ST_OVERWRITE;
		}
		if((st->drive != NULL) || (st->drive->MaximumPartitionCount > 1))
			st->flags &= ~ST_SINGLE_PARTITION;
		st->flags &= ~(ST_REMAINING|ST_DIRTY|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_EMPTY|ST_POSITION|ST_AT_END_OF_DATA;
		st->position = 0;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_ERASE_TAPE: /* Erase media */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			/* Check for function support */
			if(!check_feature(st, TAPE_DRIVE_ERASE_LONG)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support erase command.\n"));
				st->flags |= ST_ERROR;
			}
			/* Check for erasing after writing some data */
			if(st->flags & ST_DIRTY) {
				msg_print(mf, MSG_WARNING, _T("Suspicious erasing of media after writing data to it.\n"));
				st->flags |= ST_WARNING;
			}
		}
		/* Overwrite prompt  */
		if(!(cmd_line->flags & MODE_NO_OVERWRITE_CHECK) && !(st->flags & ST_EMPTY)) {
			msg_print(mf, MSG_WARNING, _T("Erasing will destory all data on the media.\n"));
			st->flags |= ST_OVERWRITE;
		}
		st->flags &= ~(ST_REMAINING|ST_DIRTY|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_EMPTY|ST_POSITION|ST_AT_END_OF_DATA;
		st->position = 0;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_LIST_TAPE_CAPACITY: /* Show media capacity */
		if( !(cmd_line->flags & MODE_NO_EXTRA_CHECKS) &&
			!check_feature(st, TAPE_DRIVE_TAPE_CAPACITY|TAPE_DRIVE_TAPE_REMAINING) )
		{
			msg_print(mf, MSG_ERROR, _T("Media capacity reporting is not supported by the drive.\n"));
			st->flags |= ST_ERROR;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_TAPE_TENSION: /* Tension tape */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_TENSION)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support tension command.\n"));
			st->flags |= ST_ERROR;
		}
		st->flags &= ~(ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_POSITION;
		st->position = 0;
		*p_media_required = st->flags & ST_UNLOADED;
		break;

	/* Navigation command group */
	case OP_LIST_CURRENT_POSITION: /* List current position */
		if( !(cmd_line->flags & MODE_NO_EXTRA_CHECKS) &&
			!check_feature(st, TAPE_DRIVE_GET_ABSOLUTE_BLK|TAPE_DRIVE_GET_LOGICAL_BLK) )
		{
			msg_print(mf, MSG_ERROR, _T("Drive does not support current position reporting.\n"));
			st->flags |= ST_ERROR;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_MOVE_TO_ORIGIN: /* Move to origin (rewind tape) */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && (st->flags & ST_POSITION) && (st->position == 0))
			msg_print(mf, MSG_INFO, _T("Note: Already at origin.\n"));
		st->flags &= ~(ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_POSITION;
		st->position = 0;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_MOVE_TO_EOD: /* Move to end of data */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && (st->flags & ST_AT_END_OF_DATA))
			msg_print(mf, MSG_INFO, _T("Note: Already at end of data.\n"));
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_END_OF_DATA)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support seeking to end of data.\n"));
			st->flags |= ST_ERROR;
		}
		st->flags &= ~(ST_POSITION|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_AT_END_OF_DATA;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_SET_ABS_POSITION: /* Move to absolute position */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS) && !check_feature(st, TAPE_DRIVE_ABSOLUTE_BLK)) {
			msg_print(mf, MSG_ERROR, _T("Drive does not support seeking to absolute block address.\n"));
			st->flags |= ST_ERROR;
		}
		st->flags &= ~(ST_POSITION|ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		if(op->count == 0) {
			st->flags |= ST_POSITION;
			st->position = 0;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_SET_TAPE_POSITION: /* Move to logical position */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			/* check function supported */
			if(!check_feature(st, TAPE_DRIVE_LOGICAL_BLK))
			{
				msg_print(mf, MSG_ERROR,
					_T("Drive does not support seeking to logical block address.\n"));
				st->flags |= ST_ERROR;
			}
			/* check parition range */
			if((st->flags & ST_SINGLE_PARTITION) && (op->partition > 1))
			{
				msg_print(mf, MSG_WARNING,
					_T("Current media is formatted with single partition.\n"));
				st->flags |= ST_WARNING;
			}
		}
		st->flags &= ~(ST_POSITION|ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		if((st->flags & ST_SINGLE_PARTITION) && (op->partition <= 1) && (op->count == 0)) {
			st->flags |= ST_POSITION;
			st->position = 0;
		}
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_MOVE_BLOCK_NEXT: /* Move to next block */
	case OP_MOVE_BLOCK_PREV: /* Move to previous block */
		/* check function supported */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			if(!check_feature(st, TAPE_DRIVE_RELATIVE_BLKS)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support seeking to relative block address.\n"));
				st->flags |= ST_ERROR;
			}
			if((op->code == OP_MOVE_BLOCK_PREV) && !check_feature(st, TAPE_DRIVE_REVERSE_POSITION)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support seeking in reverse direction.\n"));
				st->flags |= ST_ERROR;
			}
		}
		st->flags &= ~(ST_POSITION|ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_MOVE_FILE_NEXT: /* Move to file forward */
	case OP_MOVE_FILE_PREV: /* Move to file backward */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			if(!check_feature(st, TAPE_DRIVE_FILEMARKS)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support filemark seeking.\n"));
				st->flags |= ST_ERROR;
			}
			if((op->code == OP_MOVE_FILE_PREV) && !check_feature(st, TAPE_DRIVE_REVERSE_POSITION)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support seeking in reverse direction.\n"));
				st->flags |= ST_ERROR;
			}
		}
		st->flags &= ~(ST_POSITION|ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		if(op->code == OP_MOVE_FILE_NEXT) st->flags |= ST_AT_FILEMARK;
		if(op->code == OP_MOVE_FILE_PREV) st->flags |= ST_NO_FILEMARK;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_MOVE_SMK_NEXT: /* Move to setmark forward */
	case OP_MOVE_SMK_PREV: /* Move to setmark backward */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			if(!check_feature(st, TAPE_DRIVE_SETMARKS)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support setmark seeking.\n"));
				st->flags |= ST_ERROR;
			}
			if((op->code == OP_MOVE_SMK_PREV) && !check_feature(st, TAPE_DRIVE_REVERSE_POSITION)) {
				msg_print(mf, MSG_ERROR, _T("Drive does not support seeking in reverse direction.\n"));
				st->flags |= ST_ERROR;
			}
		}
		st->flags &= ~(ST_POSITION|ST_AT_END_OF_DATA|ST_NO_FILEMARK|ST_AT_FILEMARK);
		*p_media_required = st->flags & ST_UNLOADED;
		break;

	/* Read/write operations */
	case OP_READ_DATA: /* Read files from media */
		/* check for end of data */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			if(st->flags & ST_EMPTY) {
				msg_print(mf, MSG_WARNING, _T("Media is empty, nothing will be read.\n"));
				st->flags |= ST_WARNING;
			} else if(st->flags & ST_AT_END_OF_DATA) {
				msg_print(mf, MSG_WARNING, _T("At EOD positon, nothing will be read.\n"));
				st->flags |= ST_WARNING;
			}
		}
		/* check output file */
		check_dest_file(mf, st, op->filename, !(cmd_line->flags & MODE_NO_OVERWRITE_CHECK));
		st->flags &= ~(ST_DIRTY|ST_POSITION|ST_NO_FILEMARK|ST_AT_FILEMARK);
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_WRITE_DATA: /* Write files to media */
	case OP_WRITE_DATA_AND_FMK: /* Write files and filemarks */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			/* Check write protection */
			if((st->media != NULL) && st->media->WriteProtected)
			{
				msg_print(mf, MSG_ERROR, _T("Media is write protected.\n"));
				st->flags |= ST_ERROR;
			}

			/* Check source file */
			if((file_size_known = check_src_file(mf, st, op->filename, &file_size)) != 0)
			{
				/* Add filemark size */
				if((op->code == OP_WRITE_DATA_AND_FMK) && (st->drive != NULL))
					file_size += st->drive->DefaultBlockSize;
				/* Check file size vs full tape capacity */
				if((st->flags & ST_CAPACITY) && (file_size > CAP_THRES(st->capacity)))
				{
					TCHAR size_str_buf[64], size_str_buf2[64];
					msg_print(mf, MSG_WARNING,
						_T("Size of \"%s\" (%s) is%s exceeding the media capacity (%s).\n"),
						op->filename, fmt_block_size(size_str_buf, file_size, 1),
						(file_size > st->capacity) ? _T("") : _T(" nearly"),
						fmt_block_size(size_str_buf2, st->capacity, 1));
					st->flags |= ST_WARNING;
				}
				/* Check file size vs remaining tape capacity */
				else if( (st->flags & ST_CAPACITY) && (st->flags & ST_POSITION) &&
						 (st->position + file_size > CAP_THRES(st->capacity)) )
				{
					TCHAR size_str_buf[64], size_str_buf2[64];
					msg_print(mf, MSG_WARNING,
						_T("Writing \"%s\" (%s) will%s cross the end of media at %s.\n"),
						op->filename, fmt_block_size(size_str_buf, file_size, 1),
						(st->position + file_size > st->capacity) ? _T("") : _T(" nearly"),
						fmt_block_size(size_str_buf2, st->capacity - st->position, 1));
					st->flags |= ST_WARNING;
				}
			}
			/* Check for no filemark between files */
			if(st->flags & ST_NO_FILEMARK)
			{
				msg_print(mf, MSG_WARNING,
					_T("No filemark written before \"%s\".\n"),
					op->filename);
				st->flags |= ST_WARNING;
			}
		}
		else
		{
			file_size = 0;
			file_size_known = 0;
		}
		/* check for overwriting existing data on meida */
		if( !(cmd_line->flags & MODE_NO_OVERWRITE_CHECK) && !(st->flags & ST_AT_END_OF_DATA) )
		{
			msg_print(mf, MSG_WARNING,
				_T("Writing \"%s\" can destroy existing data on the media.\n"),
				op->filename);
			st->flags |= ST_OVERWRITE;
		}

		/* Update remaining capacity and current position */
		if(file_size_known)
		{
			if((st->flags & ST_REMAINING) && (st->flags & ST_AT_END_OF_DATA)) {
				st->remaining = (file_size < st->remaining) ? (st->remaining - file_size) : 0;
			} else {
				st->flags &= ~ST_REMAINING;
			}
			if(st->flags & ST_POSITION) {
				st->position += file_size;
				if((st->flags & ST_CAPACITY) && (st->position > st->capacity))
					st->position = st->capacity;
			}
		} else {
			st->flags &= ~(ST_POSITION|ST_REMAINING);
		}
		st->flags &= ~(ST_EMPTY|ST_NO_FILEMARK|ST_AT_FILEMARK);
		st->flags |= ST_DIRTY|ST_AT_END_OF_DATA;
		if(op->code == OP_WRITE_DATA)
			st->flags |= ST_NO_FILEMARK;
		if(op->code == OP_WRITE_DATA_AND_FMK)
			st->flags |= ST_AT_FILEMARK;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_WRITE_FILEMARK: /* Write filemarks */
	case OP_WRITE_SETMARK: /* Write setmarks */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS))
		{
			/* Check write protection */
			if((st->media != NULL) && st->media->WriteProtected)
			{
				msg_print(mf, MSG_ERROR, _T("Media is write protected.\n"));
				st->flags |= ST_ERROR;
			}
			if( ((op->code == OP_WRITE_FILEMARK) && !check_feature(st, TAPE_DRIVE_WRITE_FILEMARKS)) ||
				((op->code == OP_WRITE_SETMARK) && !check_feature(st, TAPE_DRIVE_WRITE_SETMARKS)) )
			{
				msg_print(mf, MSG_ERROR,
					_T("Drive does not support writing %s.\n"),
					(op->code == OP_WRITE_FILEMARK) ? _T("filemarks") : _T("setmarks"));
				st->flags |= ST_ERROR;
			}
			if((st->flags & ST_POSITION) && (st->position == 0))
			{
				msg_print(mf, MSG_WARNING,
					_T("Suspicious writing %s at beginning of the the media.\n"),
					(op->code == OP_WRITE_FILEMARK) ? _T("filemark") : _T("setmark"));
				st->flags |= ST_WARNING;
			}
			if((op->code == OP_WRITE_FILEMARK) && ((st->flags & ST_AT_FILEMARK) || (op->count > 1)))
			{
				msg_print(mf, MSG_WARNING,
					_T("Suspicious wiring filemark after another filemark.\n"));
				st->flags |= ST_WARNING;
			}
		}
		if( !(cmd_line->flags & MODE_NO_OVERWRITE_CHECK) &&
			!(st->flags & ST_EMPTY) && !(st->flags & ST_AT_END_OF_DATA) )
		{
			msg_print(mf, MSG_WARNING,
				_T("Writing %s may overwrite existing data on the the media.\n"),
				(op->code == OP_WRITE_FILEMARK) ? _T("filemark") : _T("setmark"));
			st->flags |= ST_OVERWRITE;
		}
		if((st->flags & ST_POSITION) && (st->drive != NULL))
			st->position += st->drive->DefaultBlockSize * op->count;
		if((st->flags & ST_AT_END_OF_DATA) && (st->flags & ST_REMAINING)) {
			if(st->drive != NULL)
				st->remaining -= st->drive->DefaultBlockSize * op->count;
		} else {
			st->flags &= ~ST_REMAINING;
		}
		st->flags &= ~(ST_EMPTY|ST_AT_FILEMARK|ST_NO_FILEMARK);
		st->flags |= ST_DIRTY|ST_AT_END_OF_DATA;
		if(op->code == OP_WRITE_FILEMARK)
			st->flags |= ST_AT_FILEMARK;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	case OP_TRUNCATE: /* Truncate data at current position */
		if(!(cmd_line->flags & MODE_NO_EXTRA_CHECKS)) {
			if((st->media != NULL) && st->media->WriteProtected) {
				msg_print(mf, MSG_ERROR, _T("Media is write protected.\n"));
				st->flags |= ST_ERROR;
			}
			if(st->flags & ST_AT_END_OF_DATA) {
				msg_print(mf, MSG_WARNING, _T("Suspicious truncation when already at the end of the data.\n"));
				st->flags |= ST_WARNING;
			}
		}
		if( !(cmd_line->flags & MODE_NO_OVERWRITE_CHECK) && !(st->flags & ST_AT_END_OF_DATA) ) {
			msg_print(mf, MSG_WARNING, _T("Truncation will destroy existing data on the media.\n"));
			st->flags |= ST_OVERWRITE;
		}
		st->flags &= ~ST_REMAINING;
		st->flags |= ST_AT_END_OF_DATA;
		*p_media_required = st->flags & ST_UNLOADED;
		break;
	}
}

/* ---------------------------------------------------------------------------------------------- */

/* Check operations list before execution and show details to the user */
int check_tape_operations(
	struct msg_filter *mf, struct cmd_line_args *cmd_line, HANDLE h_tape,
	TAPE_GET_DRIVE_PARAMETERS *drive, TAPE_GET_MEDIA_PARAMETERS *media)
{
	DWORD error, part, pos_low, pos_high;
	struct cmd_sim_state st;
	unsigned int op_index;
	struct tape_operation *op;
	int success = 1, media_required;

	memset(&st, 0, sizeof(st));
	st.drive = drive;
	st.media = media;

	/* Initialize check flags */
	if(drive != NULL)
	{
		/* Check for single partition */
		if(drive->MaximumPartitionCount <= 1)
			st.flags |= ST_SINGLE_PARTITION;
	}
	if(media != NULL)
	{
		/* Check for single partition */
		if(media->PartitionCount == 1)
			st.flags |= ST_SINGLE_PARTITION;
		/* Check full capacity */
		if( check_feature(&st, TAPE_DRIVE_TAPE_CAPACITY) &&
			(media->Capacity.QuadPart != 0) )
		{
			st.capacity = media->Capacity.QuadPart;
			st.flags |= ST_CAPACITY;
		}
		/* Check remaining capacity (don't trust when drive reports 100% remaining) */
		if( check_feature(&st, TAPE_DRIVE_TAPE_REMAINING) &&
			(media->Remaining.QuadPart != media->Capacity.QuadPart) )
		{
			st.remaining = media->Remaining.QuadPart;
			st.flags |= ST_REMAINING;
		}
		/* Check for empty media */
		if((st.flags & ST_CAPACITY) && (st.flags & ST_REMAINING) && (st.remaining == st.capacity))
		{
			st.flags |= ST_EMPTY|ST_POSITION|ST_AT_END_OF_DATA;
			st.position = 0;
		}
		/* Check for beginning of media */
		if( !(st.flags & ST_POSITION) && (h_tape != INVALID_HANDLE_VALUE) &&
			 check_feature(&st, TAPE_DRIVE_GET_ABSOLUTE_BLK) )
		{
			error = GetTapePosition(h_tape, TAPE_ABSOLUTE_POSITION, &part, &pos_low, &pos_high);
			if((error == NO_ERROR) && (pos_low == 0) && (pos_high == 0)) {
				st.flags |= ST_POSITION;
				st.position = 0;
			}
		}
		st.flags |= ST_LOADED;
	}
	else
	{
		st.flags |= ST_UNLOADED;
	}

	/* Display operation list and validate operations */
	op_index = 0;
	if((cmd_line->flags & MODE_SHOW_OPERATIONS) && (cmd_line->op_count != 0)) {
		msg_print(mf, MSG_VERBOSE, _T("\n"));
		msg_print(mf, MSG_MESSAGE, _T("List of operations to be executed:\n"));
	}
	for(op = cmd_line->op_list; op != NULL; op = op->next)
	{
		/* List operations if requested */
		if(cmd_line->flags & MODE_SHOW_OPERATIONS) {
			if(cmd_line->op_count >= 10) {
				msg_print(mf, MSG_MESSAGE, _T("[%2u/%2u] "),
					op_index + 1, cmd_line->op_count);
			} else if(cmd_line->op_count >= 2) {
				msg_print(mf, MSG_MESSAGE, _T("[%u/%u] "),
					op_index + 1, cmd_line->op_count);
			}
			tape_operation_info(mf, op);
		}

		/* Check operation parameters */
		tape_operation_check(mf, cmd_line, &st, op, &media_required);

		/* Display warning for ejected media */
		if(media_required && !(cmd_line->flags & MODE_NO_EXTRA_CHECKS)) {
			msg_print(mf, MSG_ERROR, _T("Operation requires media to be loaded into the drive.\n"));
			st.flags |= ST_ERROR;
		}

		/* Update check flags */
		if( (st.flags & ST_EMPTY) || /* check for empty media */
			((st.flags & ST_AT_END_OF_DATA) && (st.flags & ST_POSITION) && (st.position == 0)) ||
			((st.flags & ST_CAPACITY) && (st.flags & ST_REMAINING) && (st.capacity == st.remaining)) )
		{
			st.flags &= ~(ST_DIRTY|ST_NO_FILEMARK|ST_AT_FILEMARK);
			st.flags |= ST_EMPTY|ST_POSITION|ST_AT_END_OF_DATA;
			st.position = 0;
		}
		if((st.flags & ST_SINGLE_PARTITION) && (st.flags & ST_AT_END_OF_DATA)) /* check for end of data */
		{
			if((st.flags & ST_CAPACITY) && (st.flags & ST_REMAINING) && !(st.flags & ST_POSITION))
				st.position = st.capacity - st.remaining;
			if((st.flags & ST_CAPACITY) && !(st.flags & ST_REMAINING) && (st.flags & ST_POSITION))
				st.remaining = st.capacity - st.position;
			if(!(st.flags & ST_CAPACITY) && (st.flags & ST_REMAINING) && (st.flags & ST_POSITION))
				st.capacity = st.remaining + st.position;
		}

		op_index++;
	}

	/* Output simulation result */
	if(!(cmd_line->flags & MODE_TEST)) {
		if(st.flags & ST_ERROR) {
			msg_print(mf, MSG_INFO, _T("You can skip some checks with -y if you sure...\n"));
			success = 0;
		} else if( (st.flags & ST_WARNING) || ((st.flags & ST_OVERWRITE) && (cmd_line->flags & MODE_PROMPT_OVERWRITE)) ) {
			success = prompt(_T("Would you like to continue?"), !(st.flags & ST_WARNING));
		} else if(cmd_line->flags & MODE_SHOW_OPERATIONS) {
			success = prompt(_T("Would you like to continue?"), 1);
		} else if(st.flags & ST_OVERWRITE) {
			success = prompt_with_countdown(_T("Would you like to continue?"));
		}
	} else {
		if(!(st.flags & (ST_ERROR|ST_WARNING|ST_OVERWRITE))) {
			msg_print(mf, MSG_MESSAGE, 
				_T("%u supplied command%s seems to be correct.\n"),
				cmd_line->op_count, (cmd_line->op_count == 1) ? _T("") : _T("s"));
		}
		success = 0;
	}

	return success;
}

/* ---------------------------------------------------------------------------------------------- */
