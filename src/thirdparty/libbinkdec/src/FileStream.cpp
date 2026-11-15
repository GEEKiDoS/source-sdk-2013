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

#include "FileStream.h"
#include <stdlib.h>

namespace BinkCommon {

void FileStream::Open( bdec_file_io_t io, void *usrData )
{
	m_io = io;
	m_usrData = usrData;
}

uint32_t FileStream::ReadUint32LE()
{
	uint32_t value = 0;
	m_io.read( &value, sizeof( value ), m_usrData );
	return value;
}

uint32_t FileStream::ReadUint32BE()
{
	uint32_t value = 0;
	m_io.read( &value, sizeof( value ), m_usrData );
#ifdef _MSC_VER
	return _byteswap_ulong(value);
#else // DG: provide alternative for GCC/clang
	return __builtin_bswap32(value);
#endif
}

uint16_t FileStream::ReadUint16LE()
{
	uint16_t value = 0;
	m_io.read( &value, sizeof( value ), m_usrData );
	return value;
}

uint16_t FileStream::ReadUint16BE()
{
	uint16_t value = 0;
	m_io.read( &value, sizeof( value ), m_usrData );
#ifdef _MSC_VER
	return _byteswap_ushort(value);
#else // DG: provide alternative for GCC/clang
	return __builtin_bswap16(value);
#endif
}

uint8_t FileStream::ReadByte()
{
	uint8_t value = 0;
	m_io.read( &value, sizeof( value ), m_usrData );
	return value;
}

bool FileStream::Seek(int32_t offset, SeekDirection direction)
{
	m_io.seek( offset, direction, m_usrData );
	return !m_io.error( m_usrData );
}

bool FileStream::Skip(int32_t offset)
{
	return Seek(offset, kSeekCurrent);
}

} // close namespace BinkCommon

