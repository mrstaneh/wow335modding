// MpqTool: list, extract and pack MPQ archives with StormLib.
#include <StormLib.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int Usage() {
    fprintf(stderr,
        "usage:\n"
        "  MpqTool list <archive.mpq> [mask]\n"
        "  MpqTool extract <outdir> <mask> <archive.mpq>...   later archives override earlier ones\n"
        "  MpqTool pack <new.mpq> <srcdir>\n"
        "mask example: Interface\\FrameXML\\*\n");
    return 1;
}

static std::basic_string<TCHAR> T(const fs::path& path) {
    return path.string<TCHAR>();
}

static HANDLE OpenArchive(const char* path) {
    HANDLE mpq = nullptr;
    if (!SFileOpenArchive(T(path).c_str(), 0, STREAM_FLAG_READ_ONLY, &mpq)) {
        fprintf(stderr, "cannot open %s (error %u)\n", path, GetLastError());
        return nullptr;
    }
    return mpq;
}

// Calls fn(name, size) for every file in mpq matching mask, skipping the (listfile)-style internal files.
template <typename Fn>
static void ForEachFile(HANDLE mpq, const char* mask, Fn fn) {
    SFILE_FIND_DATA data;
    HANDLE find = SFileFindFirstFile(mpq, mask, &data, nullptr);
    if (!find) {
        return;
    }
    do {
        if (data.cFileName[0] != '(') {
            fn(data.cFileName, data.dwFileSize);
        }
    } while (SFileFindNextFile(find, &data));
    SFileFindClose(find);
}

static int List(const char* archive, const char* mask) {
    HANDLE mpq = OpenArchive(archive);
    if (!mpq) {
        return 1;
    }
    ForEachFile(mpq, mask, [](const char* name, DWORD size) { printf("%10u  %s\n", size, name); });
    SFileCloseArchive(mpq);
    return 0;
}

static int Extract(const fs::path& outDir, const char* mask, char** archives, int count) {
    int failed = 0;
    for (int i = 0; i < count; i++) {
        HANDLE mpq = OpenArchive(archives[i]);
        if (!mpq) {
            return 1;
        }
        int extracted = 0;
        ForEachFile(mpq, mask, [&](const char* name, DWORD) {
            std::string relative = name;
            for (char& c : relative) {
                if (c == '\\') {
                    c = '/';
                }
            }
            fs::path out = outDir / relative;
            fs::create_directories(out.parent_path());
            if (SFileExtractFile(mpq, name, T(out).c_str(), SFILE_OPEN_FROM_MPQ)) {
                extracted++;
            } else {
                fprintf(stderr, "failed to extract %s (error %u)\n", name, GetLastError());
                failed++;
            }
        });
        printf("%s: %d files\n", archives[i], extracted);
        SFileCloseArchive(mpq);
    }
    return failed ? 1 : 0;
}

static int Pack(const fs::path& archive, const fs::path& srcDir) {
    if (fs::exists(archive)) {
        fprintf(stderr, "%s already exists, delete it first\n", archive.string().c_str());
        return 1;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    if (files.empty()) {
        fprintf(stderr, "no files in %s\n", srcDir.string().c_str());
        return 1;
    }

    HANDLE mpq = nullptr;
    DWORD flags = MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2;
    if (!SFileCreateArchive(T(archive).c_str(), flags, static_cast<DWORD>(files.size()) + 16, &mpq)) {
        fprintf(stderr, "cannot create %s (error %u)\n", archive.string().c_str(), GetLastError());
        return 1;
    }
    int failed = 0;
    for (const fs::path& file : files) {
        // MPQ names use backslashes, like Interface\FrameXML\UIParent.lua
        std::string name = fs::relative(file, srcDir).string();
        for (char& c : name) {
            if (c == '/') {
                c = '\\';
            }
        }
        if (!SFileAddFileEx(mpq, T(file).c_str(), name.c_str(), MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
                            MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_NEXT_SAME)) {
            fprintf(stderr, "failed to add %s (error %u)\n", name.c_str(), GetLastError());
            failed++;
        }
    }
    SFileCloseArchive(mpq);
    printf("%s: %d files\n", archive.string().c_str(), static_cast<int>(files.size()) - failed);
    return failed ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        return Usage();
    }
    std::string command = argv[1];
    if (command == "list") {
        return List(argv[2], argc > 3 ? argv[3] : "*");
    }
    if (command == "extract" && argc >= 5) {
        return Extract(argv[2], argv[3], argv + 4, argc - 4);
    }
    if (command == "pack" && argc == 4) {
        return Pack(argv[2], argv[3]);
    }
    return Usage();
}
