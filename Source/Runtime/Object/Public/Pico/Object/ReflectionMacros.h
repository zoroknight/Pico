#pragma once

#include "Pico/Object/Class.h"
#include "Pico/Object/ClassRegistry.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"

#include <type_traits>
#include <utility>
#include <vector>

#define PICO_DECLARE_CLASS(Type, SuperType) \
public: \
    using ThisClass = Type; \
    using Super = SuperType; \
    static const ::Pico::PClass* StaticClass(); \
    static bool RegisterClass(); \
private: \
    static ::Pico::FObjectPtr ConstructInstance(const ::Pico::FObjectConstructionParams& Params); \
    static bool RegisterProperties(::Pico::PClass& Class);

#define PICO_DEFINE_CLASS(Type) \
    ::Pico::FObjectPtr Type::ConstructInstance(const ::Pico::FObjectConstructionParams& Params) \
    { \
        return ::Pico::FObjectPtr(new Type(Params)); \
    } \
    const ::Pico::PClass* Type::StaticClass() \
    { \
        static_assert( \
            std::is_base_of_v<typename Type::Super, Type>, \
            "PICO_DECLARE_CLASS superclass must match the native C++ inheritance hierarchy"); \
        static ::Pico::PClass Class = ::Pico::PClass::Create<Type>( \
            ::Pico::FName(#Type), \
            Type::Super::StaticClass(), \
             sizeof(Type), \
             &Type::ConstructInstance); \
        static const bool bPropertiesRegistered = Type::RegisterProperties(Class); \
        (void)bPropertiesRegistered; \
        return &Class; \
    } \
    bool Type::RegisterClass() \
    { \
        const ::Pico::PClass* Class = Type::StaticClass(); \
        return ::Pico::FClassRegistry::RegisterClass(Class); \
    }

#define PICO_DEFINE_CLASS_NO_PROPERTIES(Type) \
    PICO_DEFINE_CLASS(Type) \
    bool Type::RegisterProperties(::Pico::PClass&) \
    { \
        return true; \
    }

#define PICO_ADD_PROPERTY(PropertyList, Member) \
    (PropertyList).push_back( \
        ::Pico::PProperty::Create<&ThisClass::Member>(::Pico::FName(#Member)))

#define PICO_ADD_PROPERTY_METADATA(PropertyList, Member, Metadata) \
    (PropertyList).push_back( \
        ::Pico::PProperty::Create<&ThisClass::Member>( \
            ::Pico::FName(#Member), \
            Metadata))

#define PICO_ADD_ASSET_PROPERTY(PropertyList, Member, AssetType) \
    (PropertyList).push_back( \
        ::Pico::PProperty::CreateAssetReference<&ThisClass::Member>( \
            ::Pico::FName(#Member), \
            ::Pico::EAssetReferenceType::AssetType))
