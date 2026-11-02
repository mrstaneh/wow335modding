// wow335modding: browser build of File/Win64 (File.cpp, Filestack.cpp, Filestack_Streaming.cpp).
// Files go through POSIX calls on Emscripten's virtual file system, so they work on whatever storage it mounts.
// Game paths use backslashes; MakeUnivPath turns them into forward slashes. A stream's m_fileHandle holds the file
// descriptor (INVALID_HANDLE_VALUE is -1, an invalid descriptor too). There is no streaming (the trial client's
// on-demand downloads): its open falls through to the next file system in the stack.
// ponytail: paths are case-sensitive here, unlike on Windows; add a case-insensitive lookup if data files don't match.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Debug/Debug.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/File/Filestack_Streaming.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/MemoryStorm.h"
#include "Engine/Source/BlizzardCore/Source/System/Memory/TempBuffer.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"
#include "Engine/Source/Storm/H/storm.h"

System_File::IOFunc System_File::read_func = System_File::read_first;
System_File::IOFunc System_File::write_func = System_File::write_first;
bool System_File::s_read_init;
Blizzard::Lock::Atomic System_File::s_readInitCount;
bool System_File::s_write_init;
Blizzard::Lock::Atomic System_File::s_writeInitCount;
Blizzard::Lock::Mutex System_File::s_ioMutex;
Blizzard::Lock::Mutex* System_File::s_ioLock;

static const int64_t UNIX_SECONDS_AT_2000 = 946684800;

static int ToFd(HANDLE handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle));
}

static HANDLE FromFd(int fd) {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
}

static Blizzard::Time::Timestamp FromTimespec(const timespec& time) {
    return static_cast<Blizzard::Time::Timestamp>((static_cast<int64_t>(time.tv_sec) - UNIX_SECONDS_AT_2000) * 1000000000 + time.tv_nsec);
}

// Blizzard attribute bits: 0x1 read-only, 0x20 regular file, 0x40 directory.
static int32_t FromMode(mode_t mode) {
    int32_t attributes = S_ISDIR(mode) ? 0x40 : 0x20;
    if (!(mode & S_IWUSR)) {
        attributes |= 0x1;
    }
    return attributes;
}

static void FillInfo(Blizzard::File::FileInfo* info, const struct stat& st) {
    info->m_size = st.st_size;
    info->m_createTime = FromTimespec(st.st_ctim);
    info->m_modTime = FromTimespec(st.st_mtim);
    info->m_accessTime = FromTimespec(st.st_atim);
    info->m_attributes = FromMode(st.st_mode);
    info->m_regularFile = S_ISREG(st.st_mode);
    info->m_existence = S_ISDIR(st.st_mode) ? 2 : 1;
}

static void NativePath(const char* path, char* buffer, uint32_t size) {
    Blizzard::String::MakeUnivPath(path ? path : "", buffer, size);
}

int32_t System_File::ToNativeAttributes(int32_t attributes) {
    return attributes;
}

int32_t System_File::FromNativeAttributes(int32_t attributes) {
    return attributes;
}

bool System_File::ReadP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    return read_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::WriteP(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(parms->m_pos);
    overlapped.OffsetHigh = static_cast<DWORD>(parms->m_pos >> 32);
    return write_func(parms->m_stream, parms->m_buffer, &overlapped, &parms->m_amount);
}

bool System_File::Close(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (stream->m_fileHandle != INVALID_HANDLE_VALUE) {
        close(ToFd(stream->m_fileHandle));
    }

    SMemFree(stream, __FILE__, __LINE__, 0);
    return true;
}

bool System_File::GetRootChars(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;
    parms->m_pos = path && path[0] == '/' ? 1 : 0;
    return true;
}

void System_File::GetFileInfoByFile(Blizzard::File::StreamRecord* file) {
    if (file->m_infoValid) {
        return;
    }

    struct stat st;
    if (fstat(ToFd(file->m_fileHandle), &st) != 0) {
        return;
    }

    int64_t size = file->m_info.m_size;
    FillInfo(&file->m_info, st);

    if (file->m_streaming && file->m_streaming->m_entry) {
        file->m_info.m_size = size;
    }

    file->m_infoValid = true;
}

bool System_File::Exists(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));
    struct stat st;

    if (stat(path, &st) != 0) {
        parms->m_info->m_existence = 0;
    } else {
        parms->m_info->m_existence = S_ISDIR(st.st_mode) ? 2 : 1;
    }

    return true;
}

bool System_File::MakeAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));

    if (path[0] == '/') {
        Blizzard::String::Copy(parms->m_cwdbuf, path, parms->m_cwdbuflen);
    } else {
        char cwd[1024];
        if (!getcwd(cwd, sizeof(cwd))) {
            Blizzard::String::Copy(cwd, "/", sizeof(cwd));
        }
        Blizzard::String::Copy(parms->m_cwdbuf, cwd, parms->m_cwdbuflen);
        Blizzard::String::ForceTrailingSeparator(parms->m_cwdbuf, parms->m_cwdbuflen, '/');
        Blizzard::String::Append(parms->m_cwdbuf, path, parms->m_cwdbuflen);
    }

    const char* original = parms->m_path;
    size_t length = original ? strlen(original) : 0;
    if (length && (original[length - 1] == '/' || original[length - 1] == '\\')) {
        Blizzard::String::ForceTrailingSeparator(parms->m_cwdbuf, parms->m_cwdbuflen, '/');
    }

    return true;
}

bool System_File::Move(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char src[1024];
    char dst[1024];
    NativePath(parms->m_path, src, sizeof(src));
    NativePath(parms->m_newpath, dst, sizeof(dst));
    return rename(src, dst) == 0;
}

bool System_File::Delete(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));
    return unlink(path) == 0;
}

// With an OVERLAPPED the read is positional, as ReadP/WriteP use it; without, it continues at the file position.
bool System_File::read_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    int fd = ToFd(file->m_fileHandle);
    ssize_t result;
    if (overlapped) {
        off_t offset = static_cast<off_t>((static_cast<uint64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset);
        result = pread(fd, buffer, *count, offset);
    } else {
        result = read(fd, buffer, *count);
    }

    if (result >= 0) {
        *count = static_cast<int32_t>(result);
        return true;
    }

    *count = 0;
    Blizzard::File::SetLastError(5);
    Blizzard::File::AddToLastErrorStack(5, Blizzard::String::QuickFormat<1024>("Web Read - %s", file->m_pathname).m_buffer, 1);
    return false;
}

bool System_File::write_overlapped(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    if (file->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    if (!*count) {
        return true;
    }

    file->m_infoValid = false;
    int fd = ToFd(file->m_fileHandle);
    ssize_t result;
    if (overlapped) {
        off_t offset = static_cast<off_t>((static_cast<uint64_t>(overlapped->OffsetHigh) << 32) | overlapped->Offset);
        result = pwrite(fd, buffer, *count, offset);
    } else {
        result = write(fd, buffer, *count);
    }

    if (result >= 0) {
        *count = static_cast<int32_t>(result);
        return true;
    }

    *count = 0;
    Blizzard::File::SetLastError(6);
    Blizzard::File::AddToLastErrorStack(6, Blizzard::String::QuickFormat<1024>("Web Write - %s", file->m_pathname).m_buffer, 1);
    return false;
}

// Blizzard open modes: 0x1 read, 0x2 write, 0x100/0x400 create (truncating), 0x800 create only if new,
// 0x1000 must exist, otherwise open or create; 0x200 appends.
static int ToOpenFlags(uint32_t mode) {
    int flags = (mode & 0x2) ? ((mode & 0x1) ? O_RDWR : O_WRONLY) : O_RDONLY;

    if ((mode & 0x100) || (mode & 0x400)) {
        flags |= (mode & 0x800) ? (O_CREAT | O_EXCL) : (O_CREAT | O_TRUNC);
    } else if (mode & 0x800) {
        flags |= O_CREAT | O_EXCL;
    } else if (!(mode & 0x1000) && (mode & 0x2)) {
        flags |= O_CREAT;
    }

    return flags;
}

bool System_File::SetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));
    return chdir(path) == 0;
}

bool System_File::Create(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::SetLastError(9);
    return false;
}

bool System_File::GetWorkingDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_cwdbuf || parms->m_cwdbuflen <= 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    *parms->m_cwdbuf = '\0';
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer))) {
        FromNativeName(parms->m_cwdbuf, parms->m_cwdbuflen, buffer);
    }

    return true;
}

bool System_File::ProcessDirFast(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;
    char native[1024];
    NativePath(path, native, sizeof(native));
    DIR* dir = opendir(native);

    if (!dir) {
        Blizzard::Debug::Printf(Blizzard::Debug::StderrOutput, "failed to open %s\n", path);
        Blizzard::File::SetLastError(8);
        return false;
    }

    Blizzard::File::ProcessDirParms dirParms;
    dirParms.m_pathRoot = path;
    dirParms.m_cookie = parms->m_cookie;

    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) {
            continue;
        }

        bool isDir = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN) {
            char full[1024];
            Blizzard::String::Format(full, sizeof(full), "%s/%s", native, name);
            struct stat st;
            isDir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }

        char filename[1024];
        FromNativeName(filename, sizeof(filename), name);
        dirParms.m_filename = filename;
        dirParms.m_isDir = isDir;

        if (!parms->m_dircallback(dirParms)) {
            closedir(dir);
            return false;
        }
    }

    closedir(dir);
    return true;
}

bool System_File::Flush(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    return fsync(ToFd(parms->m_stream->m_fileHandle)) == 0;
}

bool System_File::GetFileInfo(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_path) {
        char path[1024];
        NativePath(parms->m_path, path, sizeof(path));
        struct stat st;

        if (stat(path, &st) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        FillInfo(parms->m_info, st);
        parms->m_info->m_path = nullptr;
        return true;
    }

    if (parms->m_stream) {
        GetFileInfoByFile(parms->m_stream);
        Blizzard::File::StreamRecord* stream = parms->m_stream;
        parms->m_info = &stream->m_info;

        if (!stream->m_info.m_path) {
            stream->m_info.m_path = stream->m_pathname;
        }

        return parms->m_stream->m_infoValid;
    }

    Blizzard::File::SetLastError(8);
    return false;
}

bool System_File::GetFreeSpace(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    parms->m_pos = 0x100000000; // the browser does not say; 4 GB keeps the free-space checks happy
    return true;
}

bool System_File::GetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    off_t pos = stream->m_fileHandle == INVALID_HANDLE_VALUE ? -1 : lseek(ToFd(stream->m_fileHandle), 0, SEEK_CUR);

    if (pos < 0) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    parms->m_pos = pos;
    return true;
}

bool System_File::IsAbsolutePath(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if ((path[0] == '/' && path[1]) || ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/') && path[2])) {
        return true;
    }

    Blizzard::File::SetLastError(8);
    return false;
}

bool System_File::IsReadOnly(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));
    struct stat st;

    if (stat(path, &st) == 0) {
        return !(st.st_mode & S_IWUSR);
    }

    Blizzard::File::SetLastError(8);
    return false;
}

bool System_File::CreateDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (!parms->m_path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));

    if (parms->m_recursive) {
        for (char* end = path + 1; *end; end++) {
            if (*end == '/') {
                *end = '\0';
                mkdir(path, 0777);
                *end = '/';
            }
        }
    }

    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    }

    Blizzard::File::SetLastError(8);
    return false;
}

bool System_File::Copy(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    bool overwrite = parms->m_overwrite;
    char src[1024];
    char dst[1024];
    NativePath(parms->m_path, src, sizeof(src));
    NativePath(parms->m_newpath, dst, sizeof(dst));

    Blizzard::File::StreamRecord* input;
    if (!Blizzard::File::Open(src, 0x1005, input)) {
        Blizzard::File::SetLastError(2);
        return false;
    }

    Blizzard::File::StreamRecord* output;
    if (!Blizzard::File::Open(dst, (overwrite ? 0x400 : 0xC00) | 0x2, output)) {
        Blizzard::File::Close(input);
        Blizzard::File::SetLastError(4);
        return false;
    }

    int64_t size = Blizzard::File::GetFileInfo(input)->m_size;
    int64_t offset = 0;
    bool result = true;
    void* buffer = Blizzard::Memory::Allocate(size >= 0xA00000 ? 0xA00000 : static_cast<uint32_t>(size));

    while (offset < size) {
        int32_t count = size - offset >= 0xA00000 ? 0xA00000 : static_cast<int32_t>(size - offset);
        if (!Blizzard::File::Read(input, buffer, offset, &count) || !Blizzard::File::Write(output, buffer, offset, &count)) {
            result = false;
            break;
        }
        offset += count;
    }

    Blizzard::Memory::Free(buffer);
    Blizzard::File::Close(input);
    Blizzard::File::Close(output);

    if (!result) {
        Blizzard::File::Delete(dst);
    }

    return result;
}

bool System_File::Open(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    int32_t mode = parms->m_mode;
    char path[1024];
    NativePath(parms->m_path, path, sizeof(path));

    int fd = open(path, ToOpenFlags(mode), 0666);
    if (fd < 0) {
        Blizzard::File::SetLastError(4);
        Blizzard::File::AddToLastErrorStack(4, Blizzard::String::QuickFormat<1024>("Web Open - %s", path).m_buffer, 1);
        return false;
    }

    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (!stream) {
        uint32_t length = static_cast<uint32_t>(strlen(path));
        stream = static_cast<Blizzard::File::StreamRecord*>(SMemAlloc(sizeof(Blizzard::File::StreamRecord) + length + 1, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
        stream->m_pathname = reinterpret_cast<char*>(stream + 1);
        Blizzard::String::Copy(stream->m_pathname, path, length + 1);
    }

    stream->m_fileHandle = FromFd(fd);
    stream->m_mode = mode;
    stream->m_infoValid = false;
    stream->m_info.m_path = stream->m_pathname;
    GetFileInfoByFile(stream);

    if (mode & 0x200) {
        Blizzard::File::SetPos(stream, stream->m_info.m_size, 0);
    }

    parms->m_stream = stream;
    return true;
}

bool System_File::Read(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return read_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::RemoveDirectory(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    const char* path = parms->m_path;

    if (parms->m_recursive) {
        return Blizzard::File::RemoveDirectoryAndContents(path, false);
    }

    if (!path) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    char native[1024];
    NativePath(path, native, sizeof(native));
    return rmdir(native) == 0;
}

bool System_File::SetCacheMode(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    stream->m_mode = parms->m_mode | (stream->m_mode & ~0x40); // 0x40 is unbuffered I/O, which means nothing here
    return true;
}

bool System_File::SetEOF(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;
    int64_t offset;

    if (!Blizzard::File::GetPos(stream, &offset) || !Blizzard::File::SetPos(stream, parms->m_pos, parms->m_posMode)) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    int fd = ToFd(stream->m_fileHandle);
    bool result = ftruncate(fd, lseek(fd, 0, SEEK_CUR)) == 0;
    Blizzard::File::SetPos(stream, offset, 0);
    return result;
}

bool System_File::SetAttributes(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    if (parms->m_has.m_all & 0x10) {
        Blizzard::Time::Timestamp time = parms->m_info->m_modTime;
        int64_t seconds = time / 1000000000 + UNIX_SECONDS_AT_2000;
        timespec times[2];
        times[0].tv_sec = 0;
        times[0].tv_nsec = UTIME_OMIT;
        times[1].tv_sec = static_cast<time_t>(seconds);
        times[1].tv_nsec = static_cast<long>(time % 1000000000);

        if (futimens(ToFd(parms->m_stream->m_fileHandle), times) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_stream->m_info.m_modTime = time;
        parms->m_has.m_all &= ~0x10u;
    }

    if (parms->m_has.m_all & 0x4) {
        char path[1024];
        NativePath(parms->m_path, path, sizeof(path));

        if (chmod(path, (parms->m_info->m_attributes & 0x1) ? 0444 : 0666) != 0) {
            Blizzard::File::SetLastError(8);
            return false;
        }

        parms->m_has.m_all &= ~0x4u;
    }

    return parms->m_has.m_all == 0;
}

bool System_File::SetPos(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    Blizzard::File::StreamRecord* stream = parms->m_stream;

    if (stream->m_fileHandle == INVALID_HANDLE_VALUE) {
        Blizzard::File::SetLastError(8);
        return false;
    }

    // posMode 0/1/2 is begin/current/end, the same order as SEEK_SET/SEEK_CUR/SEEK_END
    return lseek(ToFd(stream->m_fileHandle), static_cast<off_t>(parms->m_pos), parms->m_posMode) >= 0;
}

bool System_File::Write(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return write_overlapped(parms->m_stream, parms->m_buffer, nullptr, &parms->m_amount);
}

bool System_File::read_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    read_func = read_overlapped;
    return read_overlapped(file, buffer, overlapped, count);
}

bool System_File::write_first(Blizzard::File::StreamRecord* file, void* buffer, OVERLAPPED* overlapped, int32_t* count) {
    write_func = write_overlapped;
    return write_overlapped(file, buffer, overlapped, count);
}

// The browser's native names are UTF-8 already.
void System_File::FromNativeName(char* buffer, uint32_t buffersize, const char* str) {
    Blizzard::String::Copy(buffer, str ? str : "", buffersize);
}

void System_File::Stacked::Throttle(int64_t bytes, int64_t elapsed) {
    if (s_throttleDelay > 0) {
        Blizzard::Process::Sleep(s_throttleDelay);
    }

    s_throttleDebt += static_cast<int64_t>(static_cast<double>(bytes) / static_cast<double>(s_throttleRate) * 1000000000.0f) - elapsed;

    if (s_throttleDebt < -1000000000) {
        s_throttleDebt = -1000000000;
        return;
    }

    if (s_throttleDebt > 10000000) {
        int32_t milliseconds = static_cast<int32_t>(s_throttleDebt / 1000000);
        s_throttleDebt -= milliseconds * 1000000;
        Blizzard::Process::Sleep(milliseconds);
    }
}

Blizzard::File::StreamRecord* FileStack_Streaming::StreamingOpen(Blizzard::File::Filesystem* fs, System_File::Stacked::FileParms* parms) {
    return nullptr;
}

bool System_File::Stacked::stream_close(Blizzard::File::Filesystem* fs, FileParms* parms) {
    Blizzard::File::StreamRecord* file = parms->m_stream;

    if (file->m_streaming) {
        file->m_streaming->~StreamingInfo();
    }

    return fs->m_next ? (*reinterpret_cast<FileFunc*>(reinterpret_cast<uintptr_t>(fs->m_next) + parms->m_verb))(fs->m_next, parms) : false;
}

#endif
