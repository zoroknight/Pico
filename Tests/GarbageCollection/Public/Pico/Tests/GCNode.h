#pragma once

#include "Pico/Object/Object.h"
#include "Pico/Object/ObjectPtr.h"
#include "Pico/Object/ReflectionMacros.h"
#include "Pico/Tests/GCNode.generated.h"

#include <vector>

namespace PicoTest
{
PCLASS()
class PGCNode final : public Pico::PObject
{
    GENERATED_BODY()

public:
    void SetStrong(PGCNode* Object);
    void SetWeak(PGCNode* Object);
    void AddNativeStrong(PGCNode* Object);
    PGCNode* GetStrong() const;
    PGCNode* GetWeak() const;
    static int GetDestroyedInstanceCount();
    static void EnableReentrancyProbe();
    static bool WasGCActiveInBeginDestroy();
    static bool WasNestedCollectionRejected();

protected:
    explicit PGCNode(const Pico::FObjectConstructionParams& Params);
    void AddReferencedObjects(Pico::FReferenceCollector& Collector) const override;
    void BeginDestroy() override;

private:
    PPROPERTY(Transient, NotSerializable)
    Pico::TObjectPtr<PGCNode> Strong;
    PPROPERTY(Transient, NotSerializable)
    Pico::TWeakObjectPtr<PGCNode> Weak;
    std::vector<Pico::FObjectHandle> NativeStrongHandles;
    static int DestroyedInstanceCount;
    static bool bProbeReentrancy;
    static bool bSawGCInBeginDestroy;
    static bool bNestedCollectionRejected;
};
}
