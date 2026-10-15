#pragma once

#include "loomlog/file_result.hpp"

#include <cstddef>
#include <filesystem>

namespace loom {

class FileProcessor {
public:
    // 默认大文件限制 10MB
    static constexpr std::size_t kDefaultMaxFileSize = 10 * 1024 * 1024;

    explicit FileProcessor(std::size_t max_file_size = kDefaultMaxFileSize)
        : max_file_size_(max_file_size) {}

    FileResult process(const std::filesystem::path& path) const;

    [[nodiscard]] std::size_t max_file_size() const noexcept { return max_file_size_; }

private:
    std::size_t max_file_size_{kDefaultMaxFileSize};
};

}  // namespace loom
