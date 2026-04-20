/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   
    \_/    \__|  \__|  The vulkan ring buffer.
*/
#include "vk.hpp"
#include "Logging.hpp"
using namespace vk;

RingBuffer::RingBuffer(BufferCreateInfo createInfo)
{
    mCreateInfo = createInfo;
    mBuffer = vk::makeBuffer(createInfo);
}
RingBuffer::~RingBuffer()
{
    vk::destroy(mBuffer);
}

static uint32_t align(uint32_t value, uint32_t alignment)
{
    if(alignment == 0)
        return value;
    // return (value + alignment - 1) & ~(alignment - 1); // alignment is power of 2
    return value + ((alignment - (value % alignment)) % alignment);
}
uint32_t RingBuffer::request(uint32_t size, uint32_t index, uint32_t alignment)
{
    uint32_t alignedHead = align(mHead, alignment);
    // LOG_TRACE("RingBuffer::request(size = {}, index = {}, alignment = {}); alignedHead = {}", size, index, alignment, alignedHead);
    if(!mAllocations.contains(index))
        mAllocations[index].head = alignedHead;
    mAllocations[index].size += size;

    if(alignedHead >= mTail) {
        // Try end of buffer
        if(alignedHead + size <= mBuffer.createInfo.size) {
            mHead = (alignedHead + size) % mBuffer.createInfo.size;
            return alignedHead;
        }
        // Wrap
        alignedHead = 0;
    }

    // Check space before tail
    if(alignedHead + size <= mTail)
    {
        mHead = (alignedHead + size) % mBuffer.createInfo.size;
        return alignedHead;
    }

    // Out of space
    LOG_WARN("Ring buffer with size of {} is out of memory!", mCreateInfo.size);
    for(auto const &[index, allocation] : mAllocations)
        LOG_WARN("  Allocation at index {} takes [{}; {}]", index, allocation.head, allocation.size);
    
    // Maybe reallocation is already pending
    if(mBuffer.createInfo.size == mCreateInfo.size)
    {
        mCreateInfo.size *= 2;
        LOG_WARN("Stalling and reallocating to {} at the end of the frame.", mCreateInfo.size);
    }

    return 0;
}
void RingBuffer::free(uint32_t index)
{
    if(!mAllocations.contains(index))
        return;

    auto allocation = mAllocations.at(index);
    mAllocations.erase(index);
    mTail = (allocation.head + allocation.size) % mBuffer.createInfo.size;
}
bool RingBuffer::realloc()
{
    if(mBuffer.createInfo.size == mCreateInfo.size)
        return false;

    LOG_WARN("Reallocating RingBuffer to {}...", mCreateInfo.size);
    vkDeviceWaitIdle(mCreateInfo.allocInfo.device);
    vk::destroy(mBuffer);
    mBuffer = vk::makeBuffer(mCreateInfo);
    return true;
}

