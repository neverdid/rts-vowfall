#pragma once

#include "GameFramework/HUD.h"
#include "AshenHUD.generated.h"

class AAshenPlayerController;
class UAshenSimulationSubsystem;

UCLASS()
class ASHENDOMINION_API AAshenHUD final : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    void DrawFrontEnd(const AAshenPlayerController& Controller);
    void DrawCampaign(const AAshenPlayerController& Controller);
    void DrawBattleHud(const AAshenPlayerController& Controller, const UAshenSimulationSubsystem& Simulation);
    void DrawTacticalMap(const UAshenSimulationSubsystem& Simulation);
    void DrawPlacementPreview(const AAshenPlayerController& Controller);
    void DrawOrderRoute(const AAshenPlayerController& Controller, const UAshenSimulationSubsystem& Simulation);
    void DrawCommandFeedback(const AAshenPlayerController& Controller);
    void DrawSelectionMarquee(const AAshenPlayerController& Controller);
    void DrawMatchResult(const UAshenSimulationSubsystem& Simulation);
};
