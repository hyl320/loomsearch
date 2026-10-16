#include "loomlog/file_processor.hpp"

#include "loomlog/mmap_file.hpp"
#include "loomlog/text_analyzer.hpp"

#include <string>

namespace loom {

FileResult FileProcessor::process(const std::filesystem::path& path) const {
    FileResult result;
    result.path = path.lexically_normal().string();

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "cannot open file: " + result.path;
        return result;
    }

    if (file_size > max_file_size_) {
        result.error = "file size exceeds limit (" + std::to_string(file_size) + " > "
            + std::to_string(max_file_size_) + "): " + result.path;
        return result;
    }

    if (file_size == 0) {
        result.bytes = 0;
        result.success = true;
        return result;
    }

    MemoryMappedFile mmap;
    if (!mmap.open(path, &result.error)) {
        return result;
    }

    const auto text = mmap.string_view();
    result.bytes = text.size();
    result.words = tokenize_with_frequency(text);
    result.success = true;
    return result;
}

}  // namespace loom
