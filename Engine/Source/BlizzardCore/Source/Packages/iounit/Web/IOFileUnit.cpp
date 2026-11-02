// wow335modding: browser build of iounit/Win64/IOFileUnit.cpp. No sector-aligned unbuffered I/O in a browser:
// reads are plain positional reads, and the "unbuffered" unit aligns to 4 KB like the Windows fallback.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOFileUnit.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Web/WebFile.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"

bool IOFileUnit::ReadNative(uint32_t offset, const char* path, void* buffer, uint32_t count, uint32_t* bytesRead) {
    char native[1024];
    Blizzard::String::MakeUnivPath(path, native, sizeof(native));
    int fd = WebFile::Open(native, O_RDONLY);

    if (fd < 0) {
        return false;
    }

    ssize_t read = WebFile::Pread(fd, buffer, count, offset);
    WebFile::Close(fd);

    if (read < 0) {
        return false;
    }

    if (bytesRead) {
        *bytesRead = static_cast<uint32_t>(read);
    }

    return true;
}

IOUnbufferedFileUnit::IOUnbufferedFileUnit(Blizzard::File::StreamRecord* stream, bool ownsMemory) : IOFileUnit(stream, ownsMemory) {
    Blizzard::File::FileInfo* info = Blizzard::File::GetFileInfo(stream);
    this->m_size = info->m_size;
    this->m_alignMask = 0xFFF;
    this->m_buffer = Blizzard::Memory::Allocate(this->m_alignMask + 0x100000);
    this->m_alignedBuffer = reinterpret_cast<void*>(~static_cast<uintptr_t>(this->m_alignMask) & (reinterpret_cast<uintptr_t>(this->m_buffer) + this->m_alignMask));
}

#endif
