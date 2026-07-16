// TODO: merge this into the engine

#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include "Exception.hpp"

struct EcsException : EngineException {
    inline explicit EcsException(std::string_view msg) : EngineException(msg) {}
};
#define ECS_ASSERT(x, msg) if(!static_cast<bool>(x)) { throw EcsException(msg); }

#include "nicecs/ecs.hpp"

class Entity;

template<typename... T>
using exclude = ecs::exclude<T...>;

/// @brief A thin layer above the ecs::registry with more oop syntax.
class Registry
{
private:
    mutable std::shared_mutex mMutex;
    ecs::registry mReg;
public:
    Registry() = default;
    inline explicit Registry(ecs::registry &&reg) : mReg(std::move(reg)) {};
    Registry(Registry const &);
    Registry(Registry &&);
    Registry &operator=(Registry const &);
    Registry &operator=(Registry &&);

    /// @brief Get an underlying ecs::registry
    inline ecs::registry &getReg() { return mReg; }
    /// @copydoc getReg
    inline ecs::registry const &getReg() const { return mReg; }

    /// @copydoc getReg
    inline ecs::registry const *operator->() const { return &getReg(); }
    /// @copydoc getReg
    inline ecs::registry *operator->() { return &getReg(); }

    /// @brief Get the shared mutex.
    inline std::shared_mutex &getMutex() const { return mMutex; }
    inline std::shared_lock<std::shared_mutex> lockShared() const { return std::shared_lock(mMutex); }
    inline std::unique_lock<std::shared_mutex> lockUnique() const { return std::unique_lock(mMutex); }

    /// @copydoc ecs::registry::create
    template <typename... Components_t> 
    Entity create();

    /// @copydoc ecs::registry::create
    template <typename... Components_t> 
    Entity create(Components_t&&... components);

    /// @copydoc ecs::registry::destroy
    void destroy(Entity const &entity);

    /// @copydoc ecs::registry::size
    std::size_t size() const;

    /// @copydoc ecs::registry::view
    template<typename... Include, typename... Exclude>
    std::vector<Entity> const view(exclude<Exclude...> toExclude = exclude{}) const;
    /// @copydoc ecs::registry::view
    template<typename... Include, typename... Exclude>
    std::vector<Entity> view(exclude<Exclude...> toExclude = exclude{});

    /// @copydoc ecs::registry::viewAny
    template<typename... May, typename... Exclude>
    std::vector<Entity> const viewAny(exclude<Exclude...> toExclude = exclude{}) const;
    /// @copydoc ecs::registry::viewAny
    template<typename... May, typename... Exclude>
    std::vector<Entity> viewAny(exclude<Exclude...> toExclude = exclude{});

    /// @copydoc ecs::registry::merged
    Registry merged(Registry const &other) const;
    /// @copydoc ecs::registry::merged
    void merge(Registry const &other);
    /// @copydoc ecs::registry::copy
    Entity copy(Entity const &entity);
};

/// @brief A lightweight helper class to group the entity and the registry it belongs to with oop syntax.
/// WARNING: Invalidates if the registry is moved or if the entity invalidates obviously.
class Entity
{
private:
    Registry *mReg = nullptr;
    ecs::entity mEntity = 0;
public:
    inline explicit constexpr Entity() = default;
    inline explicit constexpr Entity(Registry *reg, ecs::entity e) : mReg(reg), mEntity(e) {}
    inline ecs::entity id() const { return mEntity; }
    inline Registry const *pReg() const { return mReg; }
    inline Registry *pReg() { return mReg; }
    inline Registry const &reg() const 
    { 
        assert(pReg() && "Invalid registry!");
        return *pReg(); 
    }
    inline Registry &reg() 
    { 
        assert(pReg() && "Invalid registry!");
        return *pReg(); 
    }
    inline operator ecs::entity() const { return mEntity; }

    template <typename component_t, class... Args>
    inline void emplace(Args&&... args) { assert(pReg()); return reg()->emplace<component_t, Args...>(id(), std::forward<Args>(args)...); }

    /// @copydoc ecs::registry::has
    template <typename component_t> 
    inline bool has() const { assert(pReg()); return reg()->has<component_t>(id()); }

    /// @copydoc ecs::registry::get
    template <typename component_t> 
    inline component_t const &get() const { assert(pReg()); return reg()->get<component_t>(id()); }
    /// @copydoc ecs::registry::get
    template <typename component_t> 
    inline component_t &get() { assert(pReg()); return reg()->get<component_t>(id()); }

    /// @copydoc ecs::registry::remove
    template <typename component_t> 
    inline void remove() { assert(pReg()); return reg()->remove<component_t>(id()); }

    /// @copydoc Registry::destroy
    inline void destroy() { assert(pReg()); reg().destroy(*this); };

    /// @copydoc ecs::registry::size
    inline std::size_t size() const { assert(pReg()); return reg()->size(id()); }

    /// @copydoc ecs::registry::valid
    /// Also checks if the registry pointer is valid (not nullptr)
    inline bool valid() const { return pReg() && reg()->valid(id()); }

    inline bool operator==(Entity const &o) const { return mEntity == o.mEntity && pReg() == o.pReg(); }
    inline bool operator<(Entity const &o) const { return pReg() < o.pReg() || mEntity < o.mEntity; }
};

/// @brief Entity handle that requires specified components
template<typename Op = std::logical_and<bool>, typename... Components>
class RestrictedEntity : public Entity
{
private:
    class OpWrapper {
    private:
        bool mValue;
    public:
        inline OpWrapper(bool value) : mValue(value) {}
        inline OpWrapper operator%(OpWrapper const &rhs) {
            return {Op{}(mValue, rhs.mValue)};
        }
        inline bool get() const { return mValue; }
    };
public:
    inline bool valid() const { 
        if(!this->Entity::valid())
            return false;
        return (OpWrapper(this->has<Components>()) % ...).get();
    }
    
    inline RestrictedEntity() = default;
    inline RestrictedEntity(Entity const &e) { *this = e; }
    inline RestrictedEntity(Entity &&e) { *this = std::move(e); }
    inline RestrictedEntity &operator=(Entity const &e) { 
        this->Entity::operator=(e);
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }
    inline RestrictedEntity &operator=(Entity &&e) { 
        this->Entity::operator=(std::move(e));
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }
};

inline Registry::Registry(Registry const &o) { *this = o; }
inline Registry::Registry(Registry &&o) { *this = std::move(o); }
inline Registry &Registry::operator=(Registry const &o)
{
    mReg = o.mReg;
    return *this;
}
inline Registry &Registry::operator=(Registry &&o)
{
    mReg = std::move(o.mReg);
    return *this;
}
template <typename... Components_t> 
inline Entity Registry::create()
{
    return Entity{ this, getReg().create<Components_t...>() };
}
template <typename... Components_t> 
inline Entity Registry::create(Components_t&&... components)
{
    return Entity{ this, getReg().create(std::forward<Components_t>(components)...) };
}
inline void Registry::destroy(Entity const &entity)
{
    getReg().destroy(entity.id());
}
inline std::size_t Registry::size() const
{
    return getReg().size();
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> const Registry::view(exclude<Exclude...> toExclude) const
{
    std::vector<Entity> res;
    for(auto const &e : getReg().view<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e); // Should be fine because the entity is const

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::view(exclude<Exclude...> toExclude)
{
    std::vector<Entity> res;
    for(auto const &e : getReg().view<Include...>(toExclude))
        res.emplace_back(this, e);

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> const Registry::viewAny(exclude<Exclude...> toExclude) const
{
    std::vector<Entity> res;
    for(auto const &e : getReg().viewAny<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e);

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::viewAny(exclude<Exclude...> toExclude)
{
    std::vector<Entity> res;
    for(auto const &e : getReg().viewAny<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e);

    return res;
}
inline void Registry::merge(Registry const &other)
{
    for(auto e : other.view())
        copy(e);
}
inline Registry Registry::merged(Registry const &other) const
{
    Registry reg;
    reg.merge(*this);
    reg.merge(other);
    return reg;
}
inline Entity Registry::copy(Entity const &other)
{
    return Entity(this, getReg().copy(other.id(), other.reg().getReg()));
}

constexpr ecs::entity INVALID_ENTITY = 0;

namespace std {
    template<>
    struct hash<Entity> {
        size_t operator()(Entity const &e) const {
            size_t seed = std::hash<ecs::entity>{}(e.id());
            seed ^= std::hash<std::uintptr_t>{}(reinterpret_cast<std::uintptr_t>(e.pReg())) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

template <typename T>
class SparseSet : public ecs::sparse_set<T>
{
public:
    inline SparseSet(std::size_t capacity = 10, std::uint32_t pageSize = 10) : ecs::sparse_set<T>(capacity, pageSize) {}

    /// @brief Get a range of the dense data.
    inline std::ranges::subrange<T *> range() {
        return {this->pDense(), this->pDense() + this->size()};
    }
    /// @copydoc range
    inline std::ranges::subrange<T const *> range() const {
        return {this->dense().data(), this->dense().data() + this->size()};
    }

    /// @copydoc ecs::sparse_set::get
    inline T &at(size_t index) { return this->get(index); }
    /// @copydoc ecs::sparse_set::get
    inline T const &at(size_t index) const { return this->get(index); }
};
