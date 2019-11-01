/* ---------------------------------------------------------------------------------------------- */

#include <tchar.h>
#include "util/fmt.h"
#include "drvinfo.h"

/* ---------------------------------------------------------------------------------------------- */

struct drive_feature
{
	DWORD value;
	const TCHAR *name;
	const TCHAR *description;
};

static const struct drive_feature feature[] =
{
	{ TAPE_DRIVE_FIXED_BLOCK,		_T("FIXED_BLOCK"),
		_T("The device supports fixed-length block mode.") },
	{ TAPE_DRIVE_VARIABLE_BLOCK,	_T("VARIABLE_BLOCK"),
		_T("The device supports variable-length block mode.") },
	{ TAPE_DRIVE_SET_BLOCK_SIZE,	_T("SET_BLOCK_SIZE"),
		_T("The device supports setting the size of a fixed-length logical block or setting the variable-length block mode.") },
	{ TAPE_DRIVE_GET_LOGICAL_BLK,	_T("GET_LOGICAL_BLK"),
		_T("The device provides the current logical block address (and logical tape partition).") },
	{ TAPE_DRIVE_GET_ABSOLUTE_BLK,	_T("GET_ABSOLUTE_BLK"),
		_T("The device provides the current device-specific block address.") },
	{ TAPE_DRIVE_REWIND_IMMEDIATE,	_T("REWIND_IMMEDIATE"),
		_T("The device supports immediate rewind operation.") },
	{ TAPE_DRIVE_END_OF_DATA,		_T("END_OF_DATA"),
		_T("The device moves the tape to the end-of-data marker in a partition.") },
	{ TAPE_DRIVE_ABSOLUTE_BLK,		_T("ABSOLUTE_BLK"),
		_T("The device moves the tape to a device specific block address.") },
	{ TAPE_DRIVE_ABS_BLK_IMMED,		_T("ABS_BLK_IMMED"),
		_T("The device moves the tape to a device-specific block address and returns as soon as the move begins.") },
	{ TAPE_DRIVE_LOGICAL_BLK,		_T("LOGICAL_BLK"),
		_T("The device moves the tape to a logical block address in a partition.") },
	{ TAPE_DRIVE_LOG_BLK_IMMED,		_T("LOG_BLK_IMMED"),
		_T("The device moves the tape to a logical block address in a partition and returns as soon as the move begins.") },
	{ TAPE_DRIVE_RELATIVE_BLKS,		_T("RELATIVE_BLKS"),
		_T("The device moves the tape forward (or backward) a specified number of blocks.") },
	{ TAPE_DRIVE_FILEMARKS,			_T("FILEMARKS"),
		_T("The device moves the tape forward (or backward) a specified number of filemarks.") },
	{ TAPE_DRIVE_SEQUENTIAL_FMKS,	_T("SEQUENTIAL_FMKS"),
		_T("The device moves the tape forward (or backward) to the first occurrence of a specified number of consecutive filemarks.") },
	{ TAPE_DRIVE_WRITE_FILEMARKS,	_T("WRITE_FILEMARKS"),
		_T("The device writes filemarks.") },
	{ TAPE_DRIVE_WRITE_SHORT_FMKS,	_T("WRITE_SHORT_FMKS"),
		_T("The device writes short filemarks.") },
	{ TAPE_DRIVE_WRITE_LONG_FMKS,	_T("WRITE_LONG_FMKS"),
		_T("The device writes long filemarks.") },
	{ TAPE_DRIVE_WRITE_MARK_IMMED,	_T("WRITE_MARK_IMMED"),
		_T("The device supports immediate writing of short and long filemarks.") },
	{ TAPE_DRIVE_SETMARKS,			_T("SETMARKS"),
		_T("The device moves the tape forward (or reverse) a specified number of setmarks.") },
	{ TAPE_DRIVE_REPORT_SMKS,		_T("REPORT_SMKS"),
		_T("The device supports setmark reporting.") },
	{ TAPE_DRIVE_SET_REPORT_SMKS,	_T("SET_REPORT_SMKS"),
		_T("The device enables and disables the reporting of setmarks.") },
	{ TAPE_DRIVE_WRITE_SETMARKS,	_T("WRITE_SETMARKS"),
		_T("The device writes setmarks.") },
	{ TAPE_DRIVE_SEQUENTIAL_SMKS,	_T("SEQUENTIAL_SMKS"),
		_T("The device moves the tape forward (or backward) to the first occurrence of a specified number of consecutive setmarks.") },
	{ TAPE_DRIVE_REVERSE_POSITION,	_T("REVERSE_POSITION"),
		_T("The device moves the tape backward over blocks, filemarks, or setmarks.") },
	{ TAPE_DRIVE_TAPE_CAPACITY,		_T("TAPE_CAPACITY"),
		_T("The device returns the maximum capacity of the tape.") },
	{ TAPE_DRIVE_TAPE_REMAINING,	_T("TAPE_REMAINING"),
		_T("The device returns the remaining capacity of the tape.") },
	{ TAPE_DRIVE_ERASE_LONG,		_T("ERASE_LONG"),
		_T("The device performs a long erase operation.") },
	{ TAPE_DRIVE_ERASE_SHORT,		_T("ERASE_SHORT"),
		_T("The device performs a short erase operation.") },
	{ TAPE_DRIVE_ERASE_IMMEDIATE,	_T("ERASE_IMMEDIATE"),
		_T("The device performs an immediate erase operation — that is, it returns when the erase operation begins.") },
	{ TAPE_DRIVE_ERASE_BOP_ONLY,	_T("ERASE_BOP_ONLY"),
		_T("The device performs the erase operation from the beginning-of-partition marker only.") },
	{ TAPE_DRIVE_COMPRESSION,		_T("COMPRESSION"),
		_T("The device supports hardware data compression.") },
	{ TAPE_DRIVE_SET_COMPRESSION,	_T("SET_COMPRESSION"),
		_T("The device enables and disables hardware data compression.") },
	{ TAPE_DRIVE_SET_CMP_BOP_ONLY,	_T("SET_CMP_BOP_ONLY"),
		_T("The device must be at the beginning of a partition before it can set compression on.") },
	{ TAPE_DRIVE_ECC,				_T("ECC"),
		_T("The device supports hardware error correction.") },
	{ TAPE_DRIVE_SET_ECC,			_T("SET_ECC"),
		_T("The device enables and disables hardware error correction.") },
	{ TAPE_DRIVE_PADDING,			_T("PADDING"),
		_T("The device supports data padding.") },
	{ TAPE_DRIVE_SET_PADDING,		_T("SET_PADDING"),
		_T("The device enables and disables data padding.") },
	{ TAPE_DRIVE_SET_EOT_WZ_SIZE,	_T("SET_EOT_WZ_SIZE"),
		_T("The device supports setting the end-of-medium warning size.") },
	{ TAPE_DRIVE_LOAD_UNLOAD,		_T("LOAD_UNLOAD"),
		_T("The device enables and disables the device for further operations.") },
	{ TAPE_DRIVE_LOAD_UNLD_IMMED,	_T("LOAD_UNLD_IMMED"),
		_T("The device supports immediate load and unload operations.") },
	{ TAPE_DRIVE_LOCK_UNLOCK,		_T("LOCK_UNLOCK"),
		_T("The device enables and disables the tape ejection mechanism.") },
	{ TAPE_DRIVE_LOCK_UNLK_IMMED,	_T("LOCK_UNLK_IMMED"),
		_T("The device supports immediate lock and unlock operations.") },
	{ TAPE_DRIVE_EJECT_MEDIA,		_T("EJECT_MEDIA"),
		_T("The device physically ejects the tape on a software eject.") },
	{ TAPE_DRIVE_TENSION,			_T("TENSION"),
		_T("The device supports tape tensioning.") },
	{ TAPE_DRIVE_TENSION_IMMED,		_T("TENSION_IMMED"),
		_T("The device supports immediate tape tensioning.") },
	{ TAPE_DRIVE_CLEAN_REQUESTS,	_T("CLEAN_REQUESTS"),
		_T("The device can report if cleaning is required.") },
	{ TAPE_DRIVE_WRITE_PROTECT,		_T("WRITE_PROTECT"),
		_T("The device returns an error if the tape is write-enabled or write-protected.") },
	{ TAPE_DRIVE_FIXED,				_T("FIXED"),
		_T("The device creates fixed data partitions.") },
	{ TAPE_DRIVE_INITIATOR,			_T("INITIATOR"),
		_T("The device creates initiator-defined partitions.") },
	{ TAPE_DRIVE_SELECT,			_T("SELECT"),
		_T("The device creates select data partitions.") },
	{ TAPE_DRIVE_SPACE_IMMEDIATE,	_T("SPACE_IMMEDIATE"),
		_T("The device supports immediate spacing.") }
};

/* ---------------------------------------------------------------------------------------------- */

static const TCHAR *mode_title(unsigned int enabled, unsigned int supported, unsigned int switchable)
{
	return switchable ? 
		(enabled ? _T("enabled") : _T("disabled")) : 
		(supported ? _T("always enabled") : _T("not available"));
}

void list_drive_info(struct msg_filter *mf, TAPE_GET_DRIVE_PARAMETERS *drive, int features, int verbose)
{
	TCHAR size_str_buf[64];

	msg_append(mf, MSG_MESSAGE, _T("Drive information:\n"));
	
	msg_append(mf, MSG_MESSAGE, _T("ECC                    : %s\n"),
		mode_title(drive->ECC,
		drive->FeaturesLow & TAPE_DRIVE_ECC,
		drive->FeaturesHigh & TAPE_DRIVE_SET_ECC));
	
	msg_append(mf, MSG_MESSAGE, _T("Compression            : %s\n"), 
		mode_title(drive->Compression,
		drive->FeaturesLow & TAPE_DRIVE_COMPRESSION,
		drive->FeaturesHigh & TAPE_DRIVE_SET_COMPRESSION));
	
	msg_append(mf, MSG_MESSAGE, _T("Data padding           : %s\n"),
		mode_title(drive->DataPadding,
		drive->FeaturesLow & TAPE_DRIVE_PADDING,
		drive->FeaturesHigh & TAPE_DRIVE_SET_PADDING));
	
	msg_append(mf, MSG_MESSAGE, _T("Report setmarks        : %s\n"),
		mode_title(drive->ReportSetmarks,
		drive->FeaturesLow & TAPE_DRIVE_REPORT_SMKS,
		drive->FeaturesHigh & TAPE_DRIVE_SET_REPORT_SMKS));
	
	msg_append(mf, MSG_MESSAGE, _T("Minimum block size     : %s\n"), fmt_block_size(size_str_buf, drive->MinimumBlockSize, 2));
	msg_append(mf, MSG_MESSAGE, _T("Maximum block size     : %s\n"), fmt_block_size(size_str_buf, drive->MaximumBlockSize, 2));
	msg_append(mf, MSG_MESSAGE, _T("Default block size     : %s\n"), fmt_block_size(size_str_buf, drive->DefaultBlockSize, 2));
	if(drive->MaximumPartitionCount > 1)
		msg_append(mf, MSG_MESSAGE, _T("Max partition count    : %u\n"), drive->MaximumPartitionCount);
	if((drive->EOTWarningZoneSize != 0) || (drive->FeaturesLow & TAPE_DRIVE_SET_EOT_WZ_SIZE))
		msg_append(mf, MSG_MESSAGE, _T("EOT warning zone size  : %s\n"), fmt_block_size(size_str_buf, drive->EOTWarningZoneSize, 2));

	if(features)
	{
		unsigned int feature_cnt, index;

		msg_append(mf, MSG_MESSAGE, _T("\nDrive features:\n"));

		feature_cnt = sizeof(feature) / sizeof(struct drive_feature);
		for(index = 0; index < feature_cnt; index++)
		{
			if( (!(feature[index].value & 0x80000000) && (drive->FeaturesLow & feature[index].value)) ||
				((feature[index].value & 0x80000000) && (drive->FeaturesHigh & feature[index].value)) )
			{
				if(!verbose) {
					msg_append(mf, MSG_MESSAGE, _T("%s\n"), feature[index].name);
				} else {
					msg_append(mf, MSG_MESSAGE, _T("%s: %s\n"), feature[index].name, feature[index].description);
				}
			}
		}
	}

	msg_flush(mf);
}

/* ---------------------------------------------------------------------------------------------- */

void list_media_info(struct msg_filter *mf, TAPE_GET_DRIVE_PARAMETERS *drive, TAPE_GET_MEDIA_PARAMETERS *media)
{
	TCHAR size_str_buf[64];
	
	msg_append(mf, MSG_MESSAGE, _T("Media information:\n"));
	if((drive->FeaturesLow & TAPE_DRIVE_TAPE_CAPACITY) && (media->Capacity.QuadPart != 0)) {
		msg_append(mf, MSG_MESSAGE, _T("Full capacity          : %s\n"),
			fmt_block_size(size_str_buf, media->Capacity.QuadPart, 1));
		if((drive->FeaturesLow & TAPE_DRIVE_TAPE_REMAINING) && (media->Remaining.QuadPart != 0)) {
			msg_append(mf, MSG_MESSAGE, _T("Remaining capacity     : %s (%.2f%%)\n"), 
				fmt_block_size(size_str_buf, media->Remaining.QuadPart, 1),
				100.0 * (double)(media->Remaining.QuadPart) / (double)(media->Capacity.QuadPart));
		}
	} else if((drive->FeaturesLow & TAPE_DRIVE_TAPE_REMAINING) && (media->Remaining.QuadPart != 0)) {
		msg_append(mf, MSG_MESSAGE, _T("Remaining capacity     : %s\n"), 
			fmt_block_size(size_str_buf, media->Remaining.QuadPart, 1));
	}
	msg_append(mf, MSG_MESSAGE, _T("Block size             : %s\n"),
		fmt_block_size(size_str_buf, media->BlockSize, 2));
	if(drive->MaximumPartitionCount > 1) {
		msg_append(mf, MSG_MESSAGE, _T("Partition count        : %u\n"),
			media->PartitionCount);
	}
	if(drive->FeaturesLow & TAPE_DRIVE_WRITE_PROTECT) {
		msg_append(mf, MSG_MESSAGE, _T("Write protected        : %s\n"),
			media->WriteProtected ? _T("yes") : _T("no"));
	}
	
	msg_flush(mf);
}

/* ---------------------------------------------------------------------------------------------- */
