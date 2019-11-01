/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <tchar.h>
#include <crtdbg.h>
#include "util/msgfilt.h"
#include "util/getpath.h"
#include "cmdline.h"
#include "drvinfo.h"
#include "cmdcheck.h"
#include "cmdexec.h"
#include "config.h"

/* ---------------------------------------------------------------------------------------------- */

static void load_default_settings(struct cmd_line_args *cmd_line)
{
	memset(cmd_line, 0, sizeof(struct cmd_line_args));

	_tcscpy(cmd_line->tape_device, DEFAULT_TAPE_NAME);
	cmd_line->buffer_size = DEFAULT_BUFFER_SIZE;
	cmd_line->io_block_size = DEFAULT_IO_BLOCK_SIZE;
	cmd_line->io_queue_size = DEFAULT_IO_QUEUE_SIZE;
	cmd_line->next_op_ptr = &(cmd_line->op_list);
}

static int check_settings(struct msg_filter *mf, struct cmd_line_args *cmd_line)
{
	int success = 1;

	/* Check buffer size */
	if(cmd_line->buffer_size < MIN_BUFFER_SIZE) {
		msg_print(mf, MSG_ERROR, _T("Buffer too small. Use a few megabytes at least!\n"));
		success = 0;
	}
	if(cmd_line->buffer_size < MIN_BUFFER_BLOCKS * cmd_line->io_block_size) {
		msg_print(mf, MSG_ERROR, _T("Buffer too small. Use 4X I/O block size at least!\n"));
		success = 0;
	}

	/* Check I/O queue size */
	if(cmd_line->io_block_size < MIN_IO_BLOCK_SIZE) {
		msg_print(mf, MSG_ERROR, _T("I/O block too small. Use a few kilobytes at least!\n"));
		success = 0;
	}
	if(cmd_line->io_block_size > MAX_IO_BLOCK_SIZE) {
		msg_print(mf, MSG_ERROR, _T("I/O block too big. Choose some reasonable value!\n"));
		success = 0;
	}

	/* Check queue length */
	if(cmd_line->io_queue_size > MAX_IO_QUEUE_SIZE) {
		msg_print(mf, MSG_ERROR, _T("I/O queue too big. Choose some realistic value!\n"));
		success = 0;
	}

	return success;
}

/* ---------------------------------------------------------------------------------------------- */

int main()
{
	struct msg_filter mf;
	struct cmd_line_args cmd_line;
	int success = 1;

	_tsetlocale(LC_COLLATE, _T(".OCP"));
	_tsetlocale(LC_CTYPE, _T(".OCP"));

	msg_init(&mf);

	load_default_settings(&cmd_line);

	/* Display welcome message and parse command line */
	msg_append(&mf, MSG_VERBOSE, _T("redsh's tape drive controller - version ") VERSION _T("\n"));
	if(!parse_command_line(&mf, &cmd_line))
		success = 0;
	msg_flush(&mf);

	if(cmd_line.flags & MODE_SHOW_HELP)
	{
		if(mf.report_level >= MSG_VERBOSE)
			_putts(_T(""));
		usage_help(&mf);
	}

	if(success && !(cmd_line.flags & MODE_EXIT))
	{
		HANDLE h_tape = INVALID_HANDLE_VALUE;
		TAPE_GET_DRIVE_PARAMETERS drive;
		TAPE_GET_MEDIA_PARAMETERS media;
		int have_drive_info = 0;
		int have_media_info = 0;

		success = check_settings(&mf, &cmd_line);

		/* Opening device if ... */
		if( success && 
			( !(cmd_line.flags & MODE_TEST) ||				/* normal command execution */
			  !(cmd_line.flags & MODE_NO_EXTRA_CHECKS) ||	/* or extra checks required */
			  (cmd_line.flags & MODE_LIST_DRIVE_INFO) ) )	/* or listing drive info */
		{
			unsigned int open_flags = 0;

			/* Open device */
			if(!(cmd_line.flags & MODE_WINDOWS_BUFFERING))
				open_flags |= FILE_FLAG_NO_BUFFERING;
			if(cmd_line.io_queue_size != 0)
				open_flags |= FILE_FLAG_OVERLAPPED;
			msg_print(&mf, MSG_VERY_VERBOSE,
				_T("Opening device (\"%s\", GENERIC_READ|GENERIC_WRITE, 0, OPEN_EXISTING, 0x%08X)\n"),
				cmd_line.tape_device, open_flags);
			h_tape = CreateFile(cmd_line.tape_device, GENERIC_READ|GENERIC_WRITE,
				0, NULL, OPEN_EXISTING, open_flags, NULL);

			/* Check for errors */
			if(h_tape == INVALID_HANDLE_VALUE)
			{
				DWORD error = GetLastError();
				switch(error)
				{
				case ERROR_FILE_NOT_FOUND:
					msg_print(&mf, MSG_ERROR, _T("Can't open \"%s\": device not found!\n"),
						cmd_line.tape_device);
					break;
				case ERROR_ACCESS_DENIED:
					msg_print(&mf, MSG_ERROR, _T("Can't open \"%s\": access denied!\n"),
						cmd_line.tape_device);
					break;
				case ERROR_SHARING_VIOLATION:
					msg_print(&mf, MSG_ERROR, _T("Can't open \"%s\": locked by another program!\n"),
						cmd_line.tape_device);
					break;
				default:
					msg_print(&mf, MSG_ERROR, _T("Can't open \"%s\": %s (%u).\n"),
						cmd_line.tape_device, msg_winerr(&mf, error), error);
					break;
				}
				success = 0;
			}
		}

		/* Query drive and media information if ... */
		if( (h_tape != INVALID_HANDLE_VALUE) &&
			( !(cmd_line.flags & MODE_NO_EXTRA_CHECKS) ||	/* extra checks required */
			  (cmd_line.flags & MODE_LIST_DRIVE_INFO) )	)	/* or listing drive info */
		{
			DWORD size, error;
			
			/* Get drive information */
			msg_print(&mf, MSG_VERY_VERBOSE, _T("Querying drive information...\n"));
			size = sizeof(drive);
			error = GetTapeParameters(h_tape, GET_TAPE_DRIVE_INFORMATION, &size, &drive);
			if(error == 0) {
				have_drive_info = 1;
			} else {
				msg_print(&mf, MSG_ERROR, _T("Can't get drive information: %s (%u).\n"),
					msg_winerr(&mf, error), error);
				success = 0;
			}

			/* Get media information */
			msg_print(&mf, MSG_VERY_VERBOSE, _T("Querying media information...\n"));
			size = sizeof(media);
			error = GetTapeParameters(h_tape, GET_TAPE_MEDIA_INFORMATION, &size, &media);
			if(error == 0) {
				have_media_info = 1;
			} else if(error != ERROR_NO_MEDIA_IN_DRIVE) {
				msg_print(&mf, MSG_ERROR, _T("Can't get media information: %s (%u).\n"),
					msg_winerr(&mf, error), error);
				success = 0;
			}
		}

		/* List drive information if requested */
		if(have_drive_info && (cmd_line.flags & MODE_LIST_DRIVE_INFO))
		{
			msg_print(&mf, MSG_VERBOSE, _T("\n"));
			list_drive_info(&mf, &drive,
				cmd_line.flags & MODE_VERBOSE,
				cmd_line.flags & MODE_VERY_VERBOSE);

			if(have_media_info) {
				_putts(_T(""));
				list_media_info(&mf, &drive, &media);
			} else {
				_putts(_T("\nNo media loaded."));
			}
			if((cmd_line.op_count != 0) && !(cmd_line.flags & MODE_SHOW_OPERATIONS))
				_putts(_T(""));
		}

		/* Check operation list, show list to the user and get confirmation if needed */
		success = success && check_tape_operations(&mf, &cmd_line, h_tape, 
			have_drive_info ? &drive : NULL, have_media_info ? &media : NULL);

		/* Execute requested tape operations if ...*/
		if( success &&							/* tape drive opened and operation list valid */
			(cmd_line.op_count != 0) &&			/* and operation list is not empty */
			!(cmd_line.flags & MODE_TEST) )		/* and not in dry run mode */
		{
			unsigned int op_index, op_remaining;
			struct tape_io_ctx io_ctx;
			struct tape_operation *op;
			int use_io_buffer;

			/* Allocate data buffer for read/write operations */
			use_io_buffer = 0;
			for(op = cmd_line.op_list; op != NULL; op = op->next)
			{
				if( (op->code == OP_READ_DATA) ||
					(op->code == OP_WRITE_DATA) ||
					(op->code == OP_WRITE_DATA_AND_FMK) )
				{
					use_io_buffer = 1;
					break;
				}
			}

			if(use_io_buffer)
			{
				success = tape_io_init_buffer(&mf, &io_ctx,
					cmd_line.buffer_size,
					cmd_line.io_block_size,
					cmd_line.io_queue_size, 
					(cmd_line.flags & MODE_WINDOWS_BUFFERING) ? 1 : 0);
			}

			if(success)
			{
				/* Execute operations */
				if(cmd_line.op_count > 1) {
					msg_print(&mf,
						(cmd_line.flags & MODE_SHOW_OPERATIONS) ? MSG_MESSAGE : MSG_VERBOSE,
						_T("\nExecuting %u operations...\n"), cmd_line.op_count);
				}

				op_index = 0;
				for(op = cmd_line.op_list; op != NULL; op = op->next)
				{
					/* Print operation number */
					if(cmd_line.op_count >= 10) {
						msg_print(&mf, MSG_INFO, _T("[%2u/%2u] "),
							op_index + 1, cmd_line.op_count);
					} else if(cmd_line.op_count >= 2) {
						msg_print(&mf, MSG_INFO, _T("[%u/%u] "),
							op_index + 1, cmd_line.op_count);
					}

					/* Execute operation */
					if( ! tape_operation_execute(
						&mf,
						use_io_buffer ? &io_ctx : NULL,
						op,
						h_tape,
						have_drive_info ? &drive : NULL) )
					{
						success = 0;
						break;
					}
					
					op_index++;
				}

				/* Display number of cancelled operations */
				op_remaining = cmd_line.op_count - op_index;
				if(op_remaining >= 2) {
					msg_print(&mf, MSG_INFO,
						_T("Execution of %u subsequent operation%s was cancelled.\n"),
						op_remaining - 1, (op_remaining == 2) ? _T("") : _T("s"));
				}

				/* Free data buffer */
				if(use_io_buffer)
					tape_io_cleanup(&io_ctx);
			}
		}

		/* Close device */
		if(h_tape != INVALID_HANDLE_VALUE)
			CloseHandle(h_tape);
	}

	/* Cleanup */
	free_cmd_line_args(&cmd_line);
	msg_free(&mf);
	_CrtDumpMemoryLeaks();

	return (success ? 0 : 1);
}

/* ---------------------------------------------------------------------------------------------- */
