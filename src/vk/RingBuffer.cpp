#include "RingBuffer.hpp"
#include "Resource.hpp"
#include "Logging.hpp"
#include "Renderer.hpp"
using namespace vk;

RingBuffer::RingBuffer(Registry reg, BufferCreateInfo createInfo) {
    mCreateInfo = createInfo;
    mBuffer = reg.create(vk::makeBuffer(createInfo));
}
RingBuffer::~RingBuffer() {
    vk::destroy(mBuffer.get<vk::Buffer>());
    mBuffer.destroy();
}

static uint32_t align(uint32_t value, uint32_t alignment) {
    if(alignment == 0)
        return value;
    // return (value + alignment - 1) & ~(alignment - 1); // alignment is power of 2
    return value + ((alignment - (value % alignment)) % alignment);
}
// FIXME: vulkan validation error message:
// vkCmdBindDescriptorSets2(): pBindDescriptorSetsInfo->pDynamicOffsets[0] is 63999872, which when added to the buffer descriptor's range (348) and offset (0) is greater than the size of the buffer (64000000) in descriptorSet #0 binding #0 descriptor[0].
// The Vulkan spec states: For each dynamic uniform or storage buffer binding in pDescriptorSets, the sum of the effective offset and the range of the binding must be less than or equal to the size of the buffer (https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#VUID-VkBindDescriptorSetsInfo-pDescriptorSets-01979)
uint32_t RingBuffer::request(uint32_t size, uint32_t index, uint32_t alignment) {
    uint32_t alignedHead = align(mHead, alignment);
    // LOG_TRACE("RingBuffer::request(size = {}, index = {}, alignment = {}); alignedHead = {}", size, index, alignment, alignedHead);
    if(!mAllocations.contains(index))
        mAllocations[index].head = alignedHead;
    mAllocations[index].size += size;

    auto &buffer = mBuffer.get<vk::Buffer>();

    if(alignedHead >= mTail) {
        // Try end of buffer
        if(alignedHead + size <= buffer.createInfo.size) {
            mHead = (alignedHead + size) % buffer.createInfo.size;
            return alignedHead;
        }
        // Wrap
        alignedHead = 0;
    }

    // Check space before tail
    if(alignedHead + size <= mTail)
    {
        mHead = (alignedHead + size) % buffer.createInfo.size;
        return alignedHead;
    }

    // Out of space
    LOG_WARN("Ring buffer with size of {} is out of memory!", mCreateInfo.size);
    for(auto const &[index, allocation] : mAllocations)
        LOG_WARN("  Allocation at index {} takes [{}; {}]", index, allocation.head, allocation.size);
    
    // Maybe reallocation is already pending
    if(buffer.createInfo.size == mCreateInfo.size)
    {
        mCreateInfo.size *= 2;
        LOG_WARN("Stalling and reallocating to {} at the end of the frame.", mCreateInfo.size);
    }

    return 0;
}
void RingBuffer::free(uint32_t index) {
    if(!mAllocations.contains(index))
        return;

    auto &buffer = mBuffer.get<vk::Buffer>();

    auto allocation = mAllocations.at(index);
    mAllocations.erase(index);
    mTail = (allocation.head + allocation.size) % buffer.createInfo.size;
}
bool RingBuffer::realloc() {
    auto &buffer = mBuffer.get<vk::Buffer>();
    if(buffer.createInfo.size == mCreateInfo.size)
        return false;

    LOG_WARN("Reallocating RingBuffer to {}...", mCreateInfo.size);
    vkDeviceWaitIdle(mCreateInfo.allocInfo.device);
    vk::destroy(buffer);
    buffer = vk::makeBuffer(mCreateInfo);
    mBuffer.emplace<ResourceDirty>();
    return true;
}

