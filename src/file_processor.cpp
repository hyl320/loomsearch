#include "loomlog/file_processor.hpp"

#include "loomlog/text_analyzer.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace loom {

FileResult FileProcessor::process(const std::filesystem::path& path) const {
    FileResult result;
    result.path = path.lexically_normal().string();

    // worker 只生成独立 FileResult，不接触共享倒排索引，避免并发写入竞争。
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "cannot open file: " + result.path;
        return result;
    }

    std::ostringstream content;
    content << file.rdbuf();
    if (file.bad()) {
        result.error = "cannot read file: " + result.path;
        return result;
    }

    const auto text = content.str();
    result.bytes = text.size();
    result.words = tokenize_with_frequency(text);
    result.success = true;
    return result;
}

}  // namespace loom
