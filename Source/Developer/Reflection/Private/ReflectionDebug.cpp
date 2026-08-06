#include "Pico/Developer/ReflectionDebug.h"

#include "Pico/Object/Class.h"
#include "Pico/Object/Object.h"
#include "Pico/Object/Property.h"

#include <sstream>

namespace Pico
{
namespace
{
void GatherProperties(const PClass* Class, std::vector<const PProperty*>& OutProperties)
{
    if (Class == nullptr)
    {
        return;
    }

    GatherProperties(Class->GetSuperClass(), OutProperties);
    for (const PProperty& Property : Class->GetProperties())
    {
        OutProperties.push_back(&Property);
    }
}

void AppendPropertyValue(std::ostringstream& Stream, const PProperty& Property, const PObject& Object)
{
    switch (Property.GetType())
    {
    case EPropertyType::Int32:
    {
        int32 Value = 0;
        Stream << (Property.GetValue(&Object, Value) ? std::to_string(Value) : "<unavailable>");
        break;
    }
    case EPropertyType::Float:
    {
        float Value = 0.0f;
        if (Property.GetValue(&Object, Value))
        {
            Stream << Value;
        }
        else
        {
            Stream << "<unavailable>";
        }
        break;
    }
    case EPropertyType::Bool:
    {
        bool Value = false;
        Stream << (Property.GetValue(&Object, Value) ? (Value ? "true" : "false") : "<unavailable>");
        break;
    }
    case EPropertyType::Vector3:
    {
        FVector3 Value;
        if (Property.GetValue(&Object, Value))
        {
            Stream << '(' << Value.X << ", " << Value.Y << ", " << Value.Z << ')';
        }
        else
        {
            Stream << "<unavailable>";
        }
        break;
    }
    case EPropertyType::Rotator:
    {
        FRotator Value;
        if (Property.GetValue(&Object, Value))
        {
            Stream << "(Pitch=" << Value.Pitch
                   << ", Yaw=" << Value.Yaw
                   << ", Roll=" << Value.Roll << ')';
        }
        else
        {
            Stream << "<unavailable>";
        }
        break;
    }
    case EPropertyType::Transform:
    {
        FTransform Value;
        if (Property.GetValue(&Object, Value))
        {
            const FRotator Rotation = Value.Rotation.Rotator();
            Stream << "(Location=" << Value.Translation.X << ',' << Value.Translation.Y << ',' << Value.Translation.Z
                   << " Rotation=" << Rotation.Pitch << ',' << Rotation.Yaw << ',' << Rotation.Roll
                   << " Scale=" << Value.Scale.X << ',' << Value.Scale.Y << ',' << Value.Scale.Z << ')';
        }
        else
        {
            Stream << "<unavailable>";
        }
        break;
    }
    case EPropertyType::AssetPath:
    {
        FAssetPath Value;
        Stream << (Property.GetValue(&Object, Value)
            ? std::string(Value.ToString()) : "<unavailable>");
        break;
    }
    }
}
}

std::string_view GetPropertyTypeName(EPropertyType Type)
{
    switch (Type)
    {
    case EPropertyType::Int32:
        return "Int32";
    case EPropertyType::Float:
        return "Float";
    case EPropertyType::Bool:
        return "Bool";
    case EPropertyType::Vector3:
        return "Vector3";
    case EPropertyType::Rotator:
        return "Rotator";
    case EPropertyType::Transform:
        return "Transform";
    case EPropertyType::AssetPath:
        return "AssetPath";
    }

    return "Unknown";
}

std::vector<const PProperty*> GetAllProperties(const PClass* Class)
{
    std::vector<const PProperty*> Result;
    GatherProperties(Class, Result);
    return Result;
}

std::string DumpClass(const PClass* Class)
{
    if (Class == nullptr)
    {
        return "Class: <null>\n";
    }

    std::ostringstream Stream;
    Stream << "Class: " << Class->GetName().ToString() << '\n';
    Stream << "Super: "
           << (Class->GetSuperClass() != nullptr ? Class->GetSuperClass()->GetName().ToString() : "None")
           << '\n';
    Stream << "Size: " << Class->GetSize() << '\n';
    Stream << "Constructible: " << (Class->CanConstruct() ? "true" : "false") << '\n';
    Stream << "Properties:\n";

    for (const PProperty* Property : GetAllProperties(Class))
    {
        Stream << "  " << Property->GetName().ToString()
               << "  " << GetPropertyTypeName(Property->GetType())
               << "  Access=MemberPointer"
               << "  DeclaredBy=" << Property->GetOwnerClass()->GetName().ToString()
               << '\n';
    }
    return Stream.str();
}

std::string DumpObject(const PObject* Object)
{
    if (Object == nullptr)
    {
        return "Object: <null>\n";
    }

    std::ostringstream Stream;
    const FObjectHandle Handle = Object->GetHandle();
    Stream << "Object: " << Object->GetPathName() << '\n';
    Stream << "Class: " << Object->GetClass()->GetName().ToString() << '\n';
    Stream << "Outer: " << (Object->GetOuter() != nullptr ? Object->GetOuter()->GetPathName() : "None") << '\n';
    Stream << "Handle: {" << Handle.Index << ", " << Handle.Serial << "}\n";
    Stream << "Properties:\n";

    for (const PProperty* Property : GetAllProperties(Object->GetClass()))
    {
        Stream << "  " << Property->GetName().ToString() << " = ";
        AppendPropertyValue(Stream, *Property, *Object);
        Stream << '\n';
    }
    return Stream.str();
}
}
