#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PClass;
class PObject;
class PProperty;
enum class EPropertyType : unsigned char;

std::string_view GetPropertyTypeName(EPropertyType Type);
std::vector<const PProperty*> GetAllProperties(const PClass* Class);
std::string DumpClass(const PClass* Class);
std::string DumpObject(const PObject* Object);
}
