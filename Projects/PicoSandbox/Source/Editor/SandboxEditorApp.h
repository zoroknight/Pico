#pragma once

#include "PicoSandbox/SandboxSession.h"

#include <string>
#include <vector>

namespace Pico
{
class PObject;
class PProperty;
}

namespace PicoSandbox
{
class FSandboxEditorApp
{
public:
    FSandboxEditorApp();

    void Draw();

private:
    void DrawToolbar();
    void DrawWorkflowPanel();
    void DrawObjectPanel();
    void DrawDetailsPanel();
    void DrawPropertyEditor(Pico::PObject* Object, const Pico::PProperty* Property);
    void DrawEventLog();

    void RunAction(const char* ActionName, bool (FSandboxSession::*Action)());
    void StartNewSession();
    void AppendEvent(std::string Event);

    FSandboxSession Session;
    std::vector<std::string> Events;
};
}
