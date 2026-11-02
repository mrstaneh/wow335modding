// wow335modding: see WebFile.h. HTTP files get descriptors from HTTP_FD_BASE up; reads go through a cache of
// CHUNK_SIZE-aligned chunks fetched with synchronous XHR, which a worker thread (every caller here) may use.
// ponytail: one lock around the whole cache serializes concurrent reads; per-file locks if loading threads stall.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/BlizzardCore/Source/System/File/Web/WebFile.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <vector>
#include <emscripten.h>

// Size of the file at url: -1 missing, -2 a directory (the dev server marks those).
EM_JS(double, wowHttpSize, (const char* url), {
    var xhr = new XMLHttpRequest();
    xhr.open('HEAD', UTF8ToString(url), false);
    try {
        xhr.send(null);
    } catch (e) {
        return -1;
    }
    if (xhr.status != 200) return -1;
    if (xhr.getResponseHeader('X-Wow-Directory')) return -2;
    return Number(xhr.getResponseHeader('Content-Length'));
});

// A directory listing from the dev server: one name per line, directories ending in '/'. Returns a malloc'd string.
EM_JS(char*, wowHttpList, (const char* url), {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', UTF8ToString(url), false);
    try {
        xhr.send(null);
    } catch (e) {
        return 0;
    }
    if (xhr.status != 200 || !xhr.getResponseHeader('X-Wow-Directory')) return 0;
    return stringToNewUTF8(xhr.responseText);
});

EM_JS(int, wowHttpRead, (const char* url, double offset, int size, void* dest), {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', UTF8ToString(url), false);
    xhr.responseType = 'arraybuffer';
    xhr.setRequestHeader('Range', 'bytes=' + offset + '-' + (offset + size - 1));
    try {
        xhr.send(null);
    } catch (e) {
        return -1;
    }
    if (xhr.status != 206 && xhr.status != 200) return -1;
    var data = new Uint8Array(xhr.response);
    if (xhr.status == 200) data = data.subarray(offset, offset + size);
    HEAPU8.set(data, dest);
    return data.length;
});

namespace {

const char DATA_ROOT[] = "/wow/Data";
const char DATA_URL[] = "data";
const int HTTP_FD_BASE = 0x40000000;
const int64_t CHUNK_SIZE = 256 * 1024;
const size_t MAX_CHUNKS = 512; // 128 MB

struct HttpFile {
    int m_id;
    int64_t m_size;
    int64_t m_pos;
};

struct Chunk {
    std::vector<uint8_t> m_data;
    std::list<std::pair<int, int64_t>>::iterator m_lru;
};

std::mutex s_lock;
std::map<std::string, double> s_sizes;
std::map<std::string, int> s_ids;
std::vector<std::string> s_urls;
std::map<int, HttpFile> s_files;
int s_nextFd = HTTP_FD_BASE;
std::map<std::pair<int, int64_t>, Chunk> s_chunks;
std::list<std::pair<int, int64_t>> s_lru;

// path made absolute (against the working directory) with "." and ".." resolved.
std::string Absolute(const char* path) {
    std::string full;

    if (path[0] != '/') {
        char cwd[1024];
        full = getcwd(cwd, sizeof(cwd)) ? cwd : "/";
        full += '/';
    }

    full += path;

    std::vector<std::string> parts;
    size_t start = 0;

    while (start <= full.size()) {
        size_t end = full.find('/', start);
        end = end == std::string::npos ? full.size() : end;
        std::string part = full.substr(start, end - start);

        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }

        start = end + 1;
    }

    std::string result;

    for (const std::string& part : parts) {
        result += '/' + part;
    }

    return result.empty() ? "/" : result;
}

// The URL for a path under DATA_ROOT, or false for paths that stay local.
bool DataUrl(const char* path, std::string& url) {
    std::string full = Absolute(path);
    size_t root = sizeof(DATA_ROOT) - 1;

    if (strncasecmp(full.c_str(), DATA_ROOT, root) != 0 || (full[root] != '/' && full[root] != '\0')) {
        return false;
    }

    url = DATA_URL;
    url += full.c_str() + root;
    return true;
}

double Size(const std::string& url) {
    auto found = s_sizes.find(url);

    if (found != s_sizes.end()) {
        return found->second;
    }

    double size = url == DATA_URL || url == std::string(DATA_URL) + "/" ? -2 : wowHttpSize(url.c_str());
    s_sizes[url] = size;
    return size;
}

const Chunk* GetChunk(int id, int64_t index, int64_t fileSize) {
    std::pair<int, int64_t> key(id, index);
    auto found = s_chunks.find(key);

    if (found != s_chunks.end()) {
        s_lru.splice(s_lru.begin(), s_lru, found->second.m_lru);
        return &found->second;
    }

    int64_t offset = index * CHUNK_SIZE;
    int64_t size = fileSize - offset < CHUNK_SIZE ? fileSize - offset : CHUNK_SIZE;
    std::vector<uint8_t> data(size);

    if (wowHttpRead(s_urls[id].c_str(), static_cast<double>(offset), static_cast<int>(size), data.data()) != size) {
        return nullptr;
    }

    if (s_chunks.size() >= MAX_CHUNKS) {
        s_chunks.erase(s_lru.back());
        s_lru.pop_back();
    }

    s_lru.push_front(key);
    Chunk& chunk = s_chunks[key];
    chunk.m_data.swap(data);
    chunk.m_lru = s_lru.begin();
    return &chunk;
}

ssize_t ReadAt(HttpFile& file, void* buffer, size_t count, int64_t offset) {
    if (offset >= file.m_size) {
        return 0;
    }

    if (static_cast<int64_t>(count) > file.m_size - offset) {
        count = static_cast<size_t>(file.m_size - offset);
    }

    uint8_t* out = static_cast<uint8_t*>(buffer);
    size_t done = 0;

    while (done < count) {
        int64_t position = offset + done;
        const Chunk* chunk = GetChunk(file.m_id, position / CHUNK_SIZE, file.m_size);

        if (!chunk) {
            errno = EIO;
            return done ? static_cast<ssize_t>(done) : -1;
        }

        size_t within = static_cast<size_t>(position % CHUNK_SIZE);
        size_t take = chunk->m_data.size() - within < count - done ? chunk->m_data.size() - within : count - done;
        memcpy(out + done, chunk->m_data.data() + within, take);
        done += take;
    }

    return static_cast<ssize_t>(done);
}

void FillStat(struct stat* st, double size) {
    memset(st, 0, sizeof(*st));
    st->st_mode = size == -2 ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_size = size < 0 ? 0 : static_cast<off_t>(size);
    st->st_nlink = 1;
}

}

int WebFile::Open(const char* path, int flags, mode_t mode) {
    std::string url;

    if (!DataUrl(path, url)) {
        return open(path, flags, mode);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    double size = Size(url);

    if (size == -1) {
        errno = ENOENT;
        return -1;
    }

    if ((flags & O_ACCMODE) != O_RDONLY || size == -2) {
        errno = size == -2 ? EISDIR : EROFS;
        return -1;
    }

    auto id = s_ids.find(url);

    if (id == s_ids.end()) {
        id = s_ids.emplace(url, static_cast<int>(s_urls.size())).first;
        s_urls.push_back(url);
    }

    int fd = s_nextFd++;
    s_files[fd] = HttpFile { id->second, static_cast<int64_t>(size), 0 };
    return fd;
}

ssize_t WebFile::Read(int fd, void* buffer, size_t count) {
    if (fd < HTTP_FD_BASE) {
        return read(fd, buffer, count);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    ssize_t result = ReadAt(file->second, buffer, count, file->second.m_pos);

    if (result > 0) {
        file->second.m_pos += result;
    }

    return result;
}

ssize_t WebFile::Pread(int fd, void* buffer, size_t count, off_t offset) {
    if (fd < HTTP_FD_BASE) {
        return pread(fd, buffer, count, offset);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    return ReadAt(file->second, buffer, count, offset);
}

off_t WebFile::Lseek(int fd, off_t offset, int whence) {
    if (fd < HTTP_FD_BASE) {
        return lseek(fd, offset, whence);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    int64_t base = whence == SEEK_CUR ? file->second.m_pos : whence == SEEK_END ? file->second.m_size : 0;

    if (base + offset < 0) {
        errno = EINVAL;
        return -1;
    }

    file->second.m_pos = base + offset;
    return static_cast<off_t>(file->second.m_pos);
}

int WebFile::Fstat(int fd, struct stat* st) {
    if (fd < HTTP_FD_BASE) {
        return fstat(fd, st);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    FillStat(st, static_cast<double>(file->second.m_size));
    return 0;
}

int WebFile::Stat(const char* path, struct stat* st) {
    std::string url;

    if (!DataUrl(path, url)) {
        return stat(path, st);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    double size = Size(url);

    if (size == -1) {
        errno = ENOENT;
        return -1;
    }

    FillStat(st, size);
    return 0;
}

bool WebFile::ListDirectory(const char* path, std::vector<std::pair<std::string, bool>>& entries, bool& ok) {
    std::string url;

    if (!DataUrl(path, url)) {
        return false;
    }

    char* listing = wowHttpList(url.c_str());
    ok = listing != nullptr;

    for (const char* line = listing; line && *line;) {
        const char* end = strchr(line, '\n');
        std::string name(line, end ? end - line : strlen(line));
        bool isDir = !name.empty() && name.back() == '/';

        if (isDir) {
            name.pop_back();
        }

        if (!name.empty()) {
            entries.emplace_back(name, isDir);
        }

        line = end ? end + 1 : line + strlen(line);
    }

    free(listing);
    return true;
}

int WebFile::Close(int fd) {
    if (fd < HTTP_FD_BASE) {
        return close(fd);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    return s_files.erase(fd) ? 0 : (errno = EBADF, -1);
}

#endif
