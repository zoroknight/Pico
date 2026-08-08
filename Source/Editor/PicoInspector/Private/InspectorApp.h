#pragma once

#include "Pico/Object/Function.h"
#include "Pico/Object/ObjectTypes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Pico
{
class PClass;
class PObject;
class PProperty;
class IInspectorExperiment;

class FInspectorApp
{
public:
    FInspectorApp();
    ~FInspectorApp();

    void Draw();

private:
    void DrawToolbar();
    void DrawRuntimeBrowser();
    void DrawExperiments();
    void DrawClassPanel();
    void DrawObjectPanel();
    void DrawDetailsPanel();
    void DrawClassDetails(const PClass* Class);
    void DrawObjectDetails(PObject* Object);
    void DrawPropertyEditor(PObject* Object, const PProperty* Property);
    void DrawFunctionPanel(PObject* Object);
    void DrawFunctionEditor(PObject* Object);
    void DrawFunctionParameter(std::size_t Index);
    void ResetFunctionEditor(const PFunction* Function = nullptr);
    void InvokeSelectedFunction(PObject* Object);

    PObject* GetSelectedObject() const;
    void CreateSelectedClass();
    void DestroySelectedObject();
    void SaveSelectedObject();
    void LoadObjectFromDisk();
    void SetStatus(std::string Message, bool bIsError = false);

    struct FFunctionParameterInput
    {
        FFunctionValue Value;
        FObjectHandle ObjectHandle;
        std::array<char, 256> Text {};
    };

    const PClass* SelectedClass = nullptr;
    FObjectHandle SelectedObjectHandle;
    const PFunction* SelectedFunction = nullptr;
    std::vector<FFunctionParameterInput> FunctionInputs;
    FFunctionValue FunctionReturnValue;
    EFunctionInvokeResult FunctionInvokeResult = EFunctionInvokeResult::InvalidFunction;
    bool bHasFunctionInvokeResult = false;
    bool bProvideFunctionReturnStorage = true;
    std::vector<std::unique_ptr<IInspectorExperiment>> Experiments;
    std::size_t SelectedExperimentIndex = 0;
    std::array<char, 128> NewObjectName {};
    std::array<char, 512> SavePath {};
    std::string Status;
    bool bStatusIsError = false;
};
}
