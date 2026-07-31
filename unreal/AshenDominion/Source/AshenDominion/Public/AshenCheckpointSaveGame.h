#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "AshenCheckpointSaveGame.generated.h"

UCLASS()
class ASHENDOMINION_API UAshenCheckpointSaveGame final : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentAdapterVersion = 1;

    UPROPERTY(SaveGame)
    int32 AdapterVersion = CurrentAdapterVersion;

    UPROPERTY(SaveGame)
    uint32 SnapshotSchemaVersion = 0;

    UPROPERTY(SaveGame)
    uint64 ContentDigest = 0;

    UPROPERTY(SaveGame)
    uint64 PipelineDigest = 0;

    UPROPERTY(SaveGame)
    uint64 CheckpointTick = 0;

    UPROPERTY(SaveGame)
    uint64 CheckpointStateHash = 0;

    UPROPERTY(SaveGame)
    FDateTime SavedAtUtc;

    UPROPERTY(SaveGame)
    TArray<uint8> SnapshotBytes;
};
