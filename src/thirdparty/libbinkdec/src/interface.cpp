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

#include "BinkDecoder.h"
#include "binkdec_interface.h"
#include "bd_mem.h"

#include <cstdlib>
#include <new>
#include <bit>


static constinit bdec_log_fn s_log;

namespace BinkCommon {

	void LogError(const char *error)
	{
		if ( !s_log )
			return;
		s_log( error );
	}

} // close namespace BinkCommon


extern "C"
{
	constinit bdec_alloc_fn bd_malloc = malloc;
	constinit bdec_release_fn bd_free = free;

	void bdec_set_memory_funcs( bdec_alloc_fn alloc, bdec_release_fn release )
	{
		bd_malloc = alloc;
		bd_free = release;
	}

	void bdec_set_log_func( bdec_log_fn log )
	{
		s_log = log;
	}

	bdec_decoder_t* bdec_decoder_new()
	{
		return reinterpret_cast<bdec_decoder_t*>( new ( bd_malloc( sizeof( BinkDecoder ) ) ) BinkDecoder() );
	}

	void bdec_decoder_free( bdec_decoder_t* decoder )
	{
		auto dec = reinterpret_cast<BinkDecoder*>( decoder );
		dec->~BinkDecoder();
		bd_free( dec );
	}

	bool bdec_open_file( bdec_decoder_t* decoder, bdec_file_io_t io, void* usrData )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->Open( io, usrData );;
	}

	void bdec_get_info( bdec_decoder_t* decoder, uint32_t* width, uint32_t* height, bool* transparent )
	{
		auto dec = reinterpret_cast<BinkDecoder*>( decoder );
		*width = dec->frameWidth;
		*height = dec->frameHeight;
		if ( transparent )
			*transparent = dec->IsTransparent();
	}

	uint32_t bdec_get_frame_count( bdec_decoder_t* decoder )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->GetNumFrames();
	}

	uint32_t bdec_get_current_frame_num( bdec_decoder_t* decoder )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->GetCurrentFrameNum();
	}

	float bdec_get_frame_rate( bdec_decoder_t* decoder )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->GetFrameRate();
	}

	float bdec_get_frame_time( bdec_decoder_t* decoder )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->GetFrameTime();
	}

	void bdec_goto_frame( bdec_decoder_t* decoder, uint32_t frame )
	{
		reinterpret_cast<BinkDecoder*>( decoder )->GotoFrame( frame );
	}

	uint32_t bdec_get_audio_track_count( bdec_decoder_t* decoder )
	{
		return reinterpret_cast<BinkDecoder*>( decoder )->GetNumAudioTracks();
	}

	bdec_audio_info_t bdec_get_audio_track_info( bdec_decoder_t* decoder, uint32_t track )
	{
		return std::bit_cast<bdec_audio_info_t>( reinterpret_cast<BinkDecoder*>( decoder )->GetAudioTrackDetails( track ) );
	}

	bdec_audio_data_t bdec_get_audio_data( bdec_decoder_t* decoder, uint32_t track )
	{
		auto t = reinterpret_cast<BinkDecoder*>( decoder )->GetAudioTrack( track );
		return bdec_audio_data_t{
			.channelCount = t->nChannelsReal,
			.dataSize = t->bytesReadThisFrame,
			.data = t->buffer
		};
	}

	void bdec_get_next_frame( bdec_decoder_t * decoder, bdec_video_plane_t planes[4] )
	{
		static_assert( sizeof( ImagePlane ) == sizeof( bdec_video_plane_t ) );
		reinterpret_cast<BinkDecoder*>( decoder )->GetNextFrame( reinterpret_cast<ImagePlane*>( planes ) );
	}
}
