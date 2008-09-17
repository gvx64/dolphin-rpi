// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "stdafx.h"

#include "Blob.h"
#include "FileBlob.h"

#ifdef _WIN32
#include <windows.h>

namespace DiscIO
{

PlainFileReader::PlainFileReader(HANDLE hFile_)
{
	hFile = hFile_;
	DWORD size_low, size_high;
	size_low = GetFileSize(hFile, &size_high);
	size = ((u64)size_low) | ((u64)size_high << 32);
}

PlainFileReader* PlainFileReader::Create(const char* filename)
{
	HANDLE hFile = CreateFile(
		filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
		return new PlainFileReader(hFile);
	else
		return 0;
}

PlainFileReader::~PlainFileReader()
{
	CloseHandle(hFile);
}

bool PlainFileReader::Read(u64 offset, u64 size, u8* out_ptr)
{
	LONG offset_high = (LONG)(offset >> 32);
	SetFilePointer(hFile, (DWORD)(offset & 0xFFFFFFFF), &offset_high, FILE_BEGIN);

	if (size >= 0x100000000ULL)
		return false; // WTF, does windows really have this limitation?

	DWORD unused;
	if (!ReadFile(hFile, out_ptr, DWORD(size & 0xFFFFFFFF), &unused, NULL))
		return false;
	else
		return true;
}

}  // namespace

#else // linux, 64-bit. We do not yet care about linux32

namespace DiscIO
{

class PlainFileReader : public IBlobReader
{
	FILE* file_;
	s64 size;
private:
	PlainFileReader(FILE* file__)
	{
		file_ = file__;
		#if 0
			fseek64(file_, 0, SEEK_END);
		#else
			fseek(file_, 0, SEEK_END); // I don't have fseek64 with gcc 4.3
		#endif
		size = ftell(file_);
		fseek(file_, 0, SEEK_SET);
	}

public:
	static PlainFileReader* Create(const char* filename)
	{
		FILE* file_ = fopen(filename, "rb");
		if (file_)
		{
			return new PlainFileReader(file_);
		}
		return 0;
	}

	~PlainFileReader()
	{
		fclose(file_);
	}

	u64 GetDataSize() const	{ return(size); }
	u64 GetRawSize()  const { return(size); }

	bool Read(u64 offset, u64 nbytes, u8* out_ptr)
	{
		fseek(file_, offset, SEEK_SET);
		fread(out_ptr, nbytes, 1, file_);
		return true;
	}
};

}  // namespace

#endif
