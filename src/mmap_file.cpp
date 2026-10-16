#include "loomlog/mmap_file.hpp"

#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace loom {

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      is_open_(other.is_open_)
#if defined(_WIN32)
      , file_handle_(other.file_handle_),
      mapping_handle_(other.mapping_handle_)
#else
      , fd_(other.fd_)
#endif
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.is_open_ = false;
#if defined(_WIN32)
    other.file_handle_ = INVALID_HANDLE_VALUE;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        is_open_ = other.is_open_;
#if defined(_WIN32)
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
        other.is_open_ = false;
    }
    return *this;
}

void MemoryMappedFile::close() noexcept {
    if (!is_open_) return;

#if defined(_WIN32)
    if (data_ != nullptr) {
        ::UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (mapping_handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(mapping_handle_));
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != INVALID_HANDLE_VALUE && file_handle_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(file_handle_));
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (data_ != nullptr && size_ > 0) {
        ::munmap(const_cast<char*>(data_), size_);
        data_ = nullptr;
    }
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
#endif

    size_ = 0;
    is_open_ = false;
}

bool MemoryMappedFile::open(const std::filesystem::path& path, std::string* error) {
    close();

#if defined(_WIN32)
    const auto wpath = path.wstring();
    HANDLE file = ::CreateFileW(
        wpath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot open file: " + path.lexically_normal().string();
        return false;
    }

    LARGE_INTEGER file_size;
    if (!::GetFileSizeEx(file, &file_size)) {
        ::CloseHandle(file);
        if (error) *error = "cannot get file size: " + path.lexically_normal().string();
        return false;
    }

    file_handle_ = file;
    size_ = static_cast<std::size_t>(file_size.QuadPart);

    // 0 字节文件不能调用 CreateFileMapping，安全退化为空视图
    if (size_ == 0) {
        data_ = nullptr;
        is_open_ = true;
        return true;
    }

    HANDLE mapping = ::CreateFileMappingW(
        file,
        nullptr,
        PAGE_READONLY,
        0,
        0,
        nullptr
    );

    if (!mapping) {
        ::CloseHandle(file);
        file_handle_ = INVALID_HANDLE_VALUE;
        if (error) *error = "cannot create file mapping: " + path.lexically_normal().string();
        return false;
    }

    mapping_handle_ = mapping;
    void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        ::CloseHandle(mapping);
        ::CloseHandle(file);
        mapping_handle_ = nullptr;
        file_handle_ = INVALID_HANDLE_VALUE;
        if (error) *error = "cannot map view of file: " + path.lexically_normal().string();
        return false;
    }

    data_ = static_cast<const char*>(view);
    is_open_ = true;
    return true;

#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        if (error) *error = "cannot open file: " + path.lexically_normal().string();
        return false;
    }

    struct stat st;
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        if (error) *error = "cannot stat file: " + path.lexically_normal().string();
        return false;
    }

    fd_ = fd;
    size_ = static_cast<std::size_t>(st.st_size);

    if (size_ == 0) {
        data_ = nullptr;
        is_open_ = true;
        return true;
    }

    void* view = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        fd_ = -1;
        if (error) *error = "cannot mmap file: " + path.lexically_normal().string();
        return false;
    }

    data_ = static_cast<const char*>(view);
    is_open_ = true;
    return true;
#endif
}

}  // namespace loom
