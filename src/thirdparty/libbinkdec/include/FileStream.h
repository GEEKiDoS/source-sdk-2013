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

#ifndef _BinkFileStream_h_
#define _BinkFileStream_h_

#include <stdint.h>
#include "binkdec_interface.h"

namespace BinkCommon {

class FileStream
{
	public:
		void Open(bdec_file_io_t io, void *usrData);

		uint32_t ReadUint32LE();
		uint32_t ReadUint32BE();

		uint16_t ReadUint16LE();
		uint16_t ReadUint16BE();

		uint8_t ReadByte();

		enum SeekDirection {
			kSeekStart   = 0,
			kSeekCurrent = 1,
			kSeekEnd     = 2
		};

		bool Seek(int32_t offset, SeekDirection = kSeekStart);
		bool Skip(int32_t offset);

	private:
		bdec_file_io_t m_io{};
		void *m_usrData = nullptr;
};

} // close namespace BinkCommon

#endif
