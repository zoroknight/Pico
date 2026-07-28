#include "Pico/Engine/Actor.h"

#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"

namespace Pico
{
PICO_DEFINE_CLASS_NO_PROPERTIES(PActor)

PActor::PActor(const FObjectConstructionParams& Params)
    : PObject(Params)
{
}

PWorld* PActor::GetWorld() const
{
    PLevel* Level = GetLevel();
    return Level != nullptr ? Level->GetWorld() : nullptr;
}

PLevel* PActor::GetLevel() const
{
    PObject* Outer = GetOuter();
    return Outer != nullptr && Outer->IsA(PLevel::StaticClass())
        ? static_cast<PLevel*>(Outer)
        : nullptr;
}

bool PActor::HasBegunPlay() const
{
    return bHasBegunPlay;
}

bool PActor::IsPendingDestroy() const
{
    return bPendingDestroy;
}

bool PActor::Destroy()
{
    PWorld* World = GetWorld();
    return World != nullptr && World->DestroyActor(this);
}

void PActor::BeginPlay()
{
}

void PActor::Tick(float)
{
}

void PActor::EndPlay()
{
}

void PActor::BeginDestroy()
{
    DispatchEndPlay();
    PObject::BeginDestroy();
}

void PActor::DispatchBeginPlay()
{
    if (!bHasBegunPlay && !bPendingDestroy)
    {
        bHasBegunPlay = true;
        BeginPlay();
    }
}

void PActor::DispatchTick(float DeltaSeconds)
{
    if (bHasBegunPlay && !bPendingDestroy)
    {
        Tick(DeltaSeconds);
    }
}

void PActor::DispatchEndPlay()
{
    if (bHasBegunPlay && !bHasEndedPlay)
    {
        bHasEndedPlay = true;
        EndPlay();
    }
}

void PActor::MarkPendingDestroy()
{
    bPendingDestroy = true;
}
}
