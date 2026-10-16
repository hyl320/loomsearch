#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace loom {

// 跨平台只读内存映射文件封装。
// 支持 Windows (CreateFileMappingW / MapViewOfFile) 与 POSIX (open / mmap)。
// 当文件大小为 0 时安全退化为空视图（不调用系统 mmap 避免报错）。
class MemoryMappedFile {
public:
    MemoryMappedFile() noexcept = default;
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    // 打开并只读映射指定文件。成功返回 true；失败返回 false 并记录错误原因。
    bool open(const std::filesystem::path& path, std::string* error = nullptr);

    // 显式释放映射与系统句柄。
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] const char* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::string_view string_view() const noexcept {
        return (data_ && size_ > 0) ? std::string_view(data_, size_) : std::string_view{};
    }

private:
    const char* data_{nullptr};
    std::size_t size_{0};
    bool is_open_{false};

#if defined(_WIN32)
    void* file_handle_{reinterpret_cast<void*>(-1)};     // INVALID_HANDLE_VALUE
    void* mapping_handle_{nullptr};
#else
    int fd_{-1};
#endif
};

}  // namespace loom
