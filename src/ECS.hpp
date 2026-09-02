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
class Registry {
private:
    mutable std::shared_mutex mMutex;
    ecs::registry mReg;

    inline Registry(Registry &&o) { *this = std::move(o); }
    inline Registry &operator=(Registry &&o) {
        mReg = std::move(o.mReg);
        return *this;
    }
public:
    Registry() = default;
    inline explicit Registry(ecs::registry &&reg) : mReg(std::move(reg)) {};
    inline Registry(Registry const &o) { *this = o; }
    inline Registry &operator=(Registry const &o) {
        mReg = o.mReg;
        return *this;
    }

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
    inline std::shared_lock<std::shared_mutex> lockShared() const { return std::shared_lock(getMutex()); }
    inline std::unique_lock<std::shared_mutex> lockUnique() const { return std::unique_lock(getMutex()); }

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
class Entity {
private:
    Registry *mReg = nullptr;
    ecs::entity mEntity = 0;
public:
    inline constexpr Entity() = default;
    inline explicit constexpr Entity(Registry *reg, ecs::entity e) : mReg(reg), mEntity(e) {}
    inline constexpr ecs::entity id() const { return mEntity; }
    inline constexpr Registry const *pReg() const { return mReg; }
    inline constexpr Registry *pReg() { return mReg; }
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
    inline constexpr operator ecs::entity() const { return mEntity; }
    
    inline constexpr bool operator==(Entity const &o) const { 
        return mEntity == o.mEntity && pReg() == o.pReg(); 
    }
    inline constexpr bool operator<(Entity const &o) const { 
        return pReg() < o.pReg() || mEntity < o.mEntity; 
    }

    /// @copydoc ecs::registry::emplace
    template <typename component_t, class... Args>
    component_t &emplace(Args&&... args);

    template <typename component_t, class... Args>
    component_t &tryEmplace(Args&&... args);

    /// @copydoc ecs::registry::contains
    template <typename component_t> 
    bool contains() const;

    /// @copydoc ecs::registry::get
    template <typename component_t> 
    component_t const &get() const;
    /// @copydoc ecs::registry::get
    template <typename component_t> 
    component_t &get();

    /// @copydoc ecs::registry::remove
    template <typename component_t> 
    void erase();

    /// @copydoc Registry::destroy
    void destroy();

    /// @copydoc ecs::registry::size
    std::size_t size() const;

    /// @copydoc ecs::registry::valid
    /// Also checks if the registry pointer is valid (not nullptr)
    bool valid() const;
};

// https://stackoverflow.com/questions/2118541/check-if-parameter-pack-contains-a-type#comment121318358_54346836
template<typename What, typename... Args> constexpr inline bool is_present_v = (std::is_same_v<What, Args> || ...);

template<ecs::entity e>
inline constexpr bool is_entity_null = false;
template<>
inline constexpr bool is_entity_null<0> = true;

/// @brief Entity handle that requires specified components
/// @brief xxxSafe() member functions fail on compilation if the component is not present.
template<typename Op, typename... Components>
class RestrictedEntity_t : public Entity {
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
    inline constexpr bool valid() const { 
        return this->Entity::valid() && (OpWrapper(this->contains<Components>()) % ...).get();
    }
    
    inline constexpr RestrictedEntity_t() = default;
    inline constexpr RestrictedEntity_t(Entity const &e) { *this = e; }
    inline constexpr RestrictedEntity_t(Entity &&e) { *this = std::move(e); }
    inline constexpr RestrictedEntity_t &operator=(Entity const &e) { 
        this->Entity::operator=(e);
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }
    inline constexpr RestrictedEntity_t &operator=(Entity &&e) { 
        this->Entity::operator=(std::move(e));
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }
};

template<typename Component>
using RestrictedEntity = RestrictedEntity_t<std::logical_or<>, Component>;

template<typename... Components>
using RestrictedEntityAll = RestrictedEntity_t<std::logical_and<>, Components...>;

template<typename... Components>
using RestrictedEntityAny = RestrictedEntity_t<std::logical_or<>, Components...>;

template<typename Component>
class DirectEntity : public RestrictedEntity<Component> {
public:
    using RestrictedEntity<Component>::RestrictedEntity;

    inline Component const &getc() const { return this->template get<Component>(); }
    inline Component &getc() { return this->template get<Component>(); }

    inline Component const *operator->() const { return &getc(); };
    inline Component *operator->() { return &getc(); };

    inline Component const &operator*() const {return &getc(); };
    inline Component &operator*() { return getc(); };
};

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
class SparseSet : public ecs::sparse_set<T> {
public:
    inline SparseSet(std::size_t capacity = 10, std::uint32_t pageSize = 64) : ecs::sparse_set<T>(capacity, pageSize) {}

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

struct DebugName {
    std::string name;
};

#include "Logging.hpp"

template <> class fmt::formatter<Entity> {
public:
    constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
    template <typename Context>
    constexpr auto format(Entity const &e, Context &ctx) const {
        return format_to(ctx.out(), "e{}{}{}", 
            e.id(), 
            e.valid() && e.contains<DebugName>() ? ("-\"" + e.get<DebugName>().name + "\"") : "", 
            e.valid() ? "" : "-invalid"
        );
    }
};


extern Registry sReg;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename... Components_t> 
inline Entity Registry::create() {
    return Entity{ this, getReg().create<Components_t...>() };
}
template <typename... Components_t> 
inline Entity Registry::create(Components_t&&... components) {
    return Entity{ this, getReg().create(std::forward<Components_t>(components)...) };
}
inline void Registry::destroy(Entity const &entity) {
    getReg().destroy(entity.id());
}
inline std::size_t Registry::size() const {
    return getReg().size();
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> const Registry::view(exclude<Exclude...> toExclude) const {
    std::vector<Entity> res;
    res.reserve(size() / 20u);
    for(auto const &e : getReg().view<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e); // Should be fine because the entity is const

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::view(exclude<Exclude...> toExclude) {
    std::vector<Entity> res;
    res.reserve(size() / 20u);
    for(auto const &e : getReg().view<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e);

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> const Registry::viewAny(exclude<Exclude...> toExclude) const {
    std::vector<Entity> res;
    res.reserve(size() / 20u);
    for(auto const &e : getReg().viewAny<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e);

    return res;
}
template<typename... Include, typename... Exclude>
inline std::vector<Entity> Registry::viewAny(exclude<Exclude...> toExclude) {
    std::vector<Entity> res;
    res.reserve(size() / 20u);
    for(auto const &e : getReg().viewAny<Include...>(toExclude))
        res.emplace_back(const_cast<Registry *>(this), e);

    return res;
}
inline void Registry::merge(Registry const &other) {
    for(auto e : other.view())
        copy(e);
}
inline Registry Registry::merged(Registry const &other) const {
    Registry reg;
    reg.merge(*this);
    reg.merge(other);
    return reg;
}
inline Entity Registry::copy(Entity const &other) {
    return Entity(this, getReg().copy(other.id(), other.reg().getReg()));
}

template <typename component_t, class... Args>
inline component_t &Entity::emplace(Args&&... args) { 
    assert(pReg()); 
    reg()->emplace<component_t, Args...>(id(), std::forward<Args>(args)...); 
    return get<component_t>(); 
}
template <typename component_t, class... Args>
inline component_t &Entity::tryEmplace(Args&&... args) { 
    assert(pReg()); 
    if(!contains<component_t>())
        return emplace<component_t>(std::forward<Args>(args)...);
    else 
        return get<component_t>();
}
template <typename component_t> 
inline bool Entity::contains() const { 
    assert(pReg()); 
    return reg()->contains<component_t>(id()); 
}
template <typename component_t> 
inline component_t const &Entity::get() const { 
    assert(pReg()); 
    return reg()->get<component_t>(id()); 
}
template <typename component_t> 
inline component_t &Entity::get() { 
    assert(pReg()); 
    return reg()->get<component_t>(id()); 
}
template <typename component_t> 
inline void Entity::erase() { 
    assert(pReg()); 
    return reg()->erase<component_t>(id()); 
}
inline void Entity::destroy() { 
    assert(pReg()); 
    reg().destroy(*this); 
    mEntity = 0; 
};
inline std::size_t Entity::size() const { 
    assert(pReg()); 
    return reg()->size(id()); 
}
inline bool Entity::valid() const { 
    return pReg() && reg()->valid(id()); 
}
