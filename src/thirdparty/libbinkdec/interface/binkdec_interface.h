/*
 * libbinkdec - Bink video decoder
 * Copyright (C) 2011 Barry Duncan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#ifdef BINKDEC_EXPORT
#ifdef _WIN32
#define BINK_EXPORT	__declspec( dllexport )
#else
#define BINK_EXPORT	__attribute__ ((visibility("default")))
#endif
#else
#define BINK_EXPORT
#endif

#include <stdint.h>
#include <stddef.h>

extern "C"
{
	typedef void* ( *bdec_alloc_fn )( size_t );
	typedef void ( *bdec_release_fn )( void* );

	BINK_EXPORT void bdec_set_memory_funcs( bdec_alloc_fn alloc, bdec_release_fn release );

	typedef void( *bdec_log_fn )( const char *msg );
	BINK_EXPORT void bdec_set_log_func( bdec_log_fn log );

	typedef struct bdec_decoder bdec_decoder_t;

	BINK_EXPORT bdec_decoder_t* bdec_decoder_new();
	BINK_EXPORT void bdec_decoder_free( bdec_decoder_t* decoder );

	typedef struct bdec_file_io
	{
		typedef uint32_t( *bdec_file_read_fn )( void* dest, uint32_t destSize, void* usrData );
		typedef bool( *bdec_file_check_fn )( void* usrData );
		typedef void( *bdec_file_seek_fn )( int32_t offset, uint8_t seekDir, void* usrData );

		bdec_file_check_fn error;
		bdec_file_seek_fn seek;
		bdec_file_read_fn read;
	} bdec_file_io_t;

	BINK_EXPORT bool bdec_open_file( bdec_decoder_t* decoder, bdec_file_io_t io, void* usrData );

	BINK_EXPORT void bdec_get_info( bdec_decoder_t* decoder, uint32_t* width, uint32_t* height, bool* transparent );

	BINK_EXPORT uint32_t bdec_get_frame_count( bdec_decoder_t* decoder );
	BINK_EXPORT uint32_t bdec_get_current_frame_num( bdec_decoder_t* decoder );
	BINK_EXPORT float bdec_get_frame_rate( bdec_decoder_t* decoder );
	BINK_EXPORT float bdec_get_frame_time( bdec_decoder_t* decoder );
	BINK_EXPORT void bdec_goto_frame( bdec_decoder_t* decoder, uint32_t frame );

	typedef struct bdec_audio_info
	{
		uint32_t sampleRate;
		uint32_t channelCount;
		uint32_t idealBufferSize;
	} bdec_audio_info_t;

	BINK_EXPORT uint32_t bdec_get_audio_track_count( bdec_decoder_t* decoder );
	BINK_EXPORT bdec_audio_info_t bdec_get_audio_track_info( bdec_decoder_t* decoder, uint32_t track );

	typedef struct bdec_audio_data
	{
		uint32_t channelCount;
		uint32_t dataSize;
		uint8_t *data;
	} bdec_audio_data_t;

	BINK_EXPORT bdec_audio_data_t bdec_get_audio_data( bdec_decoder_t* decoder, uint32_t track );

	typedef struct bdec_video_plane
	{
		uint32_t width;
		uint32_t height;
		uint32_t pitch;
		uint8_t *data;
	} bdec_video_plane_t;

	BINK_EXPORT void bdec_get_next_frame( bdec_decoder_t* decoder, bdec_video_plane_t planes[4] );
}
