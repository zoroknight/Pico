#include "Pico/Engine/LocalPlayer.h"

namespace Pico
{
PICO_DEFINE_CLASS(PLocalPlayer)

bool PLocalPlayer::RegisterProperties(PClass& Class)
{
    FPropertyMetadata IndexMetadata;
    IndexMetadata.Flags = EPropertyFlags::Transient | EPropertyFlags::ReadOnly;
    std::vector<PProperty> Properties;
    PICO_ADD_PROPERTY_METADATA(Properties, LocalPlayerIndex, IndexMetadata);
    return Class.AddProperties(std::move(Properties));
}

PLocalPlayer::PLocalPlayer(const FObjectConstructionParams& Params)
    : PPlayer(Params)
{
}

int32 PLocalPlayer::GetLocalPlayerIndex() const { return LocalPlayerIndex; }
void PLocalPlayer::SetLocalPlayerIndex(int32 InIndex) { LocalPlayerIndex = InIndex; }

void PLocalPlayer::BeginDestroy()
{
    PPlayer::BeginDestroy();
}
}
