#include "engine/renderer/RenderGraph.hpp"
#include "tracy/Tracy.hpp"

using namespace egn;

enum class NodeState { None = 0, Visited, Added };

struct Pass {
    std::string name;
    uint index = 0; // Number in the pass stack
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
    std::vector<Barrier> barriers;
    VkQueueFlagBits queue;
    std::unique_ptr<IRenderPassStorage> storage;
};
struct ResourceCreateInfo {
    enum Type { Unknown, External, Image, Buffer } type = Unknown;
    union {
        RenderGraphExternalResourceInfo external;
        RenderGraphImageCreateInfo image;
        RenderGraphBufferCreateInfo buffer;
    };

    ResourceCreateInfo() {
    }
    ResourceCreateInfo(ResourceCreateInfo const &o) {
        *this = o;
    }
    ResourceCreateInfo(ResourceCreateInfo &&o) {
        *this = std::move(o);
    }
    ResourceCreateInfo &operator=(ResourceCreateInfo const &o) {
        type = o.type;
        switch(type) {
            case External: external = o.external; break;
            case Image: image = o.image; break;
            case Buffer: buffer = o.buffer; break;
            case Unknown: break;
        }
        return *this;
    }
    ResourceCreateInfo &operator=(ResourceCreateInfo &&o) {
        type = std::move(o.type);
        switch(type) {
            case External: external = std::move(o.external); break;
            case Image: image = std::move(o.image); break;
            case Buffer: buffer = std::move(o.buffer); break;
            case Unknown: break;
        }
        return *this;
    }
    ~ResourceCreateInfo() {
    }
    ResourceCreateInfo(RenderGraphExternalResourceInfo const &info) {
        type = External;
        external = info;
    }
    ResourceCreateInfo(RenderGraphImageCreateInfo const &info) {
        type = Image;
        image = info;
    }
    ResourceCreateInfo(RenderGraphBufferCreateInfo const &info) {
        type = Buffer;
        buffer = info;
    }
};
struct ResourceAllocationInfo {
    ResourceCreateInfo ci;
    VkImageUsageFlags imageUsage = 0;
    VkBufferUsageFlags bufferUsage = 0;
    VkDeviceSize bufferSize = 0;
};
struct ResourceInfo {
    std::string name;

    Entity eResource;
    bool keepImage = false;
    ResourceCreateInfo ci;

    // Indices in the pass stack
    uint lifetimeBegin = std::numeric_limits<uint>::max();
    uint lifetimeEnd = 0;

    std::vector<ResourceUsage> read;
    ResourceUsage written;
};
// To pass intermediate data between functions
struct egn::_impl::RenderGraphImpl {
    vk::AllocationCreateInfo allocInfo;
    Registry *reg = nullptr;
    vk::QueueFamilies queueFamilies;

    std::unordered_set<Entity> resourcePool;
    std::unordered_map<std::string, NodeState> nodeState;
    std::unordered_map<std::string, Pass> passes;
    std::unordered_map<std::string, ResourceInfo> resources;
    std::vector<std::string> passStack;
};

struct egn::RenderGraphResultImpl {
    std::unordered_map<std::string, RenderPass> passes;
    std::unordered_map<std::string, ResourceInfo> resources;
    std::vector<std::string> passStack;
};
struct PleaseKeepTheImageContents { };

void RenderGraphBuilder::removePass(std::string const &name) {
    if(!passes.contains(std::string(name))) {
        LOG_ERROR("Cannot remove pass \"{}\": does not exist!");
        return;
    }

    passes.erase(std::string(name));
}

bool RenderGraphResourceBuilder::addExternalResource(std::string const &name, RenderGraphExternalResourceInfo info) {
    if(mExternalResources.contains(std::string(name))) {
        LOG_ERROR("External resource \"{}\" already exists!", name);
        return false;
    }
    mExternalResources[std::string(name)] = info;
    return true;
}
bool RenderGraphResourceBuilder::addImageResource(std::string const &name, RenderGraphImageCreateInfo info) {
    if(mImageResources.contains(std::string(name))) {
        LOG_ERROR("Image resource \"{}\" already exists!", name);
        return false;
    }
    mImageResources[std::string(name)] = info;
    return true;
}
bool RenderGraphResourceBuilder::addBufferResource(std::string const &name, RenderGraphBufferCreateInfo info) {
    if(mBufferResources.contains(std::string(name))) {
        LOG_ERROR("Buffer resource \"{}\" already exists!", name);
        return false;
    }
    mBufferResources[std::string(name)] = info;
    return true;
}
bool RenderGraphResourceBuilder::contains(std::string const &name) const {
    return mImageResources.contains(name) || mExternalResources.contains(name) || mBufferResources.contains(name);
}

std::unordered_map<std::string, RenderGraphExternalResourceInfo> const &RenderGraphResourceBuilder::getExternal() const {
    return mExternalResources;
}
std::unordered_map<std::string, RenderGraphImageCreateInfo> const &RenderGraphResourceBuilder::getImage() const {
    return mImageResources;
}
std::unordered_map<std::string, RenderGraphBufferCreateInfo> const &RenderGraphResourceBuilder::getBuffer() const {
    return mBufferResources;
}

RenderPassBuilder::RenderPassBuilder(RenderGraphResourceBuilder *resourceBuilder) {
    mResourceBuilder = resourceBuilder;
}
RenderGraphResourceBuilder *RenderPassBuilder::resource() {
    return mResourceBuilder;
}
void RenderPassBuilder::attachResourceRead(std::string_view resourceName, ResourceTraits traits) {
    mReads.emplace_back(std::string(resourceName), traits);
}
void RenderPassBuilder::attachResourceWrite(std::string_view resourceName, ResourceTraits traits) {
    mWrites.emplace_back(std::string(resourceName), traits);
}

void RenderGraphBuilder::setAllocInfo(vk::AllocationCreateInfo const &allocInfo, Registry &reg) {
    this->allocInfo = allocInfo;
    this->reg = &reg;
}
void RenderGraphBuilder::setQueueFamilies(vk::QueueFamilies const &queueFamilies) {
    this->queueFamilies = queueFamilies;
}

RenderGraphResult::RenderGraphResult(RenderGraphResultImpl *data) {
    mImpl = data;
}
RenderGraphResult::~RenderGraphResult() {
    if(mImpl)
        delete mImpl;
}
RenderGraphResult::RenderGraphResult(RenderGraphResult &&rhs) {
    *this = std::move(rhs);
}
RenderGraphResult &RenderGraphResult::operator=(RenderGraphResult &&rhs) {
    std::swap(mImpl, rhs.mImpl);
    return *this;
}
bool RenderGraphResult::success() const {
    return mImpl;
}
void RenderGraphResult::setResource(std::string const &name, Entity eResource) {
    assert(success());

    if(!mImpl->resources.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain resource \"{}\"", name);
        return;
    }

    auto &res = mImpl->resources.at(std::string(name));
    if(res.ci.type != ResourceCreateInfo::External) {
        LOG_ERROR("Cannot set resource \"{}\", because it is not external!");
        return;
    }
    res.eResource = eResource;
}
Entity RenderGraphResult::getResource(std::string const &name) const {
    assert(success());

    if(!mImpl->resources.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain resource \"{}\"", name);
        return Entity();
    }

    return mImpl->resources.at(std::string(name)).eResource;
}
RenderPass const *RenderGraphResult::getPass(std::string const &name) const {
    assert(success());

    if(!mImpl->passes.contains(std::string(name))) {
        LOG_ERROR("Render graph does not contain pass \"{}\"", name);
        return nullptr;
    }

    return &mImpl->passes.at(std::string(name));
}
std::vector<std::string> const &RenderGraphResult::getPassStack() const {
    assert(success());

    return mImpl->passStack;
}

static std::string collapseAttributes(std::vector<std::string> const &attributes) {
    std::string res;
    for(auto const &attrib : attributes)
        res.append("[").append(attrib).append("]");
    return res;
}
std::string RenderGraphResult::dumpGraphviz(int indent, GraphvizSettings settings) const {
    ZoneScoped;

    assert(success());

    std::stringstream ss;
    auto newline = [indent](int i) { return (indent >= 0) ? ("\n" + std::string(i, ' ')) : " "; };

    ss << "digraph RenderGraph {";
    for(auto const &attrib : settings.graphAttributes)
        ss << newline(indent) << attrib << ";";

    std::string nodeAttributes = collapseAttributes(settings.nodeAttributes);
    std::string edgeAttributes = collapseAttributes(settings.edgeAttributes);

    for(auto passName : mImpl->passStack) {
        auto const &pass = mImpl->passes.at(passName);
        passName.erase(std::remove(passName.begin(), passName.end(), '\"'), passName.end());

        ss << newline(indent) << "\"" << passName << "\" " << nodeAttributes << ";";

        for(auto [resourceName, _] : pass.reads) {
            auto &resource = mImpl->resources.at(resourceName);
            resourceName.erase(std::remove(resourceName.begin(), resourceName.end(), '\"'), resourceName.end());
            ss << newline(indent) << fmt::format("\"{}\" -> \"{}\" [label=\"{}\"]{}", resource.written.name, passName, resourceName, edgeAttributes);
        }
    }

    // ss << newline(indent) << "subgraph Legend {";
    // ss << newline(indent*2) << "pinned=cluster;";
    // ss << newline(indent*2) << "lp=\"0,5!\";";
    // ss << newline(indent*2) << "A [label=<" << makeHtmlTimeline(*mImpl) << ">;]";
    // ss << newline(indent) << "}";

    // std::string title;
    // for(auto index : mPassStack)
    //     title.append(mPasses.get(index).name).append(" -> ");
    // title.erase(title.size() - std::string_view(" -> ").size());
    // ss << newline(indent) << newline(indent) << "label = \"" << title << "\";";

    ss << newline(0) << "}";

    return ss.str();
}

bool egn::_impl::setupRenderGraph(RenderGraphBuilder &&builder, void *data, RenderGraphImpl &renderGraph) {
    ZoneScoped;

    assert(builder.fresh && "Cannot use same RenderGraphBuilder twice!");
    builder.fresh = false;
    if(builder.allocInfo.has_value()) {
        renderGraph.reg = std::move(builder.reg);
        renderGraph.allocInfo = std::move(builder.allocInfo.value());
    }
    assert(builder.queueFamilies.has_value() && "Forgot to call RenderGraphBuilder::setQueueFamilies?");
    renderGraph.queueFamilies = std::move(builder.queueFamilies.value());

    RenderGraphResourceBuilder resourceBuilder = std::move(builder.mResources);

    for(auto &&[passName, pass] : builder.passes) {
        if(renderGraph.passes.contains(passName)) {
            LOG_ERROR("Render graph already contains pass \"{}\"", passName);
            return false;
        }

        auto &renderGraphPass = renderGraph.passes[passName] = {
            .name = std::move(pass.name),
            .queue = std::move(pass.queue),
            .storage = std::move(pass.storage),
        };

        LOG_TRACE("Adding pass {} queue {}", renderGraphPass.name, string_VkQueueFlagBits(renderGraphPass.queue));

        RenderPassBuilder passBuilder(&resourceBuilder);
        renderGraphPass.storage->setup(passBuilder, data);
        for(auto const &[name, traits] : passBuilder.mWrites) {
            LOG_TRACE("  Writes {} {}", name, string_VkAccessFlags2(traits.access));
            auto &resourceInfo = renderGraph.resources[name];

            if(!resourceInfo.written.name.empty()) {
                LOG_ERROR("Cannot write to resource \"{}\" more than once!(error in pass \"{}\")", name, renderGraphPass.name);
                return false;
            }
            resourceInfo.name = name;
            resourceInfo.written = { renderGraphPass.name, traits };

            renderGraphPass.writes.emplace_back(name, traits);
        }
        for(auto const &[name, traits] : passBuilder.mReads) {
            LOG_TRACE("  Reads {} {}", name, string_VkAccessFlags2(traits.access));
            auto &resourceInfo = renderGraph.resources[name];
            if(resourceInfo.written.name == renderGraphPass.name) {
                LOG_ERROR("Cannot read and write to the same resource \"{}\". Use different names!(error in pass \"{}\")",
                    name,
                    renderGraphPass.name);
                return false;
            }

            resourceInfo.name = name;
            resourceInfo.read.emplace_back(renderGraphPass.name, traits);

            renderGraphPass.reads.emplace_back(name, traits);
        }
    }

    for(auto const &[name, info] : resourceBuilder.getExternal()) {
        LOG_TRACE("  External resource {} {}", name, info.eResource);
        if(renderGraph.resources.contains(name) && renderGraph.resources.at(name).ci.type != ResourceCreateInfo::Unknown) {
            LOG_ERROR("Resoure \"{}\" of different type already exists", name);
            return false;
        }

        auto &resourceInfo = renderGraph.resources[name];
        resourceInfo.name = name;
        resourceInfo.eResource = info.eResource;
        resourceInfo.ci = ResourceCreateInfo(info);
    }
    for(auto const &[name, info] : resourceBuilder.getImage()) {
        LOG_TRACE("  Image resource {} {} {} {}x{}x{} resize to target {} -> {}x{} allow reuse {}",
            name,
            string_VkFormat(info.imageInfo.format),
            string_VkImageType(info.imageInfo.imageType),
            info.imageInfo.dimensions.width,
            info.imageInfo.dimensions.height,
            info.imageInfo.dimensions.depth,
            info.resizeToTarget,
            info.resizeRatio.x,
            info.resizeRatio.y,
            !info.keep);
        if(renderGraph.resources.contains(name) && renderGraph.resources.at(name).ci.type != ResourceCreateInfo::Unknown) {
            LOG_ERROR("Resoure \"{}\" of different type already exists", name);
            return false;
        }

        auto &resourceInfo = renderGraph.resources[name];
        resourceInfo.name = name;
        resourceInfo.ci = info;
    }
    for(auto const &[name, info] : resourceBuilder.getBuffer()) {
        LOG_TRACE("  Buffer resource {} {} bytes", name, info.size);
        if(renderGraph.resources.contains(name) && renderGraph.resources.at(name).ci.type != ResourceCreateInfo::Unknown) {
            LOG_ERROR("Resoure \"{}\" of different type already exists", name);
            return false;
        }

        auto &resourceInfo = renderGraph.resources[name];
        resourceInfo.name = name;
        resourceInfo.ci = info;
    }

    for(auto const &[_, pass] : renderGraph.passes) {
        for(auto const &[res, _] : pass.writes) {
            if(!(renderGraph.resources.contains(res) && renderGraph.resources.at(res).ci.type != ResourceCreateInfo::Unknown)) {
                LOG_ERROR("Pass \"{}\" writes to unknown resource \"{}\"", pass.name, res);
                return false;
            }
        }
        for(auto const &[res, _] : pass.reads) {
            if(!(renderGraph.resources.contains(res) && renderGraph.resources.at(res).ci.type != ResourceCreateInfo::Unknown)) {
                LOG_ERROR("Pass \"{}\" reads from unknown resource \"{}\"", pass.name, res);
                return false;
            }
        }
    }

    return true;
}

// Simplified version of vk::RenderGraph's processPass
static bool processPass(std::string const &passName, _impl::RenderGraphImpl &renderGraph, uint depth, bool backtrack = false) {
    ZoneScoped;

    assert(!passName.empty());

    if(depth == 0) {
        LOG_ERROR("Cycle detected!");
        return false;
    }

    auto &nodeState = renderGraph.nodeState[passName];
    if(nodeState != NodeState::None)
        return true;

    auto &pass = renderGraph.passes.at(passName);

    // Cull passes results of which are not used anywhere,
    // Unless the resource is external
    bool used = false;
    for(auto const &resource : pass.writes) {
        auto const &info = renderGraph.resources.at(resource.name);
        if(!info.read.empty() || info.ci.type == ResourceCreateInfo::External) {
            used = true;
            break;
        }
    }
    if(!used) {
        return true;
    }

    for(auto const &resource : pass.reads) {
        auto const &info = renderGraph.resources.at(resource.name);
        if(!processPass(info.written.name, renderGraph, depth - 1, true)) {
            return false;
        }
    }

    if(nodeState != NodeState::Added) {
        nodeState = NodeState::Added;
        pass.index = renderGraph.passStack.size();
        renderGraph.passStack.emplace_back(pass.name);
        if(backtrack)
            return true;
    }

    for(auto const &resource : pass.writes) {
        auto const &info = renderGraph.resources.at(resource.name);
        for(auto const &[dependencyName, _] : info.read)
            if(!processPass(dependencyName, renderGraph, depth - 1, backtrack))
                return false;
    }

    return true;
}
static std::string printDependencies(Pass const &pass) {
    std::string reads;
    for(auto [name, traits] : pass.reads)
        reads.append(name).append("; ");
    if(reads.empty())
        reads = "none";
    std::string writes;
    for(auto [name, traits] : pass.writes)
        reads.append(name).append("; ");
    if(writes.empty())
        writes = "none";

    return fmt::format("reads {:>40} | writes {:>40}", reads, writes);
}
static void calculateLifetimes(_impl::RenderGraphImpl &renderGraph) {
    ZoneScoped;

    for(auto &[_, resource] : renderGraph.resources) {
        for(auto const &[passName, traits] : resource.read) {
            auto const &pass = renderGraph.passes.at(passName);
            resource.lifetimeEnd = std::max(resource.lifetimeEnd, pass.index);
        }
        if(!resource.written.name.empty()) {
            auto const &pass = renderGraph.passes.at(resource.written.name);
            resource.lifetimeBegin = std::min(resource.lifetimeBegin, pass.index);
        }
    }
}
static bool isCompatible(RenderGraphImageCreateInfo const &first, RenderGraphImageCreateInfo const &second) {
    return // oh god here we go again
        first.resizeToTarget == second.resizeToTarget && vecEquals(first.resizeRatio, second.resizeRatio) &&
        first.imageInfo.imageType == second.imageInfo.imageType && first.imageInfo.format == second.imageInfo.format &&
        (first.resizeToTarget || (first.imageInfo.dimensions.width == second.imageInfo.dimensions.width &&
                                     first.imageInfo.dimensions.height == second.imageInfo.dimensions.height &&
                                     first.imageInfo.dimensions.depth == second.imageInfo.dimensions.depth &&
                                     first.imageInfo.dimensions.mipLevels == second.imageInfo.dimensions.mipLevels &&
                                     first.imageInfo.dimensions.arrayLayers == second.imageInfo.dimensions.arrayLayers &&
                                     first.imageInfo.dimensions.samples == second.imageInfo.dimensions.samples)) &&
        (first.imageInfo.sampler.flags & second.imageInfo.sampler.flags) && first.imageInfo.sampler.magFilter == second.imageInfo.sampler.magFilter &&
        first.imageInfo.sampler.minFilter == second.imageInfo.sampler.minFilter &&
        first.imageInfo.sampler.mipmapMode == second.imageInfo.sampler.mipmapMode && // still... going...
        first.imageInfo.sampler.addressModeU == second.imageInfo.sampler.addressModeU &&
        first.imageInfo.sampler.addressModeV == second.imageInfo.sampler.addressModeV &&
        first.imageInfo.sampler.addressModeW == second.imageInfo.sampler.addressModeW &&
        std::abs(first.imageInfo.sampler.mipLodBias - second.imageInfo.sampler.mipLodBias) <= 1e-6 &&
        first.imageInfo.sampler.anisotropyEnable == second.imageInfo.sampler.anisotropyEnable &&
        std::abs(first.imageInfo.sampler.maxAnisotropy - second.imageInfo.sampler.maxAnisotropy) <= 1e-6 &&
        first.imageInfo.sampler.compareEnable == second.imageInfo.sampler.compareEnable &&
        first.imageInfo.sampler.compareOp == second.imageInfo.sampler.compareOp &&
        std::abs(first.imageInfo.sampler.minLod - second.imageInfo.sampler.minLod) <= 1e-6 &&
        first.imageInfo.sampler.borderColor == second.imageInfo.sampler.borderColor &&
        std::abs(first.imageInfo.sampler.customBorderColor.r - second.imageInfo.sampler.customBorderColor.r) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.g - second.imageInfo.sampler.customBorderColor.g) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.b - second.imageInfo.sampler.customBorderColor.b) <= 1e-6 &&
        std::abs(first.imageInfo.sampler.customBorderColor.a - second.imageInfo.sampler.customBorderColor.a) <= 1e-6 &&
        first.imageInfo.sampler.unnormalizedCoordinates == second.imageInfo.sampler.unnormalizedCoordinates;
} // let's never do THAT again...
// Additionally checks the type of an external resource
static ResourceCreateInfo::Type getResourceType(RestrictedEntityAny<ResourceCreateInfo> e) {
    ZoneScoped;

    auto const &ci = e.get<ResourceCreateInfo>();
    if(ci.type == ResourceCreateInfo::External) {
        assert(e.contains<vk::Image>() || e.contains<vk::Buffer>());
        return e.contains<vk::Image>() ? ResourceCreateInfo::Image : ResourceCreateInfo::Buffer;
    } else {
        return ci.type;
    }
}
static Entity findPoolResource(ResourceCreateInfo const &ci, _impl::RenderGraphImpl &renderGraph, VkImageUsageFlags usage = 0) {
    ZoneScoped;

    for(auto poolRes : renderGraph.resourcePool) {
        assert(poolRes);
        assert(poolRes.contains<ResourceCreateInfo>());
        auto type = getResourceType(poolRes);

        if(type != ci.type)
            continue;

        // External resources must mach the usage
        if(poolRes.contains<vk::Image>() && (poolRes.get<vk::Image>().createInfo.usage & usage) != usage) {
            continue;
        }

        auto const &poolCi = poolRes.get<ResourceCreateInfo>();

        if(ci.type == ResourceCreateInfo::Image && !isCompatible(ci.image, poolCi.image)) {
            continue;
        }

        renderGraph.resourcePool.erase(poolRes);
        return poolRes;
    }

    return Entity();
}
static void aliasResources(_impl::RenderGraphImpl &renderGraph) {
    ZoneScoped;

    LOG_TRACE("Aliasing resources");
    for(uint i = 0; i < renderGraph.passStack.size(); ++i) {
        auto const &pass = renderGraph.passes.at(renderGraph.passStack.at(i));

        LOG_TRACE("  Processing pass {}", pass.name);

        // Allocate
        for(auto const &[resourceName, traits] : pass.writes) {
            auto &resource = renderGraph.resources.at(resourceName);
            auto &e = resource.eResource;

            // TODO: external resources are not used before their lifetimes
            if(resource.ci.type == ResourceCreateInfo::External)
                continue;

            assert(!e.valid());
            e = findPoolResource(resource.ci, renderGraph);

            if(!e.valid()) {
                assert(renderGraph.reg && "Should have called RenderGraphBuilder::setAllocInfo!");
                e = renderGraph.reg->create(resource.ci);
                e.name(resourceName);
                if(resource.ci.type == ResourceCreateInfo::Image && resource.ci.image.keep) {
                    e.emplace<PleaseKeepTheImageContents>();
                }
                LOG_TRACE("    Adding new pool resource {} for {} {}", e, resourceName, string_VkAccessFlagBits2(traits.access));
            } else {
                LOG_TRACE("    Found pool resource {} for resource {} {}", e, resourceName, string_VkAccessFlagBits2(traits.access));
                e.name(std::string(e.name()) + "," + resourceName);
            }

            if(!e.contains<ResourceAllocationInfo>()) {
                e.emplace<ResourceAllocationInfo>(ResourceAllocationInfo{ .ci = resource.ci });
            }

            auto &allocInfo = e.get<ResourceAllocationInfo>();
            allocInfo.imageUsage |= traits.imageTraits.usage;
            allocInfo.bufferUsage |= traits.bufferTraits.usage;
            allocInfo.bufferSize = std::max<VkDeviceSize>(allocInfo.bufferSize, traits.bufferTraits.size + traits.bufferTraits.offset);
        }

        // Free
        for(auto const &[resourceName, traits] : pass.reads) {
            auto &resource = renderGraph.resources.at(resourceName);
            // Dummy resources may not be recycled
            if(!resource.eResource)
                continue;
            auto const &ci = resource.ci;

            if(resource.eResource.contains<ResourceAllocationInfo>()) {
                auto &allocInfo = resource.eResource.get<ResourceAllocationInfo>();
                if(ci.type == ResourceCreateInfo::Image) {
                    allocInfo.imageUsage |= traits.imageTraits.usage;
                } else {
                    allocInfo.bufferUsage |= traits.bufferTraits.usage;
                }
            }

            // May not use resources if the contents are used in the next frame
            if(ci.type == ResourceCreateInfo::Image && ci.image.keep)
                continue;
            if(ci.type == ResourceCreateInfo::External && ci.external.keep)
                continue;

            if(resource.lifetimeEnd <= i) {
                LOG_TRACE("    Freeing resource {} {}", resource.name, resource.eResource);
                renderGraph.resourcePool.emplace(resource.eResource);
            }
        }
    }
}
static void allocateResources(_impl::RenderGraphImpl &renderGraph) {
    ZoneScoped;

    LOG_TRACE("Allocating resources");
    if(!renderGraph.reg)
        return;
    for(auto e : renderGraph.reg->view<ResourceAllocationInfo>()) {
        auto allocInfo = e.get<ResourceAllocationInfo>();
        e.erase<ResourceAllocationInfo>();
        e.emplace<RenderGraphResource>();

        assert(allocInfo.ci.type == ResourceCreateInfo::Image || allocInfo.ci.type == ResourceCreateInfo::Buffer);

        if(allocInfo.ci.type == ResourceCreateInfo::Image) {
            LOG_TRACE("  Allocating image resource {} for {}", static_cast<Entity &>(e), string_VkImageUsageFlags(allocInfo.imageUsage));
            assert(allocInfo.imageUsage != 0);
            vk::ImageCreateInfo imageCreateInfo{
                .usage = allocInfo.imageUsage,
                .allocInfo = renderGraph.allocInfo,
                .image = allocInfo.ci.image.imageInfo,
            };
            imageCreateInfo.name = e.name();

            if(allocInfo.ci.image.resizeToTarget) {
                e.emplace<ResizeToTarget>(allocInfo.ci.image.resizeRatio);
                e.emplace<vk::ImageCreateInfo>(imageCreateInfo);
            } else { // Deffer the image creation.
                e.emplace<vk::Image>(vk::makeImage(imageCreateInfo));
                assert(e.get<vk::Image>().image);
                assert(e.get<vk::Image>().view);
            }
        } else {
            LOG_TRACE("  Allocating buffer resource {} for {} {} bytes",
                static_cast<Entity &>(e),
                string_VkBufferUsageFlags(allocInfo.bufferUsage),
                allocInfo.bufferSize);
            assert(allocInfo.bufferUsage != 0);
            assert(allocInfo.bufferSize != 0);
            vk::BufferCreateInfo bufferCreateInfo{
                .usage = allocInfo.bufferUsage,
                .allocInfo = renderGraph.allocInfo,
                .size = allocInfo.bufferSize,
                .map = false,
            };
            bufferCreateInfo.name = e.name();

            e.emplace<vk::Buffer>(vk::makeBuffer(bufferCreateInfo));
        }
    }
}
static bool buildBarriers(_impl::RenderGraphImpl &renderGraph) {
    ZoneScoped;

    std::unordered_map<Entity, Barrier::Scope> lastScope;
    for(auto const &passName : renderGraph.passStack) {
        auto const &pass = renderGraph.passes.at(passName);
        for(auto const &[resourceName, traits] : pass.reads) {
            auto const &resource = renderGraph.resources.at(resourceName);

            uint32_t queue = VK_QUEUE_FAMILY_IGNORED;
            if(!renderGraph.queueFamilies.indices.contains(pass.queue)) {
                LOG_ERROR("Pass \"{}\" needs queue {} which is not in queue families provided to the frame graph!",
                    pass.name,
                    string_VkQueueFlagBits(pass.queue));
                return false;
            } else {
                queue = renderGraph.queueFamilies.indices.at(pass.queue);
            }
            lastScope[resource.eResource] = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,
            };
        }
    }

    std::unordered_map<Entity, Barrier::Scope> resourceState;
    for(auto const &passName : renderGraph.passStack) {
        auto &pass = renderGraph.passes.at(passName);
        pass.barriers.reserve(pass.reads.size() + pass.writes.size());
        uint32_t queue = renderGraph.queueFamilies.indices.at(pass.queue);

        for(auto const &[resourceName, traits] : pass.reads) {
            auto const &resource = renderGraph.resources.at(resourceName);
            bool history = resource.eResource.contains<PleaseKeepTheImageContents>() && !resourceState.contains(resource.eResource);
            auto &state = resourceState[resource.eResource];
            auto prevState = history ? lastScope.at(resource.eResource) : state;

            state = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,
            };
            pass.barriers.emplace_back(Barrier{
                .resource = resource.name,
                .src = prevState,
                .dst = state,
                .subresourceRange = traits.imageTraits.subresourceRange,
                .offset = traits.bufferTraits.offset,
                .size = traits.bufferTraits.size,
            });
        }
        for(auto const &[resourceName, traits] : pass.writes) {
            auto const &resource = renderGraph.resources.at(resourceName);
            auto &state = resourceState[resource.eResource];
            auto prevState = state;

            state = {
                .layout = traits.imageTraits.layout,
                .bufferUsage = traits.bufferTraits.usage,
                .imageUsage = traits.imageTraits.usage,
                .queueIndex = queue,
                .access = traits.access,
                .stages = traits.stages,
            };
            pass.barriers.emplace_back(Barrier{
                .resource = resource.name,
                .src = prevState,
                .dst = state,
                .subresourceRange = traits.imageTraits.subresourceRange,
                .offset = traits.bufferTraits.offset,
                .size = traits.bufferTraits.size,
            });
        }
    }

    return true;
}

RenderGraphResult egn::buildRenderGraph(RenderGraphBuilder &&builder, void *data) {
    ZoneScoped;

    _impl::RenderGraphImpl renderGraph;

    if(!setupRenderGraph(std::move(builder), data, renderGraph)) {
        LOG_ERROR("Failed to set up render graph!");
        return RenderGraphResult(nullptr);
    }

    // TODO: better validation

    for(auto const &[_, pass] : renderGraph.passes) {
        if(pass.reads.empty()) {
            if(!processPass(pass.name, renderGraph, renderGraph.passes.size() * 4)) {
                LOG_ERROR("Failed to order render graph!");
                return RenderGraphResult(nullptr);
            }
        }
    }

    LOG_TRACE("Pass stack:");
    for(uint32_t i = 0; i < renderGraph.passStack.size(); ++i) {
        auto passName = renderGraph.passStack[i];
        auto const &pass = renderGraph.passes.at(passName);
        LOG_TRACE("{:>2}) {:>15}: {}", i, passName, printDependencies(pass));
    }

    calculateLifetimes(renderGraph);
    aliasResources(renderGraph);
    allocateResources(renderGraph);

    if(!buildBarriers(renderGraph)) {
        LOG_ERROR("Failed to build barriers!");
        return RenderGraphResult(nullptr);
    }

    RenderGraphResultImpl *res = new RenderGraphResultImpl{
        .resources = renderGraph.resources,
        .passStack = renderGraph.passStack,
    };

    for(auto &&[name, pass] : renderGraph.passes) {
        res->passes[name] = {
            .name = std::move(pass.name),
            .reads = std::move(pass.reads),
            .writes = std::move(pass.writes),
            .barriers = std::move(pass.barriers),
            .queue = std::move(pass.queue),
            .storage = std::move(pass.storage),
        };
    }

    RenderGraphResult result(res);

    for(auto &[_, pass] : res->passes) {
        pass.storage->postCompile(result, data);
    }

    return result;
}
