#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Pico
{
class PClass;
class PObject;
class PProperty;
class PFunction;
enum class EPropertyType : unsigned char;
enum class EFunctionValueType : unsigned char;

std::string_view GetPropertyTypeName(EPropertyType Type);
std::string_view GetFunctionValueTypeName(EFunctionValueType Type);
std::vector<const PProperty*> GetAllProperties(const PClass* Class);
std::vector<const PFunction*> GetAllFunctions(const PClass* Class);
std::string DumpClass(const PClass* Class);
std::string DumpObject(const PObject* Object);
}
