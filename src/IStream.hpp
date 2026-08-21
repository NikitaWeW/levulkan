#include <cstdint>

enum class SeekDir : uint8_t {
    Beg,
    End,
    Cur
};
class IReadStream {
public:
    /// @brief Sets read position indicator.
    virtual void seekg(intmax_t position) = 0;
    /// @brief Sets read position indicator relative to @p dir.
    virtual void seekg(intmax_t offset, SeekDir dir) = 0;
    /// @brief Returns read position indicator.
    virtual uintmax_t tellg() = 0;
    /// @brief Returns the size of the stream in bytes.
    virtual uintmax_t size() = 0;
    /// @brief Extracts block of data.
    virtual void read(void *dst, uintmax_t size) = 0;
    /// @brief Synchronizes with the underlying storage device.
    virtual void sync() = 0;
};
class IWriteStream {
public:
    /// @brief Sets write position indicator.
    virtual void seekp(intmax_t position) = 0;
    /// @brief Sets write position indicator relative to @p dir.
    virtual void seekp(intmax_t offset, SeekDir dir) = 0;
    /// @brief Returns write position indicator.
    virtual uintmax_t tellp() = 0;
    /// @brief Inserts block of data.
    virtual void write(void const *src, uintmax_t size) = 0;
    /// @brief Synchronizes with the underlying storage device.
    virtual void flush() = 0;
};

class IStream : public IReadStream, public IWriteStream {};
