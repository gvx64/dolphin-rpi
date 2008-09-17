#include "../PowerPC/PowerPC.h"
#include "Boot.h"
#include "../HLE/HLE.h"
#include "Boot_ELF.h"
#include "ElfReader.h"
#include "MappedFile.h"

bool CBoot::IsElfWii(const char *filename)
{
	Common::IMappedFile *mapfile = Common::IMappedFile::CreateMappedFileDEPRECATED();
	bool ok = mapfile->Open(filename);
	if (!ok)
		return false;
	size_t filesize = (size_t)mapfile->GetSize();
	u8 *ptr = mapfile->Lock(0, filesize);
	u8 *mem = new u8[filesize];
	memcpy(mem, ptr, filesize);
	mapfile->Unlock(ptr);
	mapfile->Close();
	
	ElfReader reader(mem);
	
	// TODO: Find a more reliable way to distinguish.
	bool isWii = reader.GetEntryPoint() >= 0x80004000;
	delete [] mem;
    return isWii;
}


bool CBoot::Boot_ELF(const char *filename)
{
	Common::IMappedFile *mapfile = Common::IMappedFile::CreateMappedFileDEPRECATED();
	mapfile->Open(filename);
	u8 *ptr = mapfile->Lock(0, mapfile->GetSize());
	u8 *mem = new u8[(size_t)mapfile->GetSize()];
	memcpy(mem, ptr, (size_t)mapfile->GetSize());
	mapfile->Unlock(ptr);
	mapfile->Close();
	
	ElfReader reader(mem);
	reader.LoadInto(0x80000000);
	if (!reader.LoadSymbols())
	{
		if (LoadMapFromFilename(filename))
			HLE::PatchFunctions();
	} else {
		HLE::PatchFunctions();
	}
	delete [] mem;
	
	PC = reader.GetEntryPoint();

    return true;
}

