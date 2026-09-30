/*
$$\    $$\ $$\   $$\   Vulkan helper functionality.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   
    \_/    \__|  \__|  The vulkan ring buffer.
*/
#pragma once
#include "ECS.hpp"
#include "Resource.hpp"
#include <map>

namespace vk {

/// @brief Vulkan ring buffer
class RingBuffer {
private:
    struct Allocation
    {
        uint32_t head = 0;
        uint32_t size = 0;
    };
    BufferCreateInfo mCreateInfo;
    Entity mBuffer;
    std::map<uint32_t, Allocation> mAllocations;
    uint32_t mHead = 0;
    uint32_t mTail = 0;
public:
    /// @brief Construct an invalid ring buffer.
    RingBuffer() = default;
    /// @brief Construct a valid ring buffer.
    RingBuffer(Registry &reg, BufferCreateInfo createInfo);
    ~RingBuffer();
    RingBuffer(RingBuffer &&) = default;
    RingBuffer &operator=(RingBuffer &&) = default;
    RingBuffer(RingBuffer const &) = delete;
    RingBuffer &operator=(RingBuffer const &) = delete;

    inline bool valid() const { return mBuffer.valid(); }
    inline Entity &getBuffer() { return mBuffer; }
    inline Entity const &getBuffer() const { return mBuffer; }
    inline uint32_t getHead() const { return mHead; }
    inline uint32_t getTail() const { return mTail; }

    /// @brief Acquire a chunk of a ring buffer.
    /// @param size The size of a chunk in bytes.
    /// @param index The allocation index.
    /// @param alignment The alignment of the chunk in bytes.
    /// @returns The start of the chunk.
    uint32_t request(uint32_t size, uint32_t index, uint32_t alignment = 0);

    /// @brief Free the allocation.
    /// @param index The index of the allocation.
    /// WARNING: You must free in the exact same order as requested.
    void free(uint32_t index);

    /// @brief Reallocate if needed.
    /// Place at the end of the frame.
    /// @returns True if reallocation happened, false otherwise.
    bool realloc();
};

} // namespace vk