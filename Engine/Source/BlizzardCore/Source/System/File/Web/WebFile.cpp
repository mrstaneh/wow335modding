// wow335modding: see WebFile.h. HTTP files get descriptors from HTTP_FD_BASE up; reads go through a cache of
// CHUNK_SIZE-aligned chunks fetched with synchronous XHR, which a worker thread (every caller here) may use.
// s_lock guards only the bookkeeping: it is released while a request runs, so threads reading cached data never wait on
// another thread's download, and two threads after the same chunk share one fetch (s_fetching).
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/BlizzardCore/Source/System/File/Web/WebFile.h"
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <map>
#include <mutex>
#include <set>
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
std::set<std::pair<int, int64_t>> s_fetching; // chunks some thread is fetching right now
std::condition_variable s_fetched;

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

// Counters for WebFile::TakeStats.
std::atomic<int32_t> s_statRequests;
std::atomic<int64_t> s_statBytes;
std::atomic<int64_t> s_statWaitUs;

// Runs a request with s_lock released, so other threads keep reading cached data meanwhile.
template <typename Request>
auto Unlocked(std::unique_lock<std::mutex>& lock, Request request) {
    lock.unlock();
    double start = emscripten_get_now();
    auto result = request();
    s_statRequests++;
    s_statWaitUs += static_cast<int64_t>((emscripten_get_now() - start) * 1000.0);
    lock.lock();
    return result;
}

double Size(const std::string& url) {
    std::unique_lock<std::mutex> lock(s_lock);
    auto found = s_sizes.find(url);

    if (found != s_sizes.end()) {
        return found->second;
    }

    double size = url == DATA_URL || url == std::string(DATA_URL) + "/" ? -2 : Unlocked(lock, [&] { return wowHttpSize(url.c_str()); });
    s_sizes[url] = size;
    return size;
}

// Copies part of one chunk into out; the chunk is fetched first if no thread has it yet. Returns the bytes copied, 0
// on a failed fetch. Called with s_lock held.
size_t CopyFromChunk(std::unique_lock<std::mutex>& lock, int id, int64_t fileSize, int64_t position, uint8_t* out, size_t count) {
    std::pair<int, int64_t> key(id, position / CHUNK_SIZE);

    while (true) {
        auto found = s_chunks.find(key);

        if (found != s_chunks.end()) {
            s_lru.splice(s_lru.begin(), s_lru, found->second.m_lru);
            const std::vector<uint8_t>& data = found->second.m_data;
            size_t within = static_cast<size_t>(position % CHUNK_SIZE);
            size_t take = data.size() - within < count ? data.size() - within : count;
            memcpy(out, data.data() + within, take);
            return take;
        }

        if (!s_fetching.count(key)) {
            break;
        }

        s_fetched.wait(lock); // another thread is fetching this chunk
    }

    s_fetching.insert(key);
    int64_t offset = key.second * CHUNK_SIZE;
    int64_t size = fileSize - offset < CHUNK_SIZE ? fileSize - offset : CHUNK_SIZE;
    std::vector<uint8_t> data(size);
    std::string url = s_urls[id]; // a copy: s_urls may grow (and move) while the lock is released
    bool ok = Unlocked(lock, [&] { return wowHttpRead(url.c_str(),static_cast<double>(offset), static_cast<int>(size), data.data()) == size; });
    s_fetching.erase(key);
    s_fetched.notify_all();

    if (!ok) {
        return 0;
    }

    s_statBytes += size;

    if (s_chunks.size() >= MAX_CHUNKS) {
        s_chunks.erase(s_lru.back());
        s_lru.pop_back();
    }

    s_lru.push_front(key);
    Chunk& chunk = s_chunks[key];
    chunk.m_data.swap(data);
    chunk.m_lru = s_lru.begin();
    return CopyFromChunk(lock, id, fileSize, position, out, count);
}

// Called with s_lock held; file is a copy, since s_lock is released while chunks are fetched.
ssize_t ReadAt(std::unique_lock<std::mutex>& lock, HttpFile file, void* buffer, size_t count, int64_t offset) {
    if (offset >= file.m_size) {
        return 0;
    }

    if (static_cast<int64_t>(count) > file.m_size - offset) {
        count = static_cast<size_t>(file.m_size - offset);
    }

    uint8_t* out = static_cast<uint8_t*>(buffer);
    size_t done = 0;

    while (done < count) {
        size_t copied = CopyFromChunk(lock, file.m_id, file.m_size, offset + done, out + done, count - done);

        if (!copied) {
            errno = EIO;
            return done ? static_cast<ssize_t>(done) : -1;
        }

        done += copied;
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

    double size = Size(url);

    if (size == -1) {
        errno = ENOENT;
        return -1;
    }

    std::lock_guard<std::mutex> lock(s_lock);

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

    std::unique_lock<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    ssize_t result = ReadAt(lock, file->second, buffer, count, file->second.m_pos);
    file = s_files.find(fd); // the lock was released during fetches

    if (result > 0 && file != s_files.end()) {
        file->second.m_pos += result;
    }

    return result;
}

ssize_t WebFile::Pread(int fd, void* buffer, size_t count, off_t offset) {
    if (fd < HTTP_FD_BASE) {
        return pread(fd, buffer, count, offset);
    }

    std::unique_lock<std::mutex> lock(s_lock);
    auto file = s_files.find(fd);

    if (file == s_files.end()) {
        errno = EBADF;
        return -1;
    }

    return ReadAt(lock, file->second, buffer, count, offset);
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

void WebFile::TakeStats(int32_t& requests, int64_t& bytes, double& waitMs) {
    requests = s_statRequests.exchange(0);
    bytes = s_statBytes.exchange(0);
    waitMs = s_statWaitUs.exchange(0) / 1000.0;
}

int WebFile::Close(int fd) {
    if (fd < HTTP_FD_BASE) {
        return close(fd);
    }

    std::lock_guard<std::mutex> lock(s_lock);
    return s_files.erase(fd) ? 0 : (errno = EBADF, -1);
}

#endif
