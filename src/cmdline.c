/* ---------------------------------------------------------------------------------------------- */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <tchar.h>
#include "util/fmt.h"
#include "util/getpath.h"
#include "cmdline.h"

/* ---------------------------------------------------------------------------------------------- */
/* Command line parse function */

/* Parse command line into individual arguments */
static TCHAR **argv_parse(TCHAR **p_arg_buf, const TCHAR *command_line)
{
	TCHAR *arg_buf, **argv = NULL, *pcur;
	size_t argc_max = 0, argc = 0;

	*p_arg_buf = NULL;

	/* Copy command line to scratch buffer */
	if( (arg_buf = _tcsdup(command_line)) == NULL )
		return 0;

	pcur = arg_buf;
	while(pcur != NULL)
	{
		TCHAR *arg;
		
		/* Skip whitespace characters before argument */
		while((pcur[0] == _T(' ')) || (pcur[0] == '\t'))
			pcur++;

		/* Finish if no more arguments */
		if(pcur[0] == 0)
			break;

		/* Check for quoted or unquoted argument */
		if((*pcur == _T('\'')) || (*pcur == _T('\"'))) {
			/* If quoted, trim at next matching quote */
			arg = pcur +  1;
			if( (pcur = _tcschr(arg, *pcur)) != NULL )
				*(pcur++) = 0;
		} else {
			/* If unqouted, trim at next whitespace */
			arg = pcur;
			if( (pcur = _tcspbrk(arg, _T(" \t"))) != NULL )
				*(pcur++) = 0;
			/* Replace command switches with escape character */
			if( ((arg[0] == _T('-')) || (arg[0] == _T('/'))) && (arg[1] != 0) )
				arg[0] = _T('\x1B');
		}

		/* Expand argument buffer if required */
		if(argc == argc_max)
		{
			TCHAR **argv_tmp;
			argc_max += 32;
			if( (argv_tmp = realloc(argv, (argc_max + 1) * sizeof(TCHAR*))) == NULL ) {
				free(argv);
				free(arg_buf);
				return NULL;
			}
			argv = argv_tmp;
		}

		/* Append argument to buffer */
		argv[argc++] = arg;
	}

	/* Check for at least one argument (should be program name) */
	if(argv == NULL) {
		free(arg_buf);
		return NULL;
	}

	/* Close argument list with zero pointer */
	argv[argc] = NULL;

	*p_arg_buf = arg_buf;
	return argv;
}

/* Check if argument is command line switch */
static int is_command_switch(const TCHAR *arg)
{
	return ((arg != NULL) && (arg[0] == _T('\x1B')));
}

/* Check if argument is command line parameter */
static int is_command_param(const TCHAR *arg)
{
	return ((arg != NULL) && (arg[0] != _T('\x1B')));
}

/* ---------------------------------------------------------------------------------------------- */
/* Parse command line parameters of various type */

/* Parse boolean parameter (yes/no). */
static int parse_enable_parameter(int *p_enable,
	const TCHAR *key_name, const TCHAR *description,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *arg;
	int enable;

	/* Parameter must be specified and not been used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used) {
		msg_append(mf, MSG_ERROR, _T("%s : No %s specified. Required: <on/off>.\n"),
			key_name, description);
		*p_success = 0;
		return 0;
	}

	/* Use next command line parameter */
	arg = *((*p_arg_cur)++);
	*p_param_used = 1;

	/* Parse boolean value from parameter */
	if( (_tcsicmp(arg, _T("yes")) == 0) ||
		(_tcsicmp(arg, _T("on")) == 0) ||
		(_tcsicmp(arg, _T("true")) == 0) )
	{
		enable = 1;
	} 
	else if( (_tcsicmp(arg, _T("no")) == 0) || 
		(_tcsicmp(arg, _T("off")) == 0) || 
		(_tcsicmp(arg, _T("false")) == 0) )
	{
		enable = 0;
	}
	else
	{
		msg_append(mf, MSG_ERROR, _T("%s : Invalid %s \"%s\". Required: <on/off>.\n"),
			key_name, description, arg);
		*p_success = 0;
		return 0;
	}

	*p_enable = enable;
	return 1;
}

/* Parse address parameter. */
static int parse_address_parameter(unsigned int *p_partition, unsigned __int64 *p_block_count,
	int absolute, const TCHAR *key_name, const TCHAR *description,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *addr_str, *addr_cur;
	TCHAR *ep;
	unsigned int partition;
	unsigned __int64 block_count;

	/* Parameter must be specified and not beging used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used) {
		msg_append(mf, MSG_ERROR, _T("%s : No %s specified. Required: %s.\n"),
			key_name, description, absolute ? _T("<address>") : _T("[partition.]<address>"));
		*p_success = 0;
		return 0;
	}

	/* Use next command line argument */
	addr_str = *((*p_arg_cur)++);
	*p_param_used = 1;

	/* Parse address */
	addr_cur = addr_str;
	partition = 0;
	block_count = _tcstoui64(addr_cur, &ep, 10);
	if((ep != addr_cur) && (*ep == _T('.')) && !absolute) {
		addr_cur = ep + 1;
		partition = (unsigned int)block_count;
		block_count = _tcstoui64(addr_cur, &ep, 10);
	}
	if((ep == addr_cur) || (*ep != 0)) {
		msg_append(mf, MSG_ERROR, _T("%s : Invalid %s \"%s\". Required: %s.\n"),
			key_name, description, addr_str,
			absolute ? _T("<address>") : _T("[partition.]<address>"));
		*p_success = 0;
		return 0;
	}

	*p_partition = partition;
	*p_block_count = block_count;
	return 1;
}


/* Parse operation count parameter (use count = 1 by default). */
static int parse_count_parameter(unsigned __int64 *p_count,
	const TCHAR *key_name, const TCHAR *description,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	unsigned __int64 count;

	/* Use command line parameter if specified and not used previously */
	if(!*p_param_used && is_command_param(**p_arg_cur))
	{
		const TCHAR *count_str;
		TCHAR *ep;
		
		count_str = *((*p_arg_cur)++);
		*p_param_used = 1;

		/* Parse unsigned int parameter */
		count = _tcstoui64(count_str, &ep, 10);
		if((ep == count_str) || (*ep != 0) || (count == 0)) {
			msg_append(mf, MSG_ERROR, _T("%s : Invalid %s \"%s\". Required: <count> (nonzero).\n"),
				key_name, description, count_str);
			*p_success = 0;
			return 0;
		}
	}
	else
	{
		/* Otherwise use count = 1 */
		count = 1;
	}

	*p_count = count;
	return 1;
}

/* Parse block size parameter. */
static int parse_size_parameter(unsigned __int64 *p_size,
	const TCHAR *key_name, const TCHAR *description,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *size_str;
	unsigned __int64 size;

	/* Parameter must be specified and not beging used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used) {
		msg_append(mf, MSG_ERROR, _T("%s : No %s specified. Required: <integer>[k/M/G].\n"),
			key_name, description);
		*p_success = 0;
		return 0;
	}

	/* Use next command line parameter */
	size_str = *((*p_arg_cur)++);
	*p_param_used = 1;

	/* Parse block size argument */
	if(!parse_block_size(size_str, &size)) {
		msg_append(mf, MSG_ERROR, _T("%s : Invalid %s \"%s\". Required: <integer>[k/M/G].\n"),
			key_name, description, size_str);
		*p_success = 0;
		return 0;
	}

	*p_size = size;
	return 1;
}

/* Parse block size parameter. */
static int parse_number_parameter(unsigned int *p_value,
	const TCHAR *key_name, const TCHAR *description,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *value_str;
	TCHAR *ep;
	unsigned int value;

	/* Parameter must be specified and not beging used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used) {
		msg_append(mf, MSG_ERROR, _T("%s : No %s specified. Required: <integer>.\n"),
			key_name, description);
		*p_success = 0;
		return 0;
	}

	/* Use next command line parameter */
	value_str = *((*p_arg_cur)++);
	*p_param_used = 1;

	/* Parse block size argument */

	value = _tcstoul(value_str, &ep, 10);
	if((ep == value_str) || (*ep != 0)) {
		msg_append(mf, MSG_ERROR, _T("%s : Invalid %s \"%s\". Required: <integer>.\n"),
			key_name, description, value_str);
		*p_success = 0;
		return 0;
	}

	*p_value = value;
	return 1;
}

/* ---------------------------------------------------------------------------------------------- */
/* Command line parameter apply functions */

/* Check and parse tape device name */
static void set_tape_device_name(TCHAR *device_name_buf,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *tape_str, *tape_n_str;
	TCHAR *ep;
	unsigned long n;

	/* Tape name must be specified and parameters not being used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used)
	{
		msg_append(mf, MSG_ERROR,
			_T("-d : No tape device name specified. Required: Tape<0..255>.\n"));
		*p_success = 0;
		return;
	}

	/* Get next command line argument */
	tape_str = *((*p_arg_cur)++);
	*p_param_used = 1;

	/* Parse tape device name */
	tape_n_str = tape_str;
	if(_tcsnicmp(tape_n_str, _T("\\\\.\\Tape"), 8) == 0) {
		tape_n_str += 8;
	} else if(_tcsnicmp(tape_n_str, _T("Tape"), 4) == 0) {
		tape_n_str += 4;
	}
	n = _tcstoul(tape_n_str, &ep, 10);
	if((tape_n_str == ep) || (*ep != 0) || (n > 255)) {
		msg_append(mf, MSG_ERROR,
			_T("-d : Invalid tape device name \"%s\". Required: Tape<0..255>.\n"), tape_str);
		*p_success = 0;
		return;
	}

	/* Set tape device name */
	_stprintf(device_name_buf, _T("\\\\.\\Tape%u"), n);
}

/* Add tape operation with parameters to operation list */
static int insert_tape_operation(struct cmd_line_args *cmd_line,
	enum tape_operation_code code, int enable, unsigned int partition,
	unsigned __int64 count, unsigned __int64 size, const TCHAR *filename,
	struct msg_filter *mf)
{
	struct tape_operation *op;

	if( (op = malloc(sizeof(struct tape_operation))) == NULL )
	{
		mf->out_of_memory = 1;
		return 0;
	}

	op->next = NULL;
	op->code = code;
	op->enable = enable;
	op->partition = partition;
	op->count = count;
	op->size = size;

	op->filename = (filename != NULL) ? _tcsdup(filename) : NULL;
	if((filename != NULL) && (op->filename == NULL))
	{
		free(op);
		mf->out_of_memory = 1;
		return 0;
	}

	*(cmd_line->next_op_ptr) = op;
	cmd_line->next_op_ptr = &(op->next);
	cmd_line->op_count++;

	return 1;
}

/* Add tape operation with enable parameter to parameter list */
static void insert_enable_operation(struct cmd_line_args *cmd_line,
	const TCHAR *key_name, const TCHAR *description, enum tape_operation_code code,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	int enable;

	if(!parse_enable_parameter(&enable, key_name, description,
		p_arg_cur, p_success, p_param_used, mf))
	{
		return;
	}

	/* Add to operation list */
	if(!insert_tape_operation(cmd_line, code, enable, 0, 0, 0, NULL, mf))
		*p_success = 0;
}

/* Add tape operation with block address to operation list. */
static void insert_address_operation(struct cmd_line_args *cmd_line, int absolute,
	const TCHAR *key_name, const TCHAR *description, enum tape_operation_code code,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	unsigned int partition;
	unsigned __int64 block_count;

	if(!parse_address_parameter(&partition, &block_count, absolute,
		key_name, description, p_arg_cur, p_success, p_param_used, mf))
	{
		return;
	}

	/* Insert operation to list */
	if(!insert_tape_operation(cmd_line, code, 0, partition, block_count, 0, NULL, mf))
		*p_success = 0;
}


/* Add tape operation with operation count argument to operation list.
 * Use operation count = 1 by default. */
static void insert_count_operation(struct cmd_line_args *cmd_line, 
	const TCHAR *key_name, const TCHAR *description, enum tape_operation_code code,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	unsigned __int64 count;

	if(!parse_count_parameter(&count, key_name, description,
		p_arg_cur, p_success, p_param_used, mf))
	{
		return;
	}

	/* Insert operation to list */
	if(!insert_tape_operation(cmd_line, code, 0, 0, count, 0, NULL, mf))
		*p_success = 0;
}

/* Add tape operation with block size argument to operation list. */
static void insert_size_operation(struct cmd_line_args *cmd_line,
	const TCHAR *key_name, const TCHAR *description, enum tape_operation_code code,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	unsigned __int64 size;

	if(!parse_size_parameter(&size, key_name, description,
		p_arg_cur, p_success, p_param_used, mf))
	{
		return;
	}

	/* Insert operation to list */
	if(!insert_tape_operation(cmd_line, code, 0, 0, 0, size, NULL, mf))
		*p_success = 0;
}

/* Add tape operations with filename argument to operation list. */
static void insert_filename_operation(struct cmd_line_args *cmd_line,
	const TCHAR *key_name, const TCHAR *description, enum tape_operation_code code,
	const TCHAR ***p_arg_cur, int *p_success, int *p_param_used,
	struct msg_filter *mf)
{
	const TCHAR *filename;

	/* At least one filename should be specified and parameters must not be used previously */
	if(!is_command_param(**p_arg_cur) || *p_param_used) {
		msg_append(mf, MSG_ERROR, _T("%s : No file specified %s.\n"), key_name, description);
		*p_success = 0;
		return;
	}

	/* Add specified operation for each filename */
	while(is_command_param(**p_arg_cur))
	{
		/* Get next filename */
		filename = *((*p_arg_cur)++);

		/* Insert operation */
		if(!insert_tape_operation(cmd_line, code, 0, 0, 0, 0, filename, mf))
			*p_success = 0;
	}

	*p_param_used = 1;
}

/* ---------------------------------------------------------------------------------------------- */

void usage_help(struct msg_filter *mf)
{
	/* Unassigned: j g z A O */
	msg_print(mf, MSG_MESSAGE,
		_T("Usage: tapectl [<switch> [param] ...]   Navigation commands:                  \n")
		_T("Output control:                         -l             List current position  \n")
		_T("-h             Show help and exit       -o             Rewind to origin       \n")
		_T("-v             Verbose output           -e             Seek to end of data    \n")
		_T("-V             Very verbose output      -a <block>     Seek to absol. address \n")
		_T("-q             Minimize output          -s [pt.]<blk>  Seek to block address  \n")
		_T("-S             Show operation list      -n [count]     Seek to next block     \n")
		_T("-y             Skip extra checks        -p [count]     Seek to previous block \n")
		_T("-Y             Confirm overwriting      -f [count]     Seek to filemark frwrd \n")
		_T("-P             Overwrite prompt         -b [count]     Seek to filemark bckwd \n")
		_T("Drive commands:                         -F [count]     Seek to setmark forward\n")
		_T("-d Tape<N>     Set tape device name     -B [count]     Seek to setmark bckwd  \n")
		_T("-C <on/off>    Compression on/off       Data read/write:                      \n")
		_T("-D <on/off>    Data padding on/off      -r <filelist>  Read files from media  \n")
		_T("-E <on/off>    ECC on/off               -w <filelist>  Write files to media   \n")
		_T("-R <on/off>    Report setmarks on/off   -W <filelist>  Write files with fmks  \n")
		_T("-Z <N>[k/M/G]  Set EOT warning zone     -m [count]     Write filemark         \n")
		_T("-k <N>[k/M/G]  Set block size           -M [count]     Write setmark          \n")
		_T("-x             Lock media ejection      -t             Truncate at current pos\n")
		_T("-u             Unlock media ejection    Input/Output settings:                \n")
		_T("Tape commands:                          -G <N>[k/M/G]  Set buffer size        \n")
		_T("-L             Load media               -I <N>[k/M/G]  Set I/O block size     \n")
		_T("-J             Eject media              -Q <N>         Set I/O queue length   \n")
		_T("-K <type,cnt,size> Create partition     -U             Use windows buffering  \n")
		_T("-c             Show media capacity      Test mode (check commands and exit):  \n")
		_T("-T             Tension tape             -N             Enable test mode       \n")
	);
}

static int cmd_parse(
	struct msg_filter *mf, struct cmd_line_args *cmd_line, const TCHAR * str,
	int is_config_file)
{
	TCHAR *arg_buf, **argv, **arg_cur;
	int success = 1, param_used;
	unsigned __int64 size_temp;

	/* Split command line into arguments */
	if( (argv = argv_parse(&arg_buf, str)) == NULL )
	{
		mf->out_of_memory = 1;
		return 0;
	}

	/* Skip first arguments (program name) */
	arg_cur = is_config_file ? argv : (argv + 1);

	/* Process arguments */
	while(*arg_cur != NULL)
	{
		TCHAR *arg, *sw_ptr;

		/* Get next argument */
		arg = *(arg_cur++);

		/* Check for command line switch */
		if(is_command_switch(arg))
		{
			/* Process switch group */
			param_used = 0; /* Next parameter can be used once by switch group */
			for(sw_ptr = arg + 1; *sw_ptr != 0; sw_ptr++) /* skip '-' or '/' */
			{
				switch(*sw_ptr)
				{
				/* Console output switch group */
				case _T('?'):
				case _T('h'):
				case _T('H'): /* Show help and exit */
					cmd_line->flags |= MODE_SHOW_HELP|MODE_EXIT;
					break;
				case _T('v'): /* Verbose output */
					cmd_line->flags |= MODE_VERBOSE;
					cmd_line->flags &= ~MODE_QUIET;
					break;
				case _T('V'): /* Very verbose output */
					cmd_line->flags |= MODE_VERBOSE|MODE_VERY_VERBOSE;
					cmd_line->flags &= ~MODE_QUIET;
					break;
				case _T('S'): /* Show operation list */
					cmd_line->flags |= MODE_SHOW_OPERATIONS;
					break;
				case _T('q'): /* No information messages */
					cmd_line->flags |= MODE_QUIET;
					cmd_line->flags &= ~(MODE_VERBOSE|MODE_VERY_VERBOSE);
					break;
				case _T('y'): /* No extra checks */
					cmd_line->flags |= MODE_NO_EXTRA_CHECKS;
					break;
				case _T('Y'): /* No overwrite checks */
					cmd_line->flags |= MODE_NO_OVERWRITE_CHECK;
					cmd_line->flags &= ~MODE_PROMPT_OVERWRITE;
					break;
				case _T('P'): /* Prompt before overwriting */
					cmd_line->flags |= MODE_PROMPT_OVERWRITE;
					cmd_line->flags &= ~MODE_NO_OVERWRITE_CHECK;
					break;

				/* Drive command group */
				case _T('d'): /* Set device name */
					set_tape_device_name(cmd_line->tape_device, &arg_cur, &success, &param_used, mf);
					break;
				case _T('i'): /* Display drive information */
					cmd_line->flags |= MODE_LIST_DRIVE_INFO;
					break;
				case _T('C'): /* Enable or disable data compression */
					insert_enable_operation(cmd_line, _T("-C"), _T("compression mode"),
						OP_SET_COMPRESSION, &arg_cur, &success, &param_used, mf);
					break;
				case _T('D'): /* Enable or disable data padding */
					insert_enable_operation(cmd_line, _T("-D"), _T("data padding mode"),
						OP_SET_DATA_PADDING, &arg_cur, &success, &param_used, mf);
					break;
				case _T('E'): /* Enable or disable ECC */
					insert_enable_operation(cmd_line, _T("-E"), _T("ECC mode"),
						OP_SET_ECC, &arg_cur, &success, &param_used, mf);
					break;
				case _T('R'): /* Enable or disable setmark reporting */
					insert_enable_operation(cmd_line, _T("-R"), _T("setmark reporting mode"),
						OP_SET_REPORT_SETMARKS, &arg_cur, &success, &param_used, mf);
					break;
				case _T('Z'): /* Set EOT warning zone size */
					insert_size_operation(cmd_line, _T("-W"), _T("EOT warning zone size"),
						OP_SET_EOT_WARNING_ZONE, &arg_cur, &success, &param_used, mf);
					break;
				case _T('k'): /* Set block size */
					insert_size_operation(cmd_line, _T("-k"), _T("block size"),
						OP_SET_BLOCK_SIZE, &arg_cur, &success, &param_used, mf);
					break;
				case _T('x'): /* Lock media ejection */
					if(!insert_tape_operation(cmd_line, OP_LOCK_TAPE_EJECT, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('u'): /* Unlock media ejection */
					if(!insert_tape_operation(cmd_line, OP_UNLOCK_TAPE_EJECT, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;

				/* Tape command group */
				case _T('L'): /* Load media */
					if(!insert_tape_operation(cmd_line, OP_LOAD_MEDIA, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('J'): /* Unload media */
					if(!insert_tape_operation(cmd_line, OP_UNLOAD_MEDIA, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('K'): /* Create tape partition */
					msg_append(mf, MSG_ERROR, _T("Making partition command currently not supported.\n"));
					success = 0;
					break;
				case _T('X'): /* Erase media */
					if(!insert_tape_operation(cmd_line, OP_ERASE_TAPE, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('c'): /* Show media capacity */
					if(!insert_tape_operation(cmd_line, OP_LIST_TAPE_CAPACITY, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('T'): /* Tension tape */
					if(!insert_tape_operation(cmd_line, OP_TAPE_TENSION, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;

				/* Navigation command group */
				case _T('l'): /* Show current position */
					if(!insert_tape_operation(cmd_line, OP_LIST_CURRENT_POSITION, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('o'): /* Move to origin (rewind tape) */
					if(!insert_tape_operation(cmd_line, OP_MOVE_TO_ORIGIN, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('e'): /* Move to end of data */
					if(!insert_tape_operation(cmd_line, OP_MOVE_TO_EOD, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;
				case _T('a'): /* Move to absolute position */
					insert_address_operation(cmd_line, 1, _T("-a"), _T("absolute position"),
						OP_SET_ABS_POSITION, &arg_cur, &success, &param_used, mf);
					break;
				case _T('s'): /* Move to logical position */
					insert_address_operation(cmd_line, 0, _T("-s"), _T("logical position"),
						OP_SET_TAPE_POSITION, &arg_cur, &success, &param_used, mf);
					break;
				case _T('n'): /* Move to next block */
					insert_count_operation(cmd_line, _T("-n"), _T("block count"),
						OP_MOVE_BLOCK_NEXT, &arg_cur, &success, &param_used, mf);
					break;
				case _T('p'): /* Move to previous block */
					insert_count_operation(cmd_line, _T("-p"), _T("block count"),
						OP_MOVE_BLOCK_PREV, &arg_cur, &success, &param_used, mf);
					break;
				case _T('f'): /* Move to file forward */
					insert_count_operation(cmd_line, _T("-f"), _T("file count"),
						OP_MOVE_FILE_NEXT, &arg_cur, &success, &param_used, mf);
					break;
				case _T('b'): /* Move to file backward */
					insert_count_operation(cmd_line, _T("-b"), _T("file count"),
						OP_MOVE_FILE_PREV, &arg_cur, &success, &param_used, mf);
					break;
				case _T('F'): /* Move to setmark forward */
					insert_count_operation(cmd_line, _T("-F"), _T("setmark count"),
						OP_MOVE_SMK_NEXT, &arg_cur, &success, &param_used, mf);
					break;
				case _T('B'): /* Move to setmark backward */
					insert_count_operation(cmd_line, _T("-B"), _T("setmark count"),
						OP_MOVE_SMK_PREV, &arg_cur, &success, &param_used, mf);
					break;

				/* Input/Output settings */
				case _T('G'): /* Set buffer size */
					parse_size_parameter(&(cmd_line->buffer_size), _T("-G"), _T("buffer size"),
						&arg_cur, &success, &param_used, mf);
					break;
				case _T('I'): /* Set I/O block size */
					parse_size_parameter(&size_temp, _T("-I"), _T("I/O block size"),
						&arg_cur, &success, &param_used, mf);
					if(size_temp > 0xFFFFFFFFUL)
						size_temp = 0xFFFFFFFFUL;
					cmd_line->io_block_size = (unsigned int)size_temp;
					break;
				case _T('Q'):
					parse_number_parameter(&(cmd_line->io_queue_size), _T("-Q"), _T("queue length"),
						&arg_cur, &success, &param_used, mf);
					break;
				case _T('U'): /* Enable windows buffering */
					cmd_line->flags |= MODE_WINDOWS_BUFFERING;
					break;

				/* Read/write operations */
				case _T('r'): /* Read files from media */
					insert_filename_operation(cmd_line, _T("-r"), _T("to save data from media"), 
						OP_READ_DATA, &arg_cur, &success, &param_used, mf);
					break;
				case _T('w'): /* Write files to media */
					insert_filename_operation(cmd_line, _T("-w"), _T("to write to media"), 
						OP_WRITE_DATA, &arg_cur, &success, &param_used, mf);
					break;
				case _T('W'): /* Write files with filemarks */
					insert_filename_operation(cmd_line, _T("-W"), _T("to write to media"), 
						OP_WRITE_DATA_AND_FMK, &arg_cur, &success, &param_used, mf);
					break;
				case _T('m'): /* Write filemarks */
					insert_count_operation(cmd_line, _T("-m"), _T("filemark count"),
						OP_WRITE_FILEMARK, &arg_cur, &success, &param_used, mf);
					break;
				case _T('M'): /* Write setmarks */
					insert_count_operation(cmd_line, _T("-M"), _T("setmark count"),
						OP_WRITE_SETMARK, &arg_cur, &success, &param_used, mf);
					break;
				case _T('t'): /* Truncate data at current position */
					if(!insert_tape_operation(cmd_line, OP_TRUNCATE, 0, 0, 0, 0, NULL, mf))
						success = 0;
					break;

				/* Test mode */
				case _T('N'): /* Don't execute any commands */
					cmd_line->flags |= MODE_TEST;
					break;

				/* Unknown command */
				default:
					msg_append(mf, MSG_ERROR, _T("Unknown command line switch \"-%c\".\n"), *sw_ptr);
					success = 0;
					break;
				}
			}
		}
		else
		{
			/* Currently no parameters expected without command switch */
			msg_append(mf, MSG_ERROR, _T("Unexpected command line parameter \"%s\".\n"), arg);
			success = 0;
		}
	}

	/* Cleanup */
	free(argv);
	free(arg_buf);

	return success;
}

static int load_config_file(struct msg_filter *mf, struct cmd_line_args *cmd_line)
{
	TCHAR *buf;
	char *mb_buf, *str, *p;
	size_t bufsize;
	int success = 1;

	bufsize = 4096;
	buf = malloc(bufsize * sizeof(TCHAR));
	mb_buf = malloc(bufsize);

	if( (buf != NULL) && (mb_buf != NULL) &&
		get_program_filename(buf, bufsize, _T(".cfg")) )
	{
		FILE *fp = _tfopen(buf, _T("rt"));
		if(fp != NULL)
		{
			buf[bufsize - 1] = 0;
			while(fgets(mb_buf, (int)bufsize, fp) != NULL)
			{
				for(str = mb_buf; (*str == ' ') || (*str == '\t'); str++)
					;
				if((p = strchr(str, '\n')) != NULL)
					*p = 0;
				if((*str == ';') || (*str == '#') || (*str == 0))
					continue;
#ifdef _UNICODE
				if((int)mbstowcs(buf, str, bufsize - 1) <= 0)
					continue;
#else
				strcpy(buf, str);
#endif
				if(!cmd_parse(mf, cmd_line, buf, 1))
					success = 0;
			}
			fclose(fp);
		}
	}

	free(mb_buf);
	free(buf);

	return success;
}

/* Parse and fill program invokation parameters */
int parse_command_line(struct msg_filter *mf, struct cmd_line_args *cmd_line)
{
	int success = 1; 

	if(!load_config_file(mf, cmd_line))
		success = 0;

	if(!cmd_parse(mf, cmd_line, GetCommandLine(), 0))
		success = 0;

	/* Print error message */
	if(!success)
	{
		msg_append(mf, MSG_INFO,
			_T("Command line invalid. Run tapectl -h for usage reference.\n"));
	}

	/* Check for any commands */
	if( success && !(cmd_line->flags & MODE_EXIT) &&
		(cmd_line->op_count == 0) && !(cmd_line->flags & MODE_LIST_DRIVE_INFO))
	{
		msg_append(mf, MSG_INFO,
			_T("No commands specified. Run tapectl -h for usage reference.\n"));
		cmd_line->flags |= MODE_EXIT;
	}

	/* Set verbosity level */
	if(cmd_line->flags & MODE_VERY_VERBOSE) {
		mf->report_level = MSG_VERY_VERBOSE;
	} else if(cmd_line->flags & MODE_VERBOSE) {
		mf->report_level = MSG_VERBOSE;
	} else if(cmd_line->flags & MODE_QUIET) {
		mf->report_level = MSG_WARNING;
	} else {
		mf->report_level = MSG_INFO;
	}

	return success;
}

/* Free parsed invokation parameters */
void free_cmd_line_args(struct cmd_line_args *cmd_line)
{
	struct tape_operation *op, *op_next;

	for(op = cmd_line->op_list; op != NULL; op = op_next)
	{
		op_next = op->next;
		free(op->filename);
		free(op);
	}

	cmd_line->flags = 0;
	cmd_line->op_list = NULL;
	cmd_line->op_count = 0;
}

/* ---------------------------------------------------------------------------------------------- */
