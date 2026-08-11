#pragma once

#include <memory>

namespace Pico
{
class IPhysicsScene;

std::unique_ptr<IPhysicsScene> CreateJoltPhysicsScene();
const char* GetJoltPhysicsVersion();
}
