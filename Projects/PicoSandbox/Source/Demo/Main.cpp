#include "Pico/Developer/ReflectionDebug.h"
#include "Pico/Object/Object.h"
#include "PicoSandbox/SandboxCharacter.h"
#include "PicoSandbox/SandboxSession.h"

#include <iostream>

int main()
{
    using namespace PicoSandbox;

    FSandboxSession Session;

    std::cout << "[1] Initialize Pico and register project classes\n";
    if (!Session.Initialize())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }

    std::cout << Pico::DumpClass(PSandboxCharacter::StaticClass()) << '\n';

    std::cout << "[2] Create SandboxHero\n";
    if (!Session.Create())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }
    std::cout << Pico::DumpObject(Session.GetObject()) << '\n';

    std::cout << "[3] Modify inherited and local properties through reflection\n";
    if (!Session.ApplyDemoChanges())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }
    std::cout << Pico::DumpObject(Session.GetObject()) << '\n';

    std::cout << "[4] Save " << Session.GetObjectPath().string() << '\n';
    if (!Session.Save())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }

    std::cout << "[5] Destroy the in-memory object\n";
    if (!Session.Destroy())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }

    std::cout << "[6] Load the .pobj file\n";
    if (!Session.Load())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }
    std::cout << Pico::DumpObject(Session.GetObject()) << '\n';

    std::cout << "[7] Verify restored values and PostLoad ordering\n";
    if (!Session.VerifyLoaded())
    {
        std::cerr << Session.GetLastMessage() << '\n';
        return 1;
    }

    std::cout << "Sandbox round trip succeeded\n";
    return 0;
}
