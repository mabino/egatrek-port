#pragma once

#include <cstdio>
#include <filesystem>

namespace trek::dos {

// An open DOS file handle backed by a host file.
struct DosFile {
    std::FILE* fp = nullptr;
    std::filesystem::path path;
    ~DosFile() {
        if (fp) std::fclose(fp);
    }
};

}  // namespace trek::dos
