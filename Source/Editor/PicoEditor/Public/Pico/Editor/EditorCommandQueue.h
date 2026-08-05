#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace Pico
{
class FEditorCommandQueue
{
public:
    using FCommand = std::function<void()>;

    void Enqueue(FCommand Command)
    {
        if (Command)
        {
            Commands.push_back(std::move(Command));
        }
    }

    void Flush()
    {
        std::vector<FCommand> Pending;
        Pending.swap(Commands);
        for (FCommand& Command : Pending)
        {
            Command();
        }
    }

    void Clear()
    {
        Commands.clear();
    }

private:
    std::vector<FCommand> Commands;
};
}
