// TODO: merge this into the engine

#pragma once
#include "nicecs/ecs.hpp"
#include <cstdint>
#include <mutex>
#include <shared_mutex>

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
    std::vector<Entity> view(exclude<Exclude...> toExclude = exclude{}) const;

    /// @copydoc ecs::registry::viewAny
    template<typename... May, typename... Exclude>
    std::vector<Entity> viewAny(exclude<Exclude...> toExclude = exclude{}) const;

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

    template <typename component_t, class... Args>
    inline void emplace(Args&&... args) { return reg()->emplace<component_t, Args...>(id(), std::forward<Args>(args)...); }

    /// @copydoc ecs::registry::has
    template <typename component_t> 
    inline bool has() const { return reg()->has<component_t>(id()); }

    /// @copydoc ecs::registry::get
    template <typename component_t> 
    inline component_t const &get() const { return reg()->get<component_t>(id()); }
    /// @copydoc ecs::registry::get
    template <typename component_t> 
    inline component_t &get() { return reg()->get<component_t>(id()); }

    /// @copydoc ecs::registry::remove
    template <typename component_t> 
    inline void remove() { return reg()->remove<component_t>(id()); }

    /// @copydoc Registry::destroy
    inline void destroy() { reg().destroy(*this); };

    /// @copydoc ecs::registry::size
    inline std::size_t size() const { return reg()->size(id()); }

    /// @copydoc ecs::registry::valid
    /// Also checks if the registry is valid (not nullptr)
    inline bool valid() const { return pReg() && reg()->valid(id()); }

    inline bool operator==(Entity const &o) const { return mEntity == o.mEntity && pReg() == o.pReg(); }
    inline bool operator<(Entity const &o) const { return pReg() < o.pReg() || mEntity < o.mEntity; }
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
/// @copydoc ecs::registry::create
template <typename... Components_t> 
inline Entity Registry::create(Components_t&&... components)
{
    return Entity{ this, getReg().create(std::forward<Components_t>(components)...) };
}
/// @copydoc ecs::registry::destroy
inline void Registry::destroy(Entity const &entity)
{
    getReg().destroy(entity.id());
}
/// @copydoc ecs::registry::size
inline std::size_t Registry::size() const
{
    return getReg().size();
}
/// @copydoc ecs::registry::view
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::view(exclude<Exclude...> toExclude) const
{
    std::vector<Entity> res;
    for(auto const &e : getReg().view<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e); // Whats the worst that could happen (clueless)

    return res;
}
/// @copydoc ecs::registry::viewAny
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::viewAny(exclude<Exclude...> toExclude) const
{
    std::vector<Entity> res;
    for(auto const &e : getReg().viewAny<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e); // Please dont kill me for my sins

    return res;
}
/// @copydoc ecs::registry::merged
inline void Registry::merge(Registry const &other)
{
    for(auto e : other.view())
        copy(e);
}
/// @copydoc ecs::registry::merged
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
using SparseSet = ecs::sparse_set<T>;
