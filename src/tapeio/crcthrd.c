/* ---------------------------------------------------------------------------------------------- */

#include <process.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <crtdbg.h>
#include "crc32.h"
#include "crcthrd.h"

/* ---------------------------------------------------------------------------------------------- */

enum {
	EVENT_READ,
	EVENT_FLUSH_EXIT,

	EVENT_COUNT
};

static unsigned int __stdcall crc32_thread_proc(struct crc32_thread *cs)
{
	HANDLE events[EVENT_COUNT];
	DWORD event_id;

	events[EVENT_READ] = cs->h_ev_readable;
	events[EVENT_FLUSH_EXIT] = cs->h_ev_exit;

	do {
	
		size_t data_offset, data_length;
		
		event_id = WaitForMultipleObjects(EVENT_COUNT, events, FALSE, INFINITE);

		/* Get buffer state */
		EnterCriticalSection(&(cs->buf_ptr_lock));
		data_offset = cs->buf_data_offset;
		data_length = cs->buf_data_length;
		LeaveCriticalSection(&(cs->buf_ptr_lock));

		while( (data_length >= cs->chunk_size) ||
			((event_id == EVENT_FLUSH_EXIT) && (data_length > 0)) )
		{
			/* Choose size of block to process */
			size_t block_size = cs->buf_size - data_offset;
			if(block_size > data_length)
				block_size = data_length;
			if(block_size > cs->chunk_size)
				block_size = cs->chunk_size;

			/* Update CRC32 */
			cs->result = crc32_update(cs->result, cs->buffer + data_offset, block_size);

			/* Update data offset */
			data_offset += block_size;
			if(data_offset == cs->buf_size)
				data_offset = 0;

			/* Update buffer state */
			EnterCriticalSection(&(cs->buf_ptr_lock));
			cs->buf_data_offset = data_offset;
			cs->buf_data_length -= block_size;
			if( (cs->buf_size - cs->buf_data_length >= cs->thres_write) &&
				!(cs->event_flag & CRC_THREAD_WRITE_EV) )
			{
				SetEvent(cs->h_ev_writable);
				cs->event_flag |= CRC_THREAD_WRITE_EV;
			}
			if( (cs->buf_data_length < cs->chunk_size) &&
				(cs->event_flag & CRC_THREAD_READ_EV) )
			{
				ResetEvent(cs->h_ev_readable);
				cs->event_flag &= ~CRC_THREAD_READ_EV;
			}
			data_length = cs->buf_data_length;
			LeaveCriticalSection(&(cs->buf_ptr_lock));
		}
	
	} while(event_id != EVENT_FLUSH_EXIT);

	return 0;
}

/* ---------------------------------------------------------------------------------------------- */

int crc32_thread_init(struct crc32_thread *cs, size_t buf_size, size_t block_size, int priority)
{
	InitializeCriticalSection(&(cs->buf_ptr_lock));
	cs->buffer = VirtualAlloc(NULL, buf_size, MEM_COMMIT, PAGE_READWRITE);
	cs->h_ev_writable = CreateEvent(NULL, TRUE, TRUE, NULL);
	cs->h_ev_readable = CreateEvent(NULL, TRUE, FALSE, NULL);
	cs->h_ev_exit = CreateEvent(NULL, TRUE, FALSE, NULL);

	cs->buf_size = buf_size;
	cs->buf_data_offset = 0;
	cs->buf_data_length = 0;
	cs->thres_write = 0;
	cs->chunk_size = block_size;
	cs->event_flag = CRC_THREAD_WRITE_EV;
	cs->result = 0;

	if( (cs->buffer != NULL) && (cs->h_ev_writable != NULL) && (cs->h_ev_readable != NULL) &&
		(cs->h_ev_exit != NULL))
	{
		unsigned int thread_id;
		cs->h_thread = (HANDLE) _beginthreadex(NULL, 0, crc32_thread_proc, cs, 0, &thread_id);
		if(cs->h_thread != INVALID_HANDLE_VALUE) {
			if(priority != THREAD_PRIORITY_NORMAL)
				SetThreadPriority(cs->h_thread, priority);
			return 1;
		}
	}

	if(cs->h_ev_exit != NULL)
		CloseHandle(cs->h_ev_exit);
	if(cs->h_ev_readable != NULL)
		CloseHandle(cs->h_ev_readable);
	if(cs->h_ev_writable != NULL)
		CloseHandle(cs->h_ev_writable);
	if(cs->buffer != NULL)
		VirtualFree(cs->buffer, 0, MEM_RELEASE);
	DeleteCriticalSection(&(cs->buf_ptr_lock));

	return 0;
}

/* ---------------------------------------------------------------------------------------------- */

/* Write data to buffer */
void crc32_thread_write(struct crc32_thread *cs, const void *data, size_t length)
{
	size_t buf_free, wr_pos;

	for(;;)
	{
		/* Get buffer pointers / set free space threshold */
		EnterCriticalSection(&(cs->buf_ptr_lock));
		buf_free = cs->buf_size - cs->buf_data_length;
		wr_pos = cs->buf_data_offset + cs->buf_data_length;
		if(wr_pos >= cs->buf_size)
			wr_pos -= cs->buf_size;
		cs->thres_write = length;
		if((buf_free < length) && (cs->event_flag & CRC_THREAD_WRITE_EV))
		{
			ResetEvent(cs->h_ev_writable);
			cs->event_flag &= ~CRC_THREAD_WRITE_EV;
		}
		LeaveCriticalSection(&(cs->buf_ptr_lock));

		/* Check for enough free space */
		if(buf_free >= length)
			break;

		/* Wait for free space threshold */
		WaitForSingleObject(cs->h_ev_writable, INFINITE);
	}

	/* Copy data to buffer */
	if(length <= cs->buf_size - wr_pos) {
		memcpy(cs->buffer + wr_pos, data, length);
	} else {
		size_t part = cs->buf_size - wr_pos;
		memcpy(cs->buffer + wr_pos, data, part);
		memcpy(cs->buffer, (const BYTE*)data + part, length - part);
	}

	/* Update buffer state */
	EnterCriticalSection(&(cs->buf_ptr_lock));
	cs->buf_data_length += length;
	if((cs->buf_data_length >= cs->chunk_size) && !(cs->event_flag & CRC_THREAD_READ_EV))
	{
		SetEvent(cs->h_ev_readable);
		cs->event_flag |= CRC_THREAD_READ_EV;
	}
	LeaveCriticalSection(&(cs->buf_ptr_lock));
}

/* ---------------------------------------------------------------------------------------------- */

unsigned int crc32_thread_finish(struct crc32_thread *cs)
{
	SetEvent(cs->h_ev_exit);
	
	WaitForSingleObject(cs->h_thread, INFINITE);
	
	CloseHandle(cs->h_thread);
	CloseHandle(cs->h_ev_exit);
	CloseHandle(cs->h_ev_readable);
	CloseHandle(cs->h_ev_writable);
	VirtualFree(cs->buffer, 0, MEM_RELEASE);
	DeleteCriticalSection(&(cs->buf_ptr_lock));

	return cs->result;
}

/* ---------------------------------------------------------------------------------------------- */
