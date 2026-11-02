// TODO: merge this into the engine

#pragma once
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <string_view>
#include "Exception.hpp"
#include "engine/dsa/Algorithm.hpp"

namespace egn {

struct EcsException : EngineException {
    inline explicit EcsException(std::string_view msg): EngineException(msg) {
    }
};

}

#define ECS_ASSERT(x, msg) if(!static_cast<bool>(x)) { throw egn::EcsException(msg); }

#include "tracy/Tracy.hpp"
#define ECS_PROFILE ZoneScoped
#include "nicecs/ecs.hpp"

namespace egn {

template <typename... T> using exclude = ecs::exclude<T...>;

class Entity;

template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t> class RegistryView;

/// @brief ecs::registry wrapper with more oop syntax.
class Registry {
private:
    mutable std::shared_mutex mMutex;
    ecs::registry mReg;

    inline Registry(Registry &&o) {
        *this = std::move(o);
    }
    inline Registry &operator=(Registry &&o) {
        mReg = std::move(o.mReg);
        return *this;
    }
public:
    Registry() = default;
    inline explicit Registry(ecs::registry &&reg): mReg(std::move(reg)) { };
    inline Registry(Registry const &o) {
        *this = o;
    }
    inline Registry &operator=(Registry const &o) {
        mReg = o.mReg;
        return *this;
    }

    /// @brief Get an underlying ecs::registry
    inline ecs::registry &getReg() {
        return mReg;
    }
    /// @copydoc getReg
    inline ecs::registry const &getReg() const {
        return mReg;
    }

    /// @copydoc getReg
    inline ecs::registry const *operator->() const {
        return &getReg();
    }
    /// @copydoc getReg
    inline ecs::registry *operator->() {
        return &getReg();
    }

    /// @brief Get the shared mutex.
    inline std::shared_mutex &getMutex() const {
        return mMutex;
    }
    inline std::shared_lock<std::shared_mutex> lockShared() const {
        return std::shared_lock(getMutex());
    }
    inline std::unique_lock<std::shared_mutex> lockUnique() const {
        return std::unique_lock(getMutex());
    }

    /// @copydoc ecs::registry::create
    template <typename... Components_t> Entity create();

    /// @copydoc ecs::registry::create
    template <typename... Components_t> Entity create(Components_t &&...components);

    /// @copydoc ecs::registry::destroy
    void destroy(Entity const &entity);

    /// @copydoc ecs::registry::size
    std::size_t size() const;

    /// @copydoc ecs::registry::view
    template <typename... Include, typename... Exclude>
    RegistryView<sizeof...(Include), sizeof...(Exclude), Entity const> view(exclude<Exclude...> toExclude = exclude{ }) const;
    /// @copydoc ecs::registry::view
    template <typename... Include, typename... Exclude>
    RegistryView<sizeof...(Include), sizeof...(Exclude), Entity> view(exclude<Exclude...> toExclude = exclude{ });
};

template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t> class RegistryView : protected ecs::view<Include, Exclude> {
private:
    Registry *mParent;
public:
    /// @brief View iterator.
    class iterator {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = Entity_t;
    private:
        ecs::view<Include, Exclude>::iterator mIt;
        Registry *mParent = nullptr;
    public:
        explicit iterator(ecs::view<Include, Exclude>::iterator &&it, Registry *parent);
        iterator &operator++() noexcept;
        iterator operator++(int) noexcept;

        reference operator*() const noexcept;

        constexpr bool operator==(iterator const &other) const noexcept;
        constexpr bool operator!=(iterator const &other) const noexcept;

        bool operator==(ecs::view_sentinel const &s) const noexcept;
        bool operator!=(ecs::view_sentinel const &s) const noexcept;
    };

    constexpr explicit RegistryView(ecs::view<Include, Exclude> &&view, Registry *parent) noexcept;

    iterator begin() const noexcept;
    ecs::view_sentinel end() const noexcept;
    ecs::size_t estimate() const noexcept;
};

struct EntityWrapper;

struct DebugName {
    std::string name;
};

/// @brief A lightweight helper class to group the entity and the registry it belongs to with oop syntax.
/// WARNING: Invalidates if the registry is moved or if the entity invalidates obviously.
class Entity {
protected:
    Registry *mReg = nullptr;
    ecs::entity mEntity = 0;
    ecs::version_t mVersion = 0;
public:
    inline constexpr Entity() = default;
    inline constexpr ecs::entity id() const {
        return mEntity;
    }
    inline ecs::version_t version() const {
        return mVersion;
    }
    inline constexpr Registry const *pReg() const {
        return mReg;
    }
    inline constexpr Registry *pReg() {
        return mReg;
    }
    inline Registry const &reg() const {
        assert(pReg() && "Invalid registry!");
        return *pReg();
    }
    inline Registry &reg() {
        assert(pReg() && "Invalid registry!");
        return *pReg();
    }
    inline explicit constexpr Entity(Registry *reg, ecs::entity e);
    inline constexpr operator ecs::entity() const {
        return mEntity;
    }
    constexpr EntityWrapper wrap();
    constexpr operator EntityWrapper();

    inline constexpr bool operator==(Entity const &o) const {
        return mEntity == o.mEntity && pReg() == o.pReg();
    }
    inline constexpr bool operator<(Entity const &o) const {
        return pReg() < o.pReg() || mEntity < o.mEntity;
    }

    /// @copydoc ecs::registry::emplace
    template <typename component_t, class... Args> component_t &emplace(Args &&...args);

    template <typename component_t, class... Args> component_t &try_emplace(Args &&...args);

    /// @copydoc ecs::registry::contains
    template <typename component_t> bool contains() const;

    /// @copydoc ecs::registry::get
    template <typename component_t> component_t const &get() const;
    /// @copydoc ecs::registry::get
    template <typename component_t> component_t &get();

    /// @copydoc ecs::registry::remove
    template <typename component_t> void erase();

    /// @copydoc Registry::destroy
    void destroy();

    /// @copydoc ecs::registry::size
    std::size_t size() const;

    /// @copydoc ecs::registry empty(ecs::entity)
    bool empty() const;

    /// @copydoc ecs::registry::valid
    /// Also checks if the registry pointer is valid (not nullptr)
    /// Checks entity version.
    bool valid() const;

    /// @brief Get the entity's debug name.
    std::string_view name() const;
    /// @brief Set the entity's debug name.
    void name(std::string_view string);
};

/// Trivial entity wrapper
struct EntityWrapper {
    Registry *registry;
    ecs::entity entity;

    inline EntityWrapper &from(Entity &e) {
        registry = e.pReg();
        entity = e.id();
        return *this;
    }
    inline Entity get() const {
        return Entity(registry, entity);
    }
    inline constexpr operator Entity() const {
        return get();
    }
};

// https://stackoverflow.com/questions/2118541/check-if-parameter-pack-contains-a-type#comment121318358_54346836
template <typename What, typename... Args> constexpr inline bool is_present_v = (std::is_same_v<What, Args> || ...);

template <ecs::entity e> inline constexpr bool is_entity_null = false;
template <> inline constexpr bool is_entity_null<0> = true;

/// @brief Entity handle that requires specified components.
template <typename Op, typename... Components> class RestrictedEntity_t : public Entity {
private:
    class OpWrapper {
    private:
        bool mValue;
    public:
        inline OpWrapper(bool value): mValue(value) {
        }
        inline OpWrapper operator%(OpWrapper const &rhs) {
            return { Op{ }(mValue, rhs.mValue) };
        }
        inline bool get() const {
            return mValue;
        }
    };
public:
    using components_t = std::tuple<Components...>;
    template <size_t N> using Component = std::tuple_element<N, components_t>::type;

    inline constexpr bool valid() const {
        return this->Entity::valid() && (OpWrapper(this->contains<Components>()) % ...).get();
    }

    inline constexpr RestrictedEntity_t() = default;
    inline constexpr RestrictedEntity_t(Entity const &e) {
        *this = e;
    }
    inline constexpr RestrictedEntity_t(Entity &&e) {
        *this = std::move(e);
    }
    inline constexpr RestrictedEntity_t &operator=(Entity const &e) {
        static_assert(sizeof...(Components) > 0);
        this->Entity::operator=(e);
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }
    inline constexpr RestrictedEntity_t &operator=(Entity &&e) {
        this->Entity::operator=(std::move(e));
        ECS_ASSERT(valid(), "Invalid RestrictedEntity!");
        return *this;
    }

    inline void reset() {
        mEntity = 0;
    }

    template <size_t N = 0> inline decltype(auto) getc() const {
        return this->template get<Component<N>>();
    }
    template <size_t N = 0> inline decltype(auto) getc() {
        return this->template get<Component<N>>();
    }

    inline auto const *operator->() const {
        static_assert(sizeof...(Components) == 1);
        return &getc();
    };
    inline auto *operator->() {
        static_assert(sizeof...(Components) == 1);
        return &getc();
    };

    inline auto const &operator*() const {
        static_assert(sizeof...(Components) == 1);
        return &getc();
    };
    inline auto &operator*() {
        static_assert(sizeof...(Components) == 1);
        return getc();
    };
};

template <typename... Components> using RestrictedEntityAll = RestrictedEntity_t<std::logical_and<>, Components...>;

template <typename... Components> using RestrictedEntityAny = RestrictedEntity_t<std::logical_or<>, Components...>;

template <typename Component_t> using RestrictedEntity = RestrictedEntity_t<std::logical_or<>, Component_t>;

constexpr ecs::entity INVALID_ENTITY = 0;

template <typename T> class SparseSet : public ecs::sparse_set<T> {
public:
    inline SparseSet(std::size_t capacity = 10, std::uint32_t pageSize = 64): ecs::sparse_set<T>(capacity, pageSize) {
    }

    /// @brief Get a range of the dense data.
    inline std::ranges::subrange<T *> range() {
        return { this->pDense(), this->pDense() + this->size() };
    }
    /// @copydoc range
    inline std::ranges::subrange<T const *> range() const {
        return { this->dense().data(), this->dense().data() + this->size() };
    }

    /// @copydoc ecs::sparse_set::get
    inline T &at(size_t index) {
        return this->get(index);
    }
    /// @copydoc ecs::sparse_set::get
    inline T const &at(size_t index) const {
        return this->get(index);
    }
};

} // namespace egn

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "engine/io/Logging.hpp"
template <> class fmt::formatter<egn::Entity> {
public:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }
    template <typename Context> constexpr auto format(egn::Entity const &e, Context &ctx) const {
        return format_to(ctx.out(), "e{}{}", e.id(), e.valid() ? ("-\"" + std::string(e.name()) + "\"") : "-invalid");
    }
};
template <> class fmt::formatter<egn::EntityWrapper> {
public:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }
    template <typename Context> constexpr auto format(egn::EntityWrapper const &e, Context &ctx) const {
        return fmt::formatter<egn::Entity>{ }.format(e, ctx);
    }
};
namespace fmt {
template <typename Op, typename... Components> class formatter<egn::RestrictedEntity_t<Op, Components...>> {
public:
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }
    template <typename Context> constexpr auto format(egn::RestrictedEntity_t<Op, Components...> const &e, Context &ctx) const {
        return fmt::formatter<egn::Entity>{ }.format(e, ctx);
    }
};
};

template <typename Stream> inline Stream &operator<<(Stream &str, egn::Entity const &e) {
    str << fmt::format("{}", e);
    return str;
}
template <> struct std::hash<egn::Entity> {
    inline size_t operator()(egn::Entity const &e) const {
        return egn::hash_combine(std::hash<ecs::entity>{ }(e.id()), std::hash<std::uintptr_t>{ }(reinterpret_cast<std::uintptr_t>(e.pReg())));
    }
};

template <typename... Components_t> inline egn::Entity egn::Registry::create() {
    return Entity{ this, getReg().create<Components_t...>() };
}
template <typename... Components_t> inline egn::Entity egn::Registry::create(Components_t &&...components) {
    return Entity{ this, getReg().create(std::forward<Components_t>(components)...) };
}
inline void egn::Registry::destroy(Entity const &entity) {
    getReg().destroy(entity.id());
}
inline std::size_t egn::Registry::size() const {
    return getReg().size();
}
template <typename... Include, typename... Exclude>
inline egn::RegistryView<sizeof...(Include), sizeof...(Exclude), egn::Entity const> egn::Registry::view(exclude<Exclude...> toExclude) const {
    return egn::RegistryView<sizeof...(Include), sizeof...(Exclude), egn::Entity const>(getReg().view<Include...>(toExclude),
        const_cast<Registry *>(this));
}
template <typename... Include, typename... Exclude>
inline egn::RegistryView<sizeof...(Include), sizeof...(Exclude), egn::Entity> egn::Registry::view(exclude<Exclude...> toExclude) {
    return egn::RegistryView<sizeof...(Include), sizeof...(Exclude), egn::Entity>(getReg().view<Include...>(toExclude), this);
}

template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline egn::RegistryView<Include, Exclude, Entity_t>::iterator::iterator(ecs::view<Include, Exclude>::iterator &&it, Registry *parent):
    mIt(std::move(it)), mParent(parent) {
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline egn::RegistryView<Include, Exclude, Entity_t>::iterator &egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator++() noexcept {
    ++mIt;
    return *this;
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline egn::RegistryView<Include, Exclude, Entity_t>::iterator egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator++(int) noexcept {
    auto original = *this;
    this->operator++();
    return original;
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline egn::RegistryView<Include, Exclude, Entity_t>::iterator::reference
egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator*() const noexcept {
    return reference(mParent, *mIt);
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
constexpr inline bool egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator==(iterator const &other) const noexcept {
    return mIt == other.mIt && mParent == other.mParent;
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
constexpr inline bool egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator!=(iterator const &other) const noexcept {
    return !operator==(other);
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
bool egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator==(ecs::view_sentinel const &s) const noexcept {
    return mIt == s;
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
bool egn::RegistryView<Include, Exclude, Entity_t>::iterator::operator!=(ecs::view_sentinel const &s) const noexcept {
    return mIt != s;
}

template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
constexpr inline egn::RegistryView<Include, Exclude, Entity_t>::RegistryView(ecs::view<Include, Exclude> &&view, Registry *parent) noexcept:
    ecs::view<Include, Exclude>(std::move(view)), mParent(parent) {
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline egn::RegistryView<Include, Exclude, Entity_t>::iterator egn::RegistryView<Include, Exclude, Entity_t>::begin() const noexcept {
    return iterator(ecs::view<Include, Exclude>::begin(), mParent);
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline ecs::view_sentinel egn::RegistryView<Include, Exclude, Entity_t>::end() const noexcept {
    return { };
}
template <ecs::size_t Include, ecs::size_t Exclude, typename Entity_t>
inline ecs::size_t egn::RegistryView<Include, Exclude, Entity_t>::estimate() const noexcept {
    ecs::view<Include, Exclude>::size();
}

inline constexpr egn::EntityWrapper egn::Entity::wrap() {
    return EntityWrapper{ }.from(*this);
}
inline constexpr egn::Entity::operator EntityWrapper() {
    return wrap();
}
inline constexpr egn::Entity::Entity(Registry *reg, ecs::entity e): mReg(reg), mEntity(e) {
    mVersion = this->reg()->version(mEntity);
}
template <typename component_t, class... Args> inline component_t &egn::Entity::emplace(Args &&...args) {
    assert(pReg());
    return reg()->emplace<component_t, Args...>(id(), std::forward<Args>(args)...);
}
template <typename component_t, class... Args> inline component_t &egn::Entity::try_emplace(Args &&...args) {
    assert(pReg());
    if(!contains<component_t>())
        return emplace<component_t>(std::forward<Args>(args)...);
    else
        return get<component_t>();
}
template <typename component_t> inline bool egn::Entity::contains() const {
    assert(pReg());
    return reg()->contains<component_t>(id());
}
template <typename component_t> inline component_t const &egn::Entity::get() const {
    assert(pReg());
    return reg()->get<component_t>(id());
}
template <typename component_t> inline component_t &egn::Entity::get() {
    assert(pReg());
    return reg()->get<component_t>(id());
}
template <typename component_t> inline void egn::Entity::erase() {
    assert(pReg());
    return reg()->erase<component_t>(id());
}
inline void egn::Entity::destroy() {
    assert(pReg());
    reg().destroy(*this);
    mEntity = 0;
};
inline std::size_t egn::Entity::size() const {
    assert(pReg());
    return reg()->size(id());
}
inline bool egn::Entity::empty() const {
    return size() == 0;
}
inline bool egn::Entity::valid() const {
    return pReg() && reg()->valid(id()) && reg()->version(id()) == mVersion;
}
inline std::string_view egn::Entity::name() const {
    if(valid() && contains<DebugName>()) {
        return get<DebugName>().name;
    }
    return "";
}
inline void egn::Entity::name(std::string_view string) {
    if(valid())
        try_emplace<DebugName>().name = string;
}
