#pragma once

#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <string>

namespace Pico
{
class PClass;
class PObject;
class PProperty;

class FInspectorApp
{
public:
    FInspectorApp();

    void Draw();

private:
    void DrawToolbar();
    void DrawClassPanel();
    void DrawObjectPanel();
    void DrawDetailsPanel();
    void DrawClassDetails(const PClass* Class);
    void DrawObjectDetails(PObject* Object);
    void DrawPropertyEditor(PObject* Object, const PProperty* Property);

    PObject* GetSelectedObject() const;
    void CreateSelectedClass();
    void DestroySelectedObject();
    void SaveSelectedObject();
    void LoadObjectFromDisk();
    void SetStatus(std::string Message, bool bIsError = false);

    const PClass* SelectedClass = nullptr;
    FObjectHandle SelectedObjectHandle;
    std::array<char, 128> NewObjectName {};
    std::array<char, 512> SavePath {};
    std::string Status;
    bool bStatusIsError = false;
};
}
