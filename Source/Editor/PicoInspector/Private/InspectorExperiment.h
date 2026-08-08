#pragma once

namespace Pico
{
class IInspectorExperiment
{
public:
    virtual ~IInspectorExperiment() = default;

    virtual const char* GetName() const = 0;
    virtual bool SetUp() = 0;
    virtual void Draw() = 0;
    virtual void Reset() = 0;
    virtual void TearDown() = 0;
};
}
