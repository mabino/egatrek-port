#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace trek {

// Locates the original game files (TREK.EXE, TREK.DOC, TREK.REF, TREK.SAV).
// DOS names are case-insensitive; Linux and the wasm virtual FS are not.
class DataPath {
public:
    explicit DataPath(std::filesystem::path root) : root_(std::move(root)) {}

    // Root from $TREK_DATA, else nullopt. Requires TREK.EXE to be present.
    static std::optional<DataPath> from_environment();
    static bool looks_like_install(const std::filesystem::path& root);

    const std::filesystem::path& root() const { return root_; }

    // Resolve a file in the root, matching the name case-insensitively.
    std::optional<std::filesystem::path> resolve(std::string_view name) const;

private:
    std::filesystem::path root_;
};

}  // namespace trek
