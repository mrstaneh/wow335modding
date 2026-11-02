// wow335modding: the POSIX file calls of the browser build, with the game data read over HTTP. The Data folder is
// gigabytes of MPQs, too big to load into the page, so paths under /wow/Data/ are read on demand with range requests
// to the page's server (data/...). Everything else (WTF, Cache, logs) lives in Emscripten's in-memory file system.
#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_FILE_WEB_WEBFILE_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_SYSTEM_FILE_WEB_WEBFILE_H

#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <utility>
#include <vector>

namespace WebFile {

int Open(const char* path, int flags, mode_t mode = 0);
ssize_t Read(int fd, void* buffer, size_t count);
ssize_t Pread(int fd, void* buffer, size_t count, off_t offset);
off_t Lseek(int fd, off_t offset, int whence);
int Fstat(int fd, struct stat* st);
int Stat(const char* path, struct stat* st);
int Close(int fd);

// Lists a directory of the game data into (name, is directory) pairs; ok says whether it exists.
// Returns false for paths that are not game data.
// Since the last call: HTTP requests made, bytes downloaded, and the time threads spent waiting on them.
void TakeStats(int32_t& requests, int64_t& bytes, double& waitMs);

bool ListDirectory(const char* path, std::vector<std::pair<std::string, bool>>& entries, bool& ok);

}

#endif
