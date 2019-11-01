/* ---------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include "util/fmt.h"
#include "cmdexec.h"

/* ---------------------------------------------------------------------------------------------- */

/* Initialize TAPE_SET_DRIVE_PARAMETERS with current settings */
static int get_tape_parameters(
	struct msg_filter *mf, HANDLE h_tape, TAPE_SET_DRIVE_PARAMETERS *p_tsdp)
{
	TAPE_GET_DRIVE_PARAMETERS tgdp;
	DWORD size, error;

	msg_print(mf, MSG_VERY_VERBOSE, _T("Querying drive information...\n"));
	size = sizeof(TAPE_GET_DRIVE_PARAMETERS);
	if( (error = GetTapeParameters(h_tape, GET_TAPE_DRIVE_INFORMATION, &size, &tgdp)) != NO_ERROR )
	{
		msg_print(mf, MSG_ERROR, _T("Can't get current drive settings: %s (%u).\n"),
			msg_winerr(mf, error), error);
		return 0;
	}

	p_tsdp->ECC = tgdp.ECC;
	p_tsdp->Compression = tgdp.Compression;
	p_tsdp->DataPadding = tgdp.DataPadding;
	p_tsdp->ReportSetmarks = tgdp.ReportSetmarks;
	p_tsdp->EOTWarningZoneSize = tgdp.EOTWarningZoneSize;

	return 1;
}

/* List current position command */
static int list_current_position(
	struct msg_filter *mf, HANDLE h_tape, TAPE_GET_DRIVE_PARAMETERS *drive)
{
	DWORD partition = 0, error = ERROR_NOT_SUPPORTED;
	int have_absolute_pos = 0, have_logical_pos = 0;
	unsigned __int64 pos_absolute = 0, pos_logical = 0;

	/* Query absolute position if supported */
	if((drive == NULL) || (drive->FeaturesLow & TAPE_DRIVE_GET_ABSOLUTE_BLK))
	{
		DWORD dw, pos_low, pos_high;
		error = GetTapePosition(h_tape, TAPE_ABSOLUTE_POSITION, &dw, &pos_low, &pos_high);
		if(error != NO_ERROR) {
			if(drive != NULL) {
				msg_print(mf, MSG_ERROR, _T("Can't get absolute position: %s (%u).\n"),
					msg_winerr(mf, error), error);
				return 0;
			}
		} else {
			pos_absolute = (unsigned __int64)pos_low | ((unsigned __int64)pos_high << 32);
			have_absolute_pos = 1;
		}
	}

	/* Query logical position if supported */
	if((drive == NULL) || (drive->FeaturesLow & TAPE_DRIVE_GET_LOGICAL_BLK))
	{
		DWORD pos_low, pos_high;
		error = GetTapePosition(h_tape, TAPE_LOGICAL_POSITION, &partition, &pos_low, &pos_high);
		if(error != NO_ERROR) {
			if(drive != NULL) {
				msg_print(mf, MSG_ERROR, _T("Can't get logical position: %s (%u).\n"),
					msg_winerr(mf, error), error);
				return 0;
			}
		} else {
			pos_logical = (unsigned __int64)pos_low | ((unsigned __int64)pos_high << 32);
			have_logical_pos = 1;
		}
	}


	/* Check for error */
	if(!have_absolute_pos && !have_logical_pos) {
		msg_print(mf, MSG_ERROR, _T("Can't get position: %s (%u).\n"),
			msg_winerr(mf, error), error);
		return 0;
	}

	/* Display only absolute position for single partition */
	if((drive != NULL) && (drive->MaximumPartitionCount <= 1))
	{
		if(have_absolute_pos && have_logical_pos && (pos_absolute == pos_logical))
			have_logical_pos = 0;
		if(have_logical_pos && !have_absolute_pos && (partition <= 1)) {
			pos_absolute = pos_logical;
			have_logical_pos = 0;
		}
	}

	/* Display current position */
	if(have_absolute_pos && have_logical_pos) {
		msg_print(mf, MSG_MESSAGE, _T("At block %u.%I64u (absolute: %I64u).\n"),
			partition, pos_logical, pos_absolute);
	} else if(have_logical_pos) {
		msg_print(mf, MSG_MESSAGE, _T("At block %u.%I64u.\n"), partition, pos_logical);
	} else {
		msg_print(mf, MSG_MESSAGE, _T("At block %I64u.\n"), pos_absolute);
	}

	return 1;
}

int tape_operation_execute(
	struct msg_filter *mf,
	struct tape_io_ctx *io_ctx,
	struct tape_operation *op,
	HANDLE h_tape,
	TAPE_GET_DRIVE_PARAMETERS *drive)
{
	int success = 0;

	switch(op->code)
	{
		/* Drive commands */
		case OP_SET_COMPRESSION: /* Enable or disable data compression */
		{
			TAPE_SET_DRIVE_PARAMETERS tsdp;
			msg_print(mf, MSG_INFO, _T("%s data compression..."),
				op->enable ? _T("Enabling") : _T("Disabling"));
			if(get_tape_parameters(mf, h_tape, &tsdp)) {
				DWORD error;
				tsdp.Compression = (BOOLEAN)(op->enable);
				error = SetTapeParameters(h_tape, SET_TAPE_DRIVE_INFORMATION, &tsdp);
				if(error != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't %s data compression: %s (%u).\n"),
						op->enable ? _T("enable") : _T("disable"), msg_winerr(mf, error), error);
				} else {
					msg_print(mf, MSG_INFO, _T(" OK\n"));
					success = 1;
				}
			}
			break;
		}
		case OP_SET_DATA_PADDING: /* Enable or disable data padding */
		{
			TAPE_SET_DRIVE_PARAMETERS tsdp;
			msg_print(mf, MSG_INFO, _T("%s data padding..."),
				op->enable ? _T("Enabling") : _T("Disabling"));
			if(get_tape_parameters(mf, h_tape, &tsdp)) {
				DWORD error;
				tsdp.DataPadding = (BOOLEAN)(op->enable);
				error = SetTapeParameters(h_tape, SET_TAPE_DRIVE_INFORMATION, &tsdp);
				if(error != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't %s data padding: %s (%u).\n"),
						op->enable ? _T("enable") : _T("disable"), msg_winerr(mf, error), error);
				} else {
					msg_print(mf, MSG_INFO, _T(" OK\n"));
					success = 1;
				}
			}
			break;
		}
		case OP_SET_ECC: /* Enable or disable ECC */
		{
			TAPE_SET_DRIVE_PARAMETERS tsdp;
			msg_print(mf, MSG_INFO, _T("%s ECC..."),
				op->enable ? _T("Enabling") : _T("Disabling"));
			if(get_tape_parameters(mf, h_tape, &tsdp)) {
				DWORD error;
				tsdp.ECC = (BOOLEAN)(op->enable);
				error = SetTapeParameters(h_tape, SET_TAPE_DRIVE_INFORMATION, &tsdp);
				if(error != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't %s ECC: %s (%u).\n"),
						op->enable ? _T("enable") : _T("disable"), msg_winerr(mf, error), error);
				} else {
					msg_print(mf, MSG_INFO, _T(" OK\n"));
					success = 1;
				}
			}
			break;
		}
		case OP_SET_REPORT_SETMARKS: /* Enable or disable setmark reporting */
		{
			TAPE_SET_DRIVE_PARAMETERS tsdp;
			msg_print(mf, MSG_INFO, _T("%s setmark reporting..."),
				op->enable ? _T("Enabling") : _T("Disabling"));
			if(get_tape_parameters(mf, h_tape, &tsdp)) {
				DWORD error;
				tsdp.ReportSetmarks = (BOOLEAN)(op->enable);
				error = SetTapeParameters(h_tape, SET_TAPE_DRIVE_INFORMATION, &tsdp);
				if(error != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't %s data setmark reporting: %s (%u).\n"),
						op->enable ? _T("enable") : _T("disable"), msg_winerr(mf, error), error);
				} else {
					msg_print(mf, MSG_INFO, _T(" OK\n"));
					success = 1;
				}
			}
			break;
		}
		case OP_SET_EOT_WARNING_ZONE: /* Set EOT warning zone size */
		{
			TAPE_SET_DRIVE_PARAMETERS tsdp;
			TCHAR size_str[64];
			msg_print(mf, MSG_INFO, _T("Setting EOT warning zone size to %s..."),
				fmt_block_size(size_str, op->size, 2));
			if(get_tape_parameters(mf, h_tape, &tsdp)) {
				DWORD error;
				tsdp.EOTWarningZoneSize = (DWORD)(op->size);
				error = SetTapeParameters(h_tape, SET_TAPE_DRIVE_INFORMATION, &tsdp);
				if(error != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't set EOT warning zone size: %s (%u).\n"),
						msg_winerr(mf, error), error);
				} else {
					msg_print(mf, MSG_INFO, _T(" OK\n"));
					success = 1;
				}
			}
			break;
		}
		case OP_SET_BLOCK_SIZE: /* Set block size */
		{
			TAPE_SET_MEDIA_PARAMETERS tsmp;
			DWORD error;
			TCHAR size_str[64];
			msg_print(mf, MSG_INFO, _T("Setting block size to %s..."),
				fmt_block_size(size_str, op->size, 2));
			tsmp.BlockSize = (DWORD)(op->size);
			error = SetTapeParameters(h_tape, SET_TAPE_MEDIA_INFORMATION, &tsmp);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't set block size: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				msg_print(mf, MSG_INFO, _T(" OK\n"));
				success = 1;
			}
			break;
		}
		case OP_LOCK_TAPE_EJECT: /* Lock media ejection */
		case OP_UNLOCK_TAPE_EJECT: /* Unlock media ejection */
		{
			DWORD operation, error;
			msg_print(mf, MSG_INFO, _T("%s media ejection..."),
				(op->code == OP_LOCK_TAPE_EJECT) ? _T("Locking") : _T("Unlocking"));
			operation = (op->code == OP_LOCK_TAPE_EJECT) ? TAPE_LOCK : TAPE_UNLOCK;
			if( (error = PrepareTape(h_tape, operation, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't %s media ejection: %s (%u).\n"),
					(op->code == OP_LOCK_TAPE_EJECT) ? _T("lock") : _T("unlock"),
					msg_winerr(mf, error), error);
			} else {
				msg_print(mf, MSG_INFO, _T(" OK\n"));
				success = 1;
			}
			break;
		}

		/* Media commands */
		case OP_LOAD_MEDIA: /* Load media */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Loading media into the drive..."));
			begin = GetTickCount();
			if( (error = PrepareTape(h_tape, TAPE_LOAD, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't load media: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_UNLOAD_MEDIA: /* Unload media */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Unloading media from the drive..."));
			begin = GetTickCount();
			if( (error = PrepareTape(h_tape, TAPE_UNLOAD, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't unload media: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
	//	case OP_MAKE_PARTITION: /* Create tape partition */
	//		break;
		case OP_ERASE_TAPE: /* Erase media */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Erasing media..."));
			begin = GetTickCount();
			if( (error = EraseTape(h_tape, TAPE_ERASE_LONG, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't erase media: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_LIST_TAPE_CAPACITY: /* Show media capacity */
		{
			TAPE_GET_MEDIA_PARAMETERS tgmp;
			DWORD size = sizeof(tgmp), error;
			error = GetTapeParameters(h_tape, GET_TAPE_MEDIA_INFORMATION, &size, &tgmp);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_ERROR, _T("Can't get media capacity: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				DWORD operation;
				TCHAR size_str1[64], size_str2[64];
				operation = 0;
				if( ((drive == NULL) || (drive->FeaturesLow & TAPE_DRIVE_TAPE_CAPACITY)) &&
					(tgmp.Capacity.QuadPart != 0) )
				{
					operation |= TAPE_DRIVE_TAPE_CAPACITY;
				}
				if( ((drive == NULL) || (drive->FeaturesLow & TAPE_DRIVE_TAPE_REMAINING)) &&
					(tgmp.Remaining.QuadPart != 0) )
				{
					operation |= TAPE_DRIVE_TAPE_REMAINING;
				}
				switch(operation) {
				case TAPE_DRIVE_TAPE_CAPACITY:
					msg_print(mf, MSG_MESSAGE, _T("Full capacity: %s.\n"),
						fmt_block_size(size_str1, tgmp.Capacity.QuadPart, 1));
					break;
				case TAPE_DRIVE_TAPE_REMAINING:
					msg_print(mf, MSG_MESSAGE, _T("Remaining capacity: %s.\n"),
						fmt_block_size(size_str1, tgmp.Remaining.QuadPart, 1));
					break;
				case TAPE_DRIVE_TAPE_CAPACITY|TAPE_DRIVE_TAPE_REMAINING:
					msg_print(mf, MSG_MESSAGE, _T("%s of %s (%.2f%%) free.\n"),
						fmt_block_size(size_str1, tgmp.Remaining.QuadPart, 1),
						fmt_block_size(size_str2, tgmp.Capacity.QuadPart, 1),
						100.0 * tgmp.Remaining.QuadPart / tgmp.Capacity.QuadPart);
					break;
				}
				success = 1;
			}
			break;
		}
		case OP_TAPE_TENSION: /* Tension tape */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Tensioning tape..."));
			begin = GetTickCount();
			if( (error = PrepareTape(h_tape, TAPE_TENSION, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't tension tape: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"),
					fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}

		/* Navigation */
		case OP_LIST_CURRENT_POSITION: /* List current position */
		{
			success = list_current_position(mf, h_tape, drive);
			break;
		}
		case OP_MOVE_TO_ORIGIN: /* Move to origin (rewind tape) */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Rewinding..."));
			begin = GetTickCount();
			if( (error = SetTapePosition(h_tape, TAPE_REWIND, 0, 0, 0, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't rewind: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_MOVE_TO_EOD: /* Move to end of data */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Seeking to end of data..."));
			begin = GetTickCount();
			error = SetTapePosition(h_tape, TAPE_SPACE_END_OF_DATA, 0, 0, 0, FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to the end of data: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_SET_ABS_POSITION: /* Move to absolute position */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Seeking to absolute block %I64u..."), op->count);
			begin = GetTickCount();
			error = SetTapePosition(h_tape, TAPE_ABSOLUTE_BLOCK, 0,
				(DWORD)(op->count), (DWORD)(op->count >> 32), FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to the absolute position: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_SET_TAPE_POSITION: /* Move to logical position */
		{
			DWORD error, begin, elapsed;
			if(op->partition != 0) {
				msg_print(mf, MSG_INFO, _T("Seeking to block %u.%I64u..."),
					op->partition, op->count);
			} else {
				msg_print(mf, MSG_INFO, _T("Seeking to block %I64u..."),
					op->count);
			}
			begin = GetTickCount();
			error = SetTapePosition(h_tape, TAPE_LOGICAL_BLOCK, op->partition,
				(DWORD)(op->count), (DWORD)(op->count >> 32), FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to block: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_MOVE_BLOCK_NEXT: /* Move to next block */
		case OP_MOVE_BLOCK_PREV: /* Move to previous block */
		{
			DWORD error, begin, elapsed;
			unsigned __int64 offset;
			msg_print(mf, MSG_INFO, _T("Seeking %I64u block%s %s..."),
				op->count, (op->count == 1) ? _T("") : _T("s"),
				(op->code == OP_MOVE_BLOCK_NEXT) ? _T("forward") : _T("backward"));
			begin = GetTickCount();
			offset = (op->code == OP_MOVE_BLOCK_NEXT) ? 
				op->count : (unsigned __int64)(-(__int64)(op->count));
			error = SetTapePosition(h_tape, TAPE_SPACE_RELATIVE_BLOCKS, 0,
				(DWORD)offset, (DWORD)(offset >> 32), FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to relative block: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_MOVE_FILE_NEXT: /* Move to file forward */
		case OP_MOVE_FILE_PREV: /* Move to file backward */
		{
			DWORD error, begin, elapsed;
			unsigned __int64 offset;
			msg_print(mf, MSG_INFO, _T("Seeking %I64u filemark%s %s..."),
				op->count, (op->count == 1) ? _T("") : _T("s"),
				(op->code == OP_MOVE_FILE_NEXT) ? _T("forward") : _T("backward"));
			begin = GetTickCount();
			offset = (op->code == OP_MOVE_FILE_NEXT) ?
				op->count : (unsigned __int64)(-(__int64)(op->count));
			error = SetTapePosition(h_tape, TAPE_SPACE_FILEMARKS, 0,
				(DWORD)offset, (DWORD)(offset >> 32), FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to filemark: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_MOVE_SMK_NEXT: /* Move to setmark forward */
		case OP_MOVE_SMK_PREV: /* Move to setmark backward */
		{
			DWORD error, begin, elapsed;
			unsigned __int64 offset;
			msg_print(mf, MSG_INFO, _T("Seeking %I64u setmark%s %s..."),
				op->count, (op->count == 1) ? _T("") : _T("s"),
				(op->code == OP_MOVE_SMK_NEXT) ? _T("forward") : _T("backward"));
			begin = GetTickCount();
			offset = (op->code == OP_MOVE_SMK_NEXT) ?
				op->count : (unsigned __int64)(-(__int64)(op->count));
			error = SetTapePosition(h_tape, TAPE_SPACE_SETMARKS, 0,
				(DWORD)offset, (DWORD)(offset >> 32), FALSE);
			if(error != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't seek to setmark: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}

		/* Read/Write */
		case OP_READ_DATA: /* Read files from media */
		{
			msg_print(mf, MSG_INFO, _T("Reading data to \"%s\"...\n"), op->filename);
			success = tape_file_read(mf, io_ctx, h_tape, op->filename);
			break;
		}
		case OP_WRITE_DATA: /* Write files to media */
		case OP_WRITE_DATA_AND_FMK: /* Write files and filemarks */
		{
			msg_print(mf, MSG_INFO, _T("Writing data from \"%s\"...\n"), op->filename);
			success = tape_file_write(mf, io_ctx, h_tape, op->filename);
			if(success && (op->code == OP_WRITE_DATA_AND_FMK)) {
				DWORD error, begin, elapsed;
				msg_print(mf, MSG_INFO, _T("Writing filemark..."));
				begin = GetTickCount();
				if((error = WriteTapemark(h_tape, TAPE_FILEMARKS, 1, FALSE)) != NO_ERROR) {
					msg_print(mf, MSG_INFO, _T("\n"));
					msg_print(mf, MSG_ERROR, _T("Can't write filemark: %s (%u).\n"),
						msg_winerr(mf, error), error);
					success = 0;
				} else {
					TCHAR elapsed_str[64];
					elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
					msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				}
			}
			break;
		}

		case OP_WRITE_FILEMARK: /* Write filemarks */
		case OP_WRITE_SETMARK: /* Write setmarks */
		{
			DWORD operation, error, begin, elapsed;
			if(op->count == 1) {
				msg_print(mf, MSG_INFO, _T("Writing %s..."),
					(op->code == OP_WRITE_FILEMARK) ? _T("filemark") : _T("setmark"));
			} else {
				msg_print(mf, MSG_INFO, _T("Writing %I64u %s..."),
					op->count, (op->code == OP_WRITE_FILEMARK) ? _T("filemarks") : _T("setmarks"));
			}
			begin = GetTickCount();
			operation = (op->code == OP_WRITE_FILEMARK) ? TAPE_FILEMARKS : TAPE_SETMARKS;
			if((error = WriteTapemark(h_tape, operation, (DWORD)(op->count), FALSE)) != NO_ERROR) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't write %s: %s (%u).\n"),
					(op->code == OP_WRITE_FILEMARK) ? _T("filemark") : _T("setmark"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}
		case OP_TRUNCATE: /* Truncate data at current position */
		{
			DWORD error, begin, elapsed;
			msg_print(mf, MSG_INFO, _T("Truncating at current position..."));
			begin = GetTickCount();
			if( (error = EraseTape(h_tape, TAPE_ERASE_SHORT, FALSE)) != NO_ERROR ) {
				msg_print(mf, MSG_INFO, _T("\n"));
				msg_print(mf, MSG_ERROR, _T("Can't truncate: %s (%u).\n"),
					msg_winerr(mf, error), error);
			} else {
				TCHAR elapsed_str[64];
				elapsed = (GetTickCount() - begin + 500UL) / 1000UL;
				msg_print(mf, MSG_INFO, _T(" %s OK\n"), fmt_elapsed_time(elapsed_str, elapsed, 0));
				success = 1;
			}
			break;
		}

		default:
		{
			msg_print(mf, MSG_ERROR, _T("Operation (%d) is not implemented.\n"), op->code);
			break;
		}
	}
	return success;
}

/* ---------------------------------------------------------------------------------------------- */
