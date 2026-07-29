#pragma once

#include "AshenTypes.h"
#include "GameFramework/PlayerController.h"
#include "AshenPlayerController.generated.h"

class AAshenEntityActor;
struct FInputKeyEventArgs;
class UAshenSimulationSubsystem;

enum class EAshenCommandMode : uint8
{
    None,
    AttackMove,
    Patrol,
    RallyPoint,
    BuildBarracks,
    BuildTurret,
};

enum class EAshenFrontEndPage : uint8
{
    Home,
    Campaign,
};

UCLASS()
class ASHENDOMINION_API AAshenPlayerController final : public APlayerController
{
    GENERATED_BODY()

public:
    AAshenPlayerController();
    virtual void SetupInputComponent() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void StartSkirmish();
    void OpenStoryCampaign();
    void StartStoryPrologue();

    UFUNCTION(BlueprintPure, Category = "Ashen")
    int32 GetSelectedCount() const;

    UFUNCTION(BlueprintPure, Category = "Ashen")
    bool IsFrontEndVisible() const noexcept { return bFrontEndVisible; }

    bool IsCampaignFrontEndVisible() const noexcept
    {
        return bFrontEndVisible && FrontEndPage == EAshenFrontEndPage::Campaign;
    }

    bool GetSelectionBox(FVector2D& OutMin, FVector2D& OutMax) const;
    bool GetFrontEndButtonRect(int32 Slot, FVector2D& OutMin, FVector2D& OutMax) const;
    FString GetCommandModeLabel() const;
    int32 GetPrimarySelectedEntityId() const;
    int32 GetActiveControlGroup() const noexcept { return ActiveControlGroup; }
    bool GetCommandFeedback(FVector& OutLocation, bool& bOutHostile, float& OutStrength) const;
    bool GetCommandButtonRect(int32 Slot, FVector2D& OutMin, FVector2D& OutMax) const;
    FString GetCommandButtonLabel(int32 Slot) const;
    FString GetCommandButtonHotkey(int32 Slot) const;
    bool IsCommandButtonEnabled(int32 Slot) const;
    bool GetPlacementPreview(FVector& OutLocation, EAshenEntityArchetype& OutBuilding, bool& bOutValid) const;

private:
    void BeginSelection();
    void EndSelection();
    void SelectUnderCursor(bool bAddToSelection);
    void SelectSameTypeUnderCursor(bool bAddToSelection);
    void CommandUnderCursor();
    void ExecutePendingCommand();
    void BeginAttackMove();
    void BeginPatrol();
    void BeginRallyPoint();
    void BeginBuild(EAshenEntityArchetype Building);
    void StopSelected();
    void HoldSelected();
    void RetreatSelected();
    void SetSelectedStance(EAshenStance Stance);
    void TrainPrimary();
    void TrainSecondary();
    void ResearchTier();
    void ResearchFactionDoctrine();
    void ActivateFactionPower();
    void DeploySkirmish();
    void DeployConfiguredMatch();
    void ToggleFrontEnd();
    void HandleFrontEndClick();
    bool HandleCommandCardClick();
    void ExecuteCommandSlot(int32 Slot);
    void ClearSelection();
    void PruneSelection();
    void AddToSelection(AAshenEntityActor* Entity);
    void HandleControlGroup(int32 GroupIndex);
    void FocusSelection();
    void RegisterCommandFeedback(const FVector& Location, bool bHostile);
    bool IsQueueModifierDown() const;
    TArray<int32> SelectedUnitIds() const;
    TArray<int32> SelectedWorkerIds() const;
    int32 SelectedBuildingId() const;
    EAshenEntityArchetype PrimarySelectedArchetype() const;
    EAshenResearch ContextResearch() const;
    UAshenSimulationSubsystem* Simulation() const;

    TArray<TWeakObjectPtr<AAshenEntityActor>> SelectedActors;
    TArray<TWeakObjectPtr<AAshenEntityActor>> ControlGroups[10];
    FVector2D SelectionStart = FVector2D::ZeroVector;
    FVector LastCommandLocation = FVector::ZeroVector;
    double LastCommandFeedbackTime = -10.0;
    double LastControlGroupTime = -10.0;
    int32 LastControlGroup = -1;
    int32 ActiveControlGroup = -1;
    EAshenCommandMode PendingCommand = EAshenCommandMode::None;
    EAshenFrontEndPage FrontEndPage = EAshenFrontEndPage::Home;
    bool bSelecting = false;
    bool bFrontEndVisible = true;
    bool bLastCommandHostile = false;
};
