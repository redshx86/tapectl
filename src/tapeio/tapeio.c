/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include "../config.h"
#include "../util/fmt.h"
#include "../util/prompt.h"
#include "filecopy.h"
#include "setpriv.h"
#include "tapeio.h"

/* ---------------------------------------------------------------------------------------------- */

/* Query actual tape and drive info before starting operation */
static int get_tape_info(struct msg_filter *mf, HANDLE h_tape,
	TAPE_GET_DRIVE_PARAMETERS *p_tgdp, TAPE_GET_MEDIA_PARAMETERS *p_tgmp)
{
	DWORD error, size;

	msg_print(mf, MSG_VERY_VERBOSE, _T("Querying drive information...\n"));
	size = sizeof(TAPE_GET_DRIVE_PARAMETERS);
	if((error = GetTapeParameters(h_tape, GET_TAPE_DRIVE_INFORMATION, &size, p_tgdp)) != NO_ERROR)
	{
		msg_print(mf, MSG_ERROR, _T("Can't get drive information: %s (%u).\n"),
			msg_winerr(mf, error), error);
		return 0;
	}

	msg_print(mf, MSG_VERY_VERBOSE, _T("Querying media information...\n"));
	size = sizeof(TAPE_GET_MEDIA_PARAMETERS);
	if((error = GetTapeParameters(h_tape, GET_TAPE_MEDIA_INFORMATION, &size, p_tgmp)) != NO_ERROR)
	{
		msg_print(mf, MSG_ERROR, _T("Can't get media information: %s (%u).\n"),
			msg_winerr(mf, error), error);
		return 0;
	}

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Write file to tape */
int tape_file_write(struct msg_filter *mf, struct tape_io_ctx *ctx,
	HANDLE h_tape, const TCHAR *filename)
{
	HANDLE h_file;
	unsigned int dst_block_align, dst_block_size;
	ULARGE_INTEGER file_size;
	TAPE_GET_DRIVE_PARAMETERS drive_info;
	TAPE_GET_MEDIA_PARAMETERS media_info;
	DWORD error;
	int success;

	/* Get tape info */
	if(!get_tape_info(mf, h_tape, &drive_info, &media_info))
		return 0;

	/* Check for write protection */
	if((drive_info.FeaturesLow & TAPE_DRIVE_WRITE_PROTECT) && (media_info.WriteProtected)) {
		msg_print(mf, MSG_ERROR, _T("Can't write: media is write protected.\n"));
		return 0;
	}

	/* Use block size set or default block size */
	if(media_info.BlockSize != 0) {
		dst_block_align = media_info.BlockSize;
	} else if(drive_info.DefaultBlockSize != 0) {
		dst_block_align = drive_info.DefaultBlockSize;
	} else {
		msg_print(mf, MSG_ERROR, _T("Can't write: block size is not set.\n"));
		return 0;
	}
	dst_block_size = ((ctx->io_block_size + dst_block_align - 1) / 
		dst_block_align) * dst_block_align;

	/* Open source file */
	msg_print(mf, MSG_VERY_VERBOSE,
		_T("Opening file (\"%s\", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, 0x%08X)...\n"),
		filename, ctx->file_open_flags);
	h_file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, ctx->file_open_flags, NULL);
	if(h_file == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		msg_print(mf, MSG_ERROR, _T("Can't open \"%s\": %s (%u).\n"),
			filename, msg_winerr(mf, error), error);
		return 0;
	}

	/* Get size of file */
	file_size.LowPart = GetFileSize(h_file, &(file_size.HighPart));
	if((file_size.LowPart == INVALID_FILE_SIZE) && ((error = GetLastError()) != NO_ERROR))
	{
		msg_print(mf, MSG_ERROR, _T("Can't get size of \"%s\": %s (%u).\n"),
			filename, msg_winerr(mf, error), error);
		CloseHandle(h_file);
		return 0;
	}

	/* Write data to tape */
	success = copy_file(
		mf,
		&(ctx->cb), 
		COPY_SUSTAIN_WRITE,
		h_tape,
		ctx->io_queue_size,
		dst_block_size,
		dst_block_align,
		h_file,
		ctx->io_queue_size,
		ctx->file_block_size,
		file_size.QuadPart,
		ctx->crc_buffer_size,
		ctx->crc_block_size,
		NULL,
		NULL);

	/* Close source file */
	msg_print(mf, MSG_VERY_VERBOSE, _T("Closing file (\"%s\")...\n"), filename);
	CloseHandle(h_file);

	/* Clear archive attribute */
	if(success) {
		DWORD attr = GetFileAttributes(filename);
		if((attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_ARCHIVE))
			SetFileAttributes(filename, attr & ~FILE_ATTRIBUTE_ARCHIVE);
	}

	return success;
}

/* Read file from tape */
int tape_file_read(struct msg_filter *mf, struct tape_io_ctx *ctx,
	HANDLE h_tape, const TCHAR *filename)
{
	HANDLE h_file;
	unsigned int tape_block_align, tape_block_size;
	TAPE_GET_DRIVE_PARAMETERS drive_info;
	TAPE_GET_MEDIA_PARAMETERS media_info;
	unsigned __int64 data_size, padded_size;
	int success;

	/* Get tape info */
	if(!get_tape_info(mf, h_tape, &drive_info, &media_info))
		return 0;

	/* Use block size set or default block size */
	if(media_info.BlockSize != 0) {
		tape_block_align = media_info.BlockSize;
	} else if(drive_info.DefaultBlockSize != 0) {
		tape_block_align = drive_info.DefaultBlockSize;
	} else {
		msg_print(mf, MSG_ERROR, _T("Can't read: block size is not set.\n"));
		return 0;
	}
	tape_block_size = ((ctx->io_block_size + tape_block_align - 1) / 
		tape_block_align) * tape_block_align;

	/* Create output file */
	msg_print(mf, MSG_VERY_VERBOSE,
		_T("Creating file (\"%s\", GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, 0x%08X)...\n"),
		filename, ctx->file_open_flags);
	h_file = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ,
		NULL, CREATE_ALWAYS, ctx->file_open_flags, NULL);
	if(h_file == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		msg_print(mf, MSG_ERROR, _T("Can't create \"%s\": %s (%u).\n"),
			filename, msg_winerr(mf, error), error);
		return 0;
	}

	/* Read data from file */
	success = copy_file(
		mf,
		&(ctx->cb),
		COPY_SUSTAIN_READ|COPY_NO_PADDING_INFO,
		h_file,
		ctx->io_queue_size,
		ctx->file_block_size,
		ctx->file_block_align,
		h_tape,
		ctx->io_queue_size,
		tape_block_size,
		0,
		ctx->crc_buffer_size,
		ctx->crc_block_size,
		&data_size,
		&padded_size);

	/* Truncated padded output file */
	if(success && (data_size < padded_size))
	{
		/* Reopen file to allow unaligned positionning */
		msg_print(mf, MSG_VERY_VERBOSE, _T("Closing file (\"%s\")...\n"), filename);
		CloseHandle(h_file);

		msg_print(mf, MSG_VERY_VERBOSE,
			_T("Reopening file (\"%s\", GENERIC_WRITE, 0, OPEN_EXISTING, 0)\n"),
			filename, 0);
		h_file = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if(h_file == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			msg_print(mf, MSG_ERROR,
				_T("Can't reopen output file for size trimming: %s (%u).\n"),
				msg_winerr(mf, error), error);
			success = 0;
		}

		/* Trim padding from output file */
		if(success) {
			LARGE_INTEGER position;
			position.QuadPart = data_size;
			msg_print(mf, MSG_VERY_VERBOSE,
				_T("Trimming \"%s\" to %I64u bytes...\n"), filename, position.QuadPart);
			if( ! SetFilePointerEx(h_file, position, NULL, FILE_BEGIN) ||
				! SetEndOfFile(h_file) )
			{
				DWORD error = GetLastError();
				msg_print(mf, MSG_ERROR,
					_T("Can't trim size of output file: %s (%u).\n"),
					msg_winerr(mf, error), error);
				success = 0;
			}
		}
	}

	/* Close output file */
	if(h_file != INVALID_HANDLE_VALUE) {
		msg_print(mf, MSG_VERY_VERBOSE, _T("Closing file (\"%s\")...\n"), filename);
		CloseHandle(h_file);
	}

	/* Ask to delete invalid output file */
	if(!success && !prompt(_T("Would you like to keep the output file?"), 0)) {
		msg_print(mf, MSG_VERY_VERBOSE, _T("Deleting the file (\"%s\")...\n"), filename);
		DeleteFile(filename);
	}

	return success;
}

/* ---------------------------------------------------------------------------------------------- */

/* Initialize buffer for reading/writing to tape */
int tape_io_init_buffer(struct msg_filter *mf, struct tape_io_ctx *ctx,
	unsigned __int64 buffer_size, unsigned int io_block_size, unsigned int io_queue_size,
	int use_windows_buffering)
{
	/* Allocate buffer */
	if(buffer_size <= MAX_HEAP_BUFFER_SIZE)
	{
		/* No privilegy required */
		ctx->lock_pages_changed = 0;
		ctx->lock_pages_prev_state = 0;

		/* Allocate buffer in virutal memory */
		if(!bigbuf_init(mf, &(ctx->cb), 1, buffer_size, 0))
			return 0;
	}
	else
	{
		DWORD error;
		
		/* Enable page lock privilegy */
		msg_print(mf, MSG_VERY_VERBOSE, _T("Requesting privilegy: %s.\n"), SE_LOCK_MEMORY_NAME);

		ctx->lock_pages_changed = 1;
		if(!set_privilegy(SE_LOCK_MEMORY_NAME, 1, &(ctx->lock_pages_prev_state), &error))
		{
			msg_print(mf, MSG_ERROR, _T("Can't enable page locking privilegy: %s (%u).\n"),
				msg_winerr(mf, error), error);
			return 0;
		}

		/* Allocate buffer in userpages */
		if(!bigbuf_init(mf, &(ctx->cb), 0, buffer_size, PAGE_MAPPING_WINDOW_SIZE))
		{
			if(!ctx->lock_pages_prev_state)
				set_privilegy(SE_LOCK_MEMORY_NAME, 0, NULL, &error);
			return 0;
		}
	}

	/* Set block size */
	ctx->io_block_size = io_block_size;
	ctx->io_queue_size = io_queue_size;
	ctx->use_windows_buffering = use_windows_buffering;

	/* Use 4K aligned file access blocks */
	ctx->file_block_align = use_windows_buffering ? 0 : 0x1000;
	ctx->file_block_size = (io_block_size + 0x0FFF) & ~0x0FFF;
	ctx->file_open_flags = use_windows_buffering ? 
		FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_NO_BUFFERING;
	if(io_queue_size != 0)
		ctx->file_open_flags |= FILE_FLAG_OVERLAPPED;

	/* Choose crc buffer size about 1/4 I/O queue */
	ctx->crc_block_size = CRC_BLOCK_SIZE;
	if(ctx->io_queue_size == 0) {
		ctx->crc_buffer_size = io_block_size * 4;
	} else {
		unsigned int crc_blocks = io_queue_size / 4;
		if(crc_blocks < 4) crc_blocks = 4;
		if(crc_blocks > 64) crc_blocks = 64;
		ctx->crc_buffer_size = crc_blocks * io_block_size;
	}
	if(ctx->crc_buffer_size < MIN_CRC_BUFFER)
		ctx->crc_buffer_size = MIN_CRC_BUFFER;
	if(ctx->crc_buffer_size > MAX_CRC_BUFFER)
		ctx->crc_buffer_size = MAX_CRC_BUFFER;

	return 1;
}

/* ---------------------------------------------------------------------------------------------- */

/* Free buffer */
void tape_io_cleanup(struct tape_io_ctx *ctx)
{
	bigbuf_free(&(ctx->cb));

	if(ctx->lock_pages_changed && !ctx->lock_pages_prev_state)
	{
		DWORD error;
		set_privilegy(SE_LOCK_MEMORY_NAME, 0, NULL, &error);
	}
}

/* ---------------------------------------------------------------------------------------------- */
