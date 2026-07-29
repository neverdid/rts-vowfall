#include "AshenPlayerController.h"

#include "AshenCameraPawn.h"
#include "AshenEntityActor.h"
#include "AshenResourceActor.h"
#include "AshenSimulationSubsystem.h"

#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"

namespace
{
bool IsUnitArchetype(const EAshenEntityArchetype Archetype)
{
    return Archetype == EAshenEntityArchetype::Worker || Archetype == EAshenEntityArchetype::Vanguard ||
           Archetype == EAshenEntityArchetype::Skirmisher;
}

int32 ControlGroupFromKey(const FKey& Key)
{
    if (Key == EKeys::Zero) return 0;
    if (Key == EKeys::One) return 1;
    if (Key == EKeys::Two) return 2;
    if (Key == EKeys::Three) return 3;
    if (Key == EKeys::Four) return 4;
    if (Key == EKeys::Five) return 5;
    if (Key == EKeys::Six) return 6;
    if (Key == EKeys::Seven) return 7;
    if (Key == EKeys::Eight) return 8;
    if (Key == EKeys::Nine) return 9;
    return -1;
}
}

AAshenPlayerController::AAshenPlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
    DefaultMouseCursor = EMouseCursor::Crosshairs;
}

void AAshenPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindAction(TEXT("Select"), IE_Pressed, this, &AAshenPlayerController::BeginSelection);
    InputComponent->BindAction(TEXT("Select"), IE_Released, this, &AAshenPlayerController::EndSelection);
    InputComponent->BindAction(TEXT("Command"), IE_Pressed, this, &AAshenPlayerController::CommandUnderCursor);
    InputComponent->BindAction(TEXT("TrainPrimary"), IE_Pressed, this, &AAshenPlayerController::TrainPrimary);
    InputComponent->BindAction(TEXT("TrainSecondary"), IE_Pressed, this, &AAshenPlayerController::TrainSecondary);
    InputComponent->BindAction(TEXT("Deploy"), IE_Pressed, this, &AAshenPlayerController::DeploySkirmish);
    InputComponent->BindAction(TEXT("Menu"), IE_Pressed, this, &AAshenPlayerController::ToggleFrontEnd);
}

bool AAshenPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (!bFrontEndVisible && Params.Key == EKeys::LeftMouseButton && Params.Event == IE_DoubleClick)
    {
        SelectSameTypeUnderCursor(IsQueueModifierDown());
        return true;
    }

    if (Params.Event == IE_Pressed)
    {
        if (bFrontEndVisible && (Params.Key == EKeys::Enter || Params.Key == EKeys::SpaceBar))
        {
            if (FrontEndPage == EAshenFrontEndPage::Campaign)
            {
                StartStoryPrologue();
            }
            else
            {
                OpenStoryCampaign();
            }
            return true;
        }
        if (bFrontEndVisible && Params.Key == EKeys::Escape &&
            FrontEndPage == EAshenFrontEndPage::Campaign)
        {
            FrontEndPage = EAshenFrontEndPage::Home;
            return true;
        }
        if (bFrontEndVisible && Params.Key == EKeys::LeftMouseButton)
        {
            HandleFrontEndClick();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::Escape)
        {
            if (PendingCommand != EAshenCommandMode::None)
            {
                PendingCommand = EAshenCommandMode::None;
                return true;
            }
            ToggleFrontEnd();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::LeftMouseButton && PendingCommand != EAshenCommandMode::None)
        {
            ExecutePendingCommand();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::LeftMouseButton && HandleCommandCardClick())
        {
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::A)
        {
            BeginAttackMove();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::P)
        {
            BeginPatrol();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::R)
        {
            BeginRallyPoint();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::S)
        {
            StopSelected();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::H)
        {
            HoldSelected();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::B)
        {
            BeginBuild(EAshenEntityArchetype::Barracks);
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::T)
        {
            BeginBuild(EAshenEntityArchetype::Turret);
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::Y)
        {
            ResearchTier();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::U)
        {
            ResearchFactionDoctrine();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::F)
        {
            ActivateFactionPower();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::X)
        {
            RetreatSelected();
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::Z)
        {
            SetSelectedStance(EAshenStance::Aggressive);
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::C)
        {
            SetSelectedStance(EAshenStance::Defensive);
            return true;
        }
        if (!bFrontEndVisible && Params.Key == EKeys::V)
        {
            SetSelectedStance(EAshenStance::Hold);
            return true;
        }

        const int32 GroupIndex = ControlGroupFromKey(Params.Key);
        if (!bFrontEndVisible && GroupIndex >= 0)
        {
            HandleControlGroup(GroupIndex);
            return true;
        }
    }
    return Super::InputKey(Params);
}

int32 AAshenPlayerController::GetSelectedCount() const
{
    int32 Count = 0;
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        Count += Actor.IsValid() ? 1 : 0;
    }
    return Count;
}

bool AAshenPlayerController::GetSelectionBox(FVector2D& OutMin, FVector2D& OutMax) const
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!bSelecting || !GetMousePosition(MouseX, MouseY))
    {
        return false;
    }

    const FVector2D Current(MouseX, MouseY);
    if (FVector2D::Distance(SelectionStart, Current) < 6.0f)
    {
        return false;
    }

    OutMin = {FMath::Min(SelectionStart.X, Current.X), FMath::Min(SelectionStart.Y, Current.Y)};
    OutMax = {FMath::Max(SelectionStart.X, Current.X), FMath::Max(SelectionStart.Y, Current.Y)};
    return true;
}

bool AAshenPlayerController::GetFrontEndButtonRect(const int32 Slot, FVector2D& OutMin,
                                                   FVector2D& OutMax) const
{
    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    GetViewportSize(ViewportWidth, ViewportHeight);
    if (ViewportWidth <= 0 || ViewportHeight <= 0)
    {
        return false;
    }

    const float ViewWidth = static_cast<float>(ViewportWidth);
    const float ViewHeight = static_cast<float>(ViewportHeight);
    const bool bCompact = ViewportHeight < 650;
    const float Margin = ViewportWidth < 760 ? 28.0f : 64.0f;

    if (FrontEndPage == EAshenFrontEndPage::Campaign)
    {
        if (Slot == 0)
        {
            const float Width = FMath::Min(390.0f, ViewWidth - Margin * 2.0f);
            const float X = ViewportWidth >= 980 ? FMath::Max(Margin, ViewWidth * 0.56f) : Margin;
            const float Y = FMath::Max(430.0f, ViewHeight - (bCompact ? 74.0f : 104.0f));
            OutMin = {X, Y};
            OutMax = {FMath::Min(X + Width, ViewWidth - Margin), Y + (bCompact ? 48.0f : 56.0f)};
            return true;
        }
        if (Slot == 1)
        {
            OutMin = {Margin, bCompact ? 18.0f : 28.0f};
            OutMax = {Margin + 44.0f, OutMin.Y + 40.0f};
            return true;
        }
        return false;
    }

    if (Slot < 0 || Slot > 2)
    {
        return false;
    }
    const float Width = FMath::Min(390.0f, ViewWidth - Margin * 2.0f);
    const float PrimaryY = bCompact ? 258.0f : FMath::Clamp(ViewHeight * 0.52f, 300.0f, ViewHeight - 222.0f);
    const float PrimaryHeight = bCompact ? 48.0f : 56.0f;
    const float SecondaryHeight = bCompact ? 39.0f : 44.0f;
    const float Gap = bCompact ? 8.0f : 10.0f;
    const float Y = Slot == 0 ? PrimaryY : PrimaryY + PrimaryHeight + Gap +
                                              static_cast<float>(Slot - 1) * (SecondaryHeight + Gap);
    OutMin = {Margin, Y};
    OutMax = {Margin + Width, Y + (Slot == 0 ? PrimaryHeight : SecondaryHeight)};
    return true;
}

FString AAshenPlayerController::GetCommandModeLabel() const
{
    switch (PendingCommand)
    {
    case EAshenCommandMode::AttackMove:
        return TEXT("ATTACK-MOVE  //  CHOOSE GROUND OR ENEMY");
    case EAshenCommandMode::Patrol:
        return TEXT("PATROL  //  CHOOSE DESTINATION");
    case EAshenCommandMode::RallyPoint:
        return TEXT("RALLY POINT  //  CHOOSE DESTINATION");
    case EAshenCommandMode::BuildBarracks:
        return TEXT("ASSEMBLY HALL  //  CHOOSE A CLEAR CONSTRUCTION SITE");
    case EAshenCommandMode::BuildTurret:
        return TEXT("SIGNAL BASTION  //  CHOOSE A CLEAR CONSTRUCTION SITE");
    case EAshenCommandMode::None:
        return {};
    }
    return {};
}

int32 AAshenPlayerController::GetPrimarySelectedEntityId() const
{
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid())
        {
            return Actor->GetEntityId();
        }
    }
    return 0;
}

bool AAshenPlayerController::GetCommandFeedback(FVector& OutLocation, bool& bOutHostile,
                                                float& OutStrength) const
{
    if (GetWorld() == nullptr)
    {
        return false;
    }
    constexpr double FeedbackDuration = 0.72;
    const double Age = GetWorld()->GetTimeSeconds() - LastCommandFeedbackTime;
    if (Age < 0.0 || Age > FeedbackDuration)
    {
        return false;
    }
    OutLocation = LastCommandLocation;
    bOutHostile = bLastCommandHostile;
    OutStrength = 1.0f - static_cast<float>(Age / FeedbackDuration);
    return true;
}

void AAshenPlayerController::StartSkirmish()
{
    DeploySkirmish();
}

void AAshenPlayerController::OpenStoryCampaign()
{
    if (bFrontEndVisible)
    {
        FrontEndPage = EAshenFrontEndPage::Campaign;
    }
}

void AAshenPlayerController::StartStoryPrologue()
{
    if (!bFrontEndVisible)
    {
        return;
    }
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        if (!Sim->IsStoryMatch() || Sim->IsMatchOver())
        {
            Sim->ConfigureStoryMission(ashen::core::StoryMissionId::BridgeOfNames);
        }
    }
    DeployConfiguredMatch();
}

bool AAshenPlayerController::GetCommandButtonRect(const int32 Slot, FVector2D& OutMin, FVector2D& OutMax) const
{
    if (Slot < 0 || Slot >= 9)
    {
        return false;
    }
    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    GetViewportSize(ViewportWidth, ViewportHeight);
    if (ViewportWidth <= 0 || ViewportHeight <= 0)
    {
        return false;
    }

    constexpr float Gap = 4.0f;
    constexpr float CellHeight = 40.0f;
    const float GridWidth = FMath::Clamp(static_cast<float>(ViewportWidth) *
                                         (ViewportWidth < 900 ? 0.39f : 0.30f), 246.0f, 342.0f);
    const float CellWidth = (GridWidth - Gap * 2.0f) / 3.0f;
    const float GridX = static_cast<float>(ViewportWidth) - GridWidth - 20.0f;
    const float GridY = static_cast<float>(ViewportHeight) - 148.0f;
    const int32 Column = Slot % 3;
    const int32 Row = Slot / 3;
    OutMin = {GridX + static_cast<float>(Column) * (CellWidth + Gap),
              GridY + static_cast<float>(Row) * (CellHeight + Gap)};
    OutMax = {OutMin.X + CellWidth, OutMin.Y + CellHeight};
    return true;
}

FString AAshenPlayerController::GetCommandButtonLabel(const int32 Slot) const
{
    const EAshenEntityArchetype Archetype = PrimarySelectedArchetype();
    if (Archetype == EAshenEntityArchetype::Worker)
    {
        const TCHAR* Labels[] = {TEXT("ASSEMBLY HALL"), TEXT("SIGNAL BASTION"), TEXT(""), TEXT("ADVANCE"),
                                 TEXT("STOP"), TEXT("HOLD"), TEXT("RETREAT"), TEXT("DEFENSIVE"),
                                 TEXT("OATH POWER")};
        return Slot >= 0 && Slot < 9 ? Labels[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Vanguard || Archetype == EAshenEntityArchetype::Skirmisher)
    {
        const TCHAR* Labels[] = {TEXT("ADVANCE"), TEXT("STOP"), TEXT("HOLD"), TEXT("PATROL"),
                                 TEXT("RETREAT"), TEXT("AGGRESSIVE"), TEXT("DEFENSIVE"),
                                 TEXT("STAND GROUND"), TEXT("OATH POWER")};
        return Slot >= 0 && Slot < 9 ? Labels[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Command)
    {
        const FAshenPlayerView PlayerView = Simulation() != nullptr ? Simulation()->GetPlayerView(0) : FAshenPlayerView{};
        const TCHAR* ResearchLabel = PlayerView.TechTier < 2 ? TEXT("BLACK-IRON AGE") : TEXT("FIELD CHARTERS");
        const TCHAR* Labels[] = {TEXT("TRAIN WORKER"), ResearchLabel, TEXT("RALLY"), TEXT(""), TEXT(""),
                                 TEXT(""), TEXT(""), TEXT(""), TEXT("OATH POWER")};
        return Slot >= 0 && Slot < 9 ? Labels[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Barracks)
    {
        const TCHAR* Labels[] = {TEXT("VANGUARD"), TEXT("SKIRMISHER"), TEXT("DRILLS"), TEXT("RALLY"),
                                 TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("OATH POWER")};
        return Slot >= 0 && Slot < 9 ? Labels[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Turret)
    {
        return Slot == 8 ? TEXT("OATH POWER") : TEXT("");
    }
    return Slot == 8 ? TEXT("OATH POWER") : TEXT("");
}

FString AAshenPlayerController::GetCommandButtonHotkey(const int32 Slot) const
{
    const EAshenEntityArchetype Archetype = PrimarySelectedArchetype();
    if (Archetype == EAshenEntityArchetype::Worker)
    {
        const TCHAR* Keys[] = {TEXT("B"), TEXT("T"), TEXT(""), TEXT("A"), TEXT("S"), TEXT("H"),
                               TEXT("X"), TEXT("C"), TEXT("F")};
        return Slot >= 0 && Slot < 9 ? Keys[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Vanguard || Archetype == EAshenEntityArchetype::Skirmisher)
    {
        const TCHAR* Keys[] = {TEXT("A"), TEXT("S"), TEXT("H"), TEXT("P"), TEXT("X"), TEXT("Z"),
                               TEXT("C"), TEXT("V"), TEXT("F")};
        return Slot >= 0 && Slot < 9 ? Keys[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Command)
    {
        const TCHAR* Keys[] = {TEXT("Q"), TEXT("Y/U"), TEXT("R"), TEXT(""), TEXT(""), TEXT(""),
                               TEXT(""), TEXT(""), TEXT("F")};
        return Slot >= 0 && Slot < 9 ? Keys[Slot] : TEXT("");
    }
    if (Archetype == EAshenEntityArchetype::Barracks)
    {
        const TCHAR* Keys[] = {TEXT("Q"), TEXT("E"), TEXT("U"), TEXT("R"), TEXT(""), TEXT(""),
                               TEXT(""), TEXT(""), TEXT("F")};
        return Slot >= 0 && Slot < 9 ? Keys[Slot] : TEXT("");
    }
    return Slot == 8 ? TEXT("F") : TEXT("");
}

bool AAshenPlayerController::IsCommandButtonEnabled(const int32 Slot) const
{
    if (GetCommandButtonLabel(Slot).IsEmpty())
    {
        return false;
    }
    UAshenSimulationSubsystem* Sim = Simulation();
    if (Sim == nullptr)
    {
        return false;
    }
    if (Slot == 8)
    {
        const FAshenPlayerView PlayerView = Sim->GetPlayerView(0);
        return PlayerView.PowerCooldownSeconds <= 0.0f && PlayerView.Ore >= 45;
    }
    const int32 BuildingId = SelectedBuildingId();
    if (PrimarySelectedArchetype() == EAshenEntityArchetype::Command && Slot == 1)
    {
        const EAshenResearch Research = ContextResearch();
        return Sim->GetResearchViews(BuildingId).ContainsByPredicate([Research](const FAshenResearchView& View)
        {
            return View.Research == Research && View.bAvailable;
        });
    }
    if (PrimarySelectedArchetype() == EAshenEntityArchetype::Barracks && Slot == 2)
    {
        return Sim->GetResearchViews(BuildingId).ContainsByPredicate([](const FAshenResearchView& View)
        {
            return View.Research == EAshenResearch::TemperedOaths && View.bAvailable;
        });
    }
    if (PrimarySelectedArchetype() == EAshenEntityArchetype::Barracks && Slot == 1)
    {
        return Sim->GetPlayerView(0).TechTier >= 2;
    }
    return true;
}

bool AAshenPlayerController::GetPlacementPreview(FVector& OutLocation, EAshenEntityArchetype& OutBuilding,
                                                  bool& bOutValid) const
{
    if (PendingCommand != EAshenCommandMode::BuildBarracks && PendingCommand != EAshenCommandMode::BuildTurret)
    {
        return false;
    }
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit))
    {
        return false;
    }
    OutLocation = Hit.ImpactPoint;
    OutBuilding = PendingCommand == EAshenCommandMode::BuildBarracks
                      ? EAshenEntityArchetype::Barracks
                      : EAshenEntityArchetype::Turret;
    bOutValid = Simulation() != nullptr && Simulation()->CanPlaceBuilding(OutBuilding, OutLocation);
    return true;
}

void AAshenPlayerController::BeginSelection()
{
    if (bFrontEndVisible)
    {
        HandleFrontEndClick();
        return;
    }
    if (PendingCommand != EAshenCommandMode::None)
    {
        ExecutePendingCommand();
        return;
    }

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    bSelecting = GetMousePosition(MouseX, MouseY);
    SelectionStart = {MouseX, MouseY};
}

void AAshenPlayerController::EndSelection()
{
    if (!bSelecting)
    {
        return;
    }

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    const bool bHasMousePosition = GetMousePosition(MouseX, MouseY);
    bSelecting = false;
    if (!bHasMousePosition)
    {
        return;
    }

    const FVector2D SelectionEnd(MouseX, MouseY);
    const bool bAddToSelection = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
    if (FVector2D::Distance(SelectionStart, SelectionEnd) < 8.0f)
    {
        SelectUnderCursor(bAddToSelection);
        return;
    }

    FBox2D SelectionBox(ForceInit);
    SelectionBox += SelectionStart;
    SelectionBox += SelectionEnd;
    TArray<AAshenEntityActor*> Candidates;
    bool bContainsUnit = false;
    for (TActorIterator<AAshenEntityActor> It(GetWorld()); It; ++It)
    {
        AAshenEntityActor* Entity = *It;
        FVector2D ScreenPosition;
        if (Entity->GetOwnerIndex() != 0 || !ProjectWorldLocationToScreen(Entity->GetActorLocation(), ScreenPosition) ||
            !SelectionBox.IsInside(ScreenPosition))
        {
            continue;
        }
        Candidates.Add(Entity);
        bContainsUnit = bContainsUnit || IsUnitArchetype(Entity->GetArchetype());
    }
    if (!bAddToSelection)
    {
        ClearSelection();
    }
    for (AAshenEntityActor* Entity : Candidates)
    {
        if (!bContainsUnit || IsUnitArchetype(Entity->GetArchetype()))
        {
            AddToSelection(Entity);
        }
    }
    ActiveControlGroup = -1;
}

void AAshenPlayerController::SelectUnderCursor(const bool bAddToSelection)
{
    FHitResult Hit;
    const bool bHit = GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit);
    if (!bAddToSelection)
    {
        ClearSelection();
    }

    AAshenEntityActor* Entity = bHit ? Cast<AAshenEntityActor>(Hit.GetActor()) : nullptr;
    if (Entity == nullptr || Entity->GetOwnerIndex() != 0)
    {
        return;
    }

    AddToSelection(Entity);
    ActiveControlGroup = -1;
}

void AAshenPlayerController::SelectSameTypeUnderCursor(const bool bAddToSelection)
{
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit))
    {
        return;
    }

    const AAshenEntityActor* Seed = Cast<AAshenEntityActor>(Hit.GetActor());
    if (Seed == nullptr || Seed->GetOwnerIndex() != 0)
    {
        return;
    }
    if (!bAddToSelection)
    {
        ClearSelection();
    }

    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    GetViewportSize(ViewportWidth, ViewportHeight);
    for (TActorIterator<AAshenEntityActor> It(GetWorld()); It; ++It)
    {
        AAshenEntityActor* Entity = *It;
        FVector2D ScreenPosition;
        if (Entity->GetOwnerIndex() == 0 && Entity->GetArchetype() == Seed->GetArchetype() &&
            ProjectWorldLocationToScreen(Entity->GetActorLocation(), ScreenPosition) && ScreenPosition.X >= 0.0f &&
            ScreenPosition.Y >= 0.0f && ScreenPosition.X <= static_cast<float>(ViewportWidth) &&
            ScreenPosition.Y <= static_cast<float>(ViewportHeight))
        {
            AddToSelection(Entity);
        }
    }
    ActiveControlGroup = -1;
}

void AAshenPlayerController::CommandUnderCursor()
{
    if (bFrontEndVisible)
    {
        return;
    }

    PruneSelection();
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit))
    {
        return;
    }

    UAshenSimulationSubsystem* Sim = Simulation();
    if (Sim == nullptr)
    {
        return;
    }

    PendingCommand = EAshenCommandMode::None;
    const bool bQueue = IsQueueModifierDown();
    const TArray<int32> UnitIds = SelectedUnitIds();
    if (const AAshenEntityActor* Target = Cast<AAshenEntityActor>(Hit.GetActor()))
    {
        if (Target->GetOwnerIndex() != 0 && !UnitIds.IsEmpty())
        {
            if (Sim->IssueAttack(UnitIds, Target->GetEntityId(), bQueue))
            {
                RegisterCommandFeedback(Target->GetActorLocation(), true);
            }
            return;
        }
    }
    if (const AAshenResourceActor* Resource = Cast<AAshenResourceActor>(Hit.GetActor()))
    {
        const TArray<int32> WorkerIds = SelectedWorkerIds();
        if (!WorkerIds.IsEmpty() && Sim->IssueGather(WorkerIds, Resource->GetResourceId(), bQueue))
        {
            RegisterCommandFeedback(Resource->GetActorLocation(), false);
        }
        return;
    }

    if (!UnitIds.IsEmpty())
    {
        if (Sim->IssueMove(UnitIds, Hit.ImpactPoint, bQueue))
        {
            RegisterCommandFeedback(Hit.ImpactPoint, false);
        }
        return;
    }
    const int32 BuildingId = SelectedBuildingId();
    if (BuildingId > 0 && Sim->IssueSetRallyPoint(BuildingId, Hit.ImpactPoint))
    {
        RegisterCommandFeedback(Hit.ImpactPoint, false);
    }
}

void AAshenPlayerController::ExecutePendingCommand()
{
    if (bFrontEndVisible || PendingCommand == EAshenCommandMode::None)
    {
        return;
    }

    PruneSelection();
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit))
    {
        return;
    }
    UAshenSimulationSubsystem* Sim = Simulation();
    if (Sim == nullptr)
    {
        return;
    }

    const EAshenCommandMode CommandMode = PendingCommand;
    const bool bQueue = IsQueueModifierDown();
    bool bIssued = false;
    bool bHostile = false;
    FVector FeedbackLocation = Hit.ImpactPoint;
    if (CommandMode == EAshenCommandMode::BuildBarracks || CommandMode == EAshenCommandMode::BuildTurret)
    {
        const TArray<int32> Workers = SelectedWorkerIds();
        if (Workers.Num() == 1)
        {
            const EAshenEntityArchetype Building = CommandMode == EAshenCommandMode::BuildBarracks
                                                       ? EAshenEntityArchetype::Barracks
                                                       : EAshenEntityArchetype::Turret;
            bIssued = Sim->IssueBuild(Workers[0], Building, Hit.ImpactPoint);
        }
    }
    else if (CommandMode == EAshenCommandMode::RallyPoint)
    {
        const int32 BuildingId = SelectedBuildingId();
        bIssued = BuildingId > 0 && Sim->IssueSetRallyPoint(BuildingId, Hit.ImpactPoint);
    }
    else
    {
        const TArray<int32> UnitIds = SelectedUnitIds();
        if (UnitIds.IsEmpty())
        {
            return;
        }

        if (CommandMode == EAshenCommandMode::AttackMove)
        {
            if (const AAshenEntityActor* Target = Cast<AAshenEntityActor>(Hit.GetActor());
                Target != nullptr && Target->GetOwnerIndex() != 0)
            {
                bIssued = Sim->IssueAttack(UnitIds, Target->GetEntityId(), bQueue);
                bHostile = true;
                FeedbackLocation = Target->GetActorLocation();
            }
            else
            {
                bIssued = Sim->IssueAttackMove(UnitIds, Hit.ImpactPoint, bQueue);
                bHostile = true;
            }
        }
        else if (CommandMode == EAshenCommandMode::Patrol)
        {
            bIssued = Sim->IssuePatrol(UnitIds, Hit.ImpactPoint, bQueue);
        }
    }

    if (bIssued)
    {
        PendingCommand = EAshenCommandMode::None;
        RegisterCommandFeedback(FeedbackLocation, bHostile);
    }
}

void AAshenPlayerController::BeginAttackMove()
{
    PruneSelection();
    PendingCommand = SelectedUnitIds().IsEmpty() ? EAshenCommandMode::None : EAshenCommandMode::AttackMove;
}

void AAshenPlayerController::BeginPatrol()
{
    PruneSelection();
    PendingCommand = SelectedUnitIds().IsEmpty() ? EAshenCommandMode::None : EAshenCommandMode::Patrol;
}

void AAshenPlayerController::BeginRallyPoint()
{
    PruneSelection();
    PendingCommand = SelectedBuildingId() <= 0 ? EAshenCommandMode::None : EAshenCommandMode::RallyPoint;
}

void AAshenPlayerController::BeginBuild(const EAshenEntityArchetype Building)
{
    PruneSelection();
    if (SelectedWorkerIds().Num() != 1)
    {
        PendingCommand = EAshenCommandMode::None;
        return;
    }
    PendingCommand = Building == EAshenEntityArchetype::Barracks
                         ? EAshenCommandMode::BuildBarracks
                         : EAshenCommandMode::BuildTurret;
}

void AAshenPlayerController::StopSelected()
{
    PendingCommand = EAshenCommandMode::None;
    PruneSelection();
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        const TArray<int32> UnitIds = SelectedUnitIds();
        if (!UnitIds.IsEmpty() && Sim->IssueStop(UnitIds))
        {
            FVector Center = FVector::ZeroVector;
            int32 Count = 0;
            for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
            {
                if (Actor.IsValid() && IsUnitArchetype(Actor->GetArchetype()))
                {
                    Center += Actor->GetActorLocation();
                    ++Count;
                }
            }
            RegisterCommandFeedback(Count > 0 ? Center / static_cast<float>(Count) : FVector::ZeroVector, false);
        }
    }
}

void AAshenPlayerController::HoldSelected()
{
    PendingCommand = EAshenCommandMode::None;
    PruneSelection();
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        const TArray<int32> UnitIds = SelectedUnitIds();
        if (!UnitIds.IsEmpty() && Sim->IssueHold(UnitIds, IsQueueModifierDown()))
        {
            FVector Center = FVector::ZeroVector;
            int32 Count = 0;
            for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
            {
                if (Actor.IsValid() && IsUnitArchetype(Actor->GetArchetype()))
                {
                    Center += Actor->GetActorLocation();
                    ++Count;
                }
            }
            RegisterCommandFeedback(Count > 0 ? Center / static_cast<float>(Count) : FVector::ZeroVector, false);
        }
    }
}

void AAshenPlayerController::RetreatSelected()
{
    PendingCommand = EAshenCommandMode::None;
    PruneSelection();
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        Sim->IssueRetreat(SelectedUnitIds());
    }
}

void AAshenPlayerController::SetSelectedStance(const EAshenStance Stance)
{
    PendingCommand = EAshenCommandMode::None;
    PruneSelection();
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        Sim->IssueSetStance(SelectedUnitIds(), Stance);
    }
}

void AAshenPlayerController::TrainPrimary()
{
    if (bFrontEndVisible)
    {
        return;
    }
    PruneSelection();
    const int32 BuildingId = SelectedBuildingId();
    if (BuildingId > 0 && Simulation() != nullptr)
    {
        Simulation()->IssueTrain(BuildingId, false);
    }
}

void AAshenPlayerController::TrainSecondary()
{
    if (bFrontEndVisible)
    {
        return;
    }
    PruneSelection();
    const int32 BuildingId = SelectedBuildingId();
    if (BuildingId > 0 && Simulation() != nullptr)
    {
        Simulation()->IssueTrain(BuildingId, true);
    }
}

void AAshenPlayerController::ResearchTier()
{
    if (!bFrontEndVisible && Simulation() != nullptr)
    {
        Simulation()->IssueResearch(SelectedBuildingId(), EAshenResearch::TierTwo);
    }
}

void AAshenPlayerController::ResearchFactionDoctrine()
{
    if (!bFrontEndVisible && Simulation() != nullptr)
    {
        Simulation()->IssueResearch(SelectedBuildingId(), ContextResearch());
    }
}

void AAshenPlayerController::ActivateFactionPower()
{
    if (!bFrontEndVisible && Simulation() != nullptr)
    {
        Simulation()->IssueActivatePower();
    }
}

void AAshenPlayerController::DeploySkirmish()
{
    if (!bFrontEndVisible)
    {
        return;
    }

    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        if (Sim->IsStoryMatch() || Sim->IsMatchOver())
        {
            Sim->ConfigureSkirmish();
        }
    }
    DeployConfiguredMatch();
}

void AAshenPlayerController::DeployConfiguredMatch()
{
    if (!bFrontEndVisible)
    {
        return;
    }

    bFrontEndVisible = false;
    FrontEndPage = EAshenFrontEndPage::Home;
    PendingCommand = EAshenCommandMode::None;
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        Sim->SetGameplayEnabled(true);
    }
}

void AAshenPlayerController::ToggleFrontEnd()
{
    if (bFrontEndVisible)
    {
        return;
    }

    ClearSelection();
    PendingCommand = EAshenCommandMode::None;
    FrontEndPage = EAshenFrontEndPage::Home;
    bFrontEndVisible = true;
    if (UAshenSimulationSubsystem* Sim = Simulation())
    {
        Sim->SetGameplayEnabled(false);
    }
}

void AAshenPlayerController::HandleFrontEndClick()
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!GetMousePosition(MouseX, MouseY))
    {
        return;
    }

    const FVector2D Mouse(MouseX, MouseY);
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        FVector2D ButtonMin;
        FVector2D ButtonMax;
        if (!GetFrontEndButtonRect(Slot, ButtonMin, ButtonMax) ||
            Mouse.X < ButtonMin.X || Mouse.X > ButtonMax.X ||
            Mouse.Y < ButtonMin.Y || Mouse.Y > ButtonMax.Y)
        {
            continue;
        }
        if (FrontEndPage == EAshenFrontEndPage::Campaign)
        {
            if (Slot == 0)
            {
                StartStoryPrologue();
            }
            else if (Slot == 1)
            {
                FrontEndPage = EAshenFrontEndPage::Home;
            }
        }
        else if (Slot == 0)
        {
            OpenStoryCampaign();
        }
        else if (Slot == 1)
        {
            DeploySkirmish();
        }
        return;
    }
}

bool AAshenPlayerController::HandleCommandCardClick()
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!GetMousePosition(MouseX, MouseY))
    {
        return false;
    }
    const FVector2D Mouse(MouseX, MouseY);
    for (int32 Slot = 0; Slot < 9; ++Slot)
    {
        FVector2D Min;
        FVector2D Max;
        if (GetCommandButtonRect(Slot, Min, Max) && Mouse.X >= Min.X && Mouse.X <= Max.X &&
            Mouse.Y >= Min.Y && Mouse.Y <= Max.Y)
        {
            if (IsCommandButtonEnabled(Slot))
            {
                ExecuteCommandSlot(Slot);
            }
            return true;
        }
    }
    return false;
}

void AAshenPlayerController::ExecuteCommandSlot(const int32 Slot)
{
    const EAshenEntityArchetype Archetype = PrimarySelectedArchetype();
    if (Slot == 8)
    {
        ActivateFactionPower();
        return;
    }
    if (Archetype == EAshenEntityArchetype::Worker)
    {
        if (Slot == 0) BeginBuild(EAshenEntityArchetype::Barracks);
        else if (Slot == 1) BeginBuild(EAshenEntityArchetype::Turret);
        else if (Slot == 3) BeginAttackMove();
        else if (Slot == 4) StopSelected();
        else if (Slot == 5) HoldSelected();
        else if (Slot == 6) RetreatSelected();
        else if (Slot == 7) SetSelectedStance(EAshenStance::Defensive);
        return;
    }
    if (Archetype == EAshenEntityArchetype::Vanguard || Archetype == EAshenEntityArchetype::Skirmisher)
    {
        if (Slot == 0) BeginAttackMove();
        else if (Slot == 1) StopSelected();
        else if (Slot == 2) HoldSelected();
        else if (Slot == 3) BeginPatrol();
        else if (Slot == 4) RetreatSelected();
        else if (Slot == 5) SetSelectedStance(EAshenStance::Aggressive);
        else if (Slot == 6) SetSelectedStance(EAshenStance::Defensive);
        else if (Slot == 7) SetSelectedStance(EAshenStance::Hold);
        return;
    }
    if (Archetype == EAshenEntityArchetype::Command)
    {
        if (Slot == 0) TrainPrimary();
        else if (Slot == 1)
        {
            Simulation()->GetPlayerView(0).TechTier < 2 ? ResearchTier() : ResearchFactionDoctrine();
        }
        else if (Slot == 2) BeginRallyPoint();
        return;
    }
    if (Archetype == EAshenEntityArchetype::Barracks)
    {
        if (Slot == 0) TrainPrimary();
        else if (Slot == 1) TrainSecondary();
        else if (Slot == 2) ResearchFactionDoctrine();
        else if (Slot == 3) BeginRallyPoint();
    }
}

void AAshenPlayerController::ClearSelection()
{
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid())
        {
            Actor->SetSelected(false);
        }
    }
    SelectedActors.Reset();
    ActiveControlGroup = -1;
}

void AAshenPlayerController::PruneSelection()
{
    SelectedActors.RemoveAll([](const TWeakObjectPtr<AAshenEntityActor>& Actor)
    {
        return !Actor.IsValid();
    });
}

void AAshenPlayerController::AddToSelection(AAshenEntityActor* Entity)
{
    if (Entity == nullptr)
    {
        return;
    }
    const bool bAlreadySelected = SelectedActors.ContainsByPredicate(
        [Entity](const TWeakObjectPtr<AAshenEntityActor>& Item)
        {
            return Item.Get() == Entity;
        });
    if (!bAlreadySelected)
    {
        SelectedActors.Add(Entity);
        Entity->SetSelected(true);
    }
}

void AAshenPlayerController::HandleControlGroup(const int32 GroupIndex)
{
    if (GroupIndex < 0 || GroupIndex >= 10)
    {
        return;
    }

    const bool bControlDown = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
    if (bControlDown)
    {
        PruneSelection();
        ControlGroups[GroupIndex] = SelectedActors;
        ActiveControlGroup = GroupIndex;
        LastControlGroup = GroupIndex;
        LastControlGroupTime = GetWorld() == nullptr ? 0.0 : GetWorld()->GetTimeSeconds();
        return;
    }

    const bool bAdd = IsQueueModifierDown();
    if (!bAdd)
    {
        ClearSelection();
    }
    ControlGroups[GroupIndex].RemoveAll([](const TWeakObjectPtr<AAshenEntityActor>& Actor)
    {
        return !Actor.IsValid();
    });
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : ControlGroups[GroupIndex])
    {
        if (Actor.IsValid())
        {
            AddToSelection(Actor.Get());
        }
    }
    ActiveControlGroup = GroupIndex;

    const double Now = GetWorld() == nullptr ? 0.0 : GetWorld()->GetTimeSeconds();
    if (LastControlGroup == GroupIndex && Now - LastControlGroupTime <= 0.36)
    {
        FocusSelection();
    }
    LastControlGroup = GroupIndex;
    LastControlGroupTime = Now;
}

void AAshenPlayerController::FocusSelection()
{
    FVector Center = FVector::ZeroVector;
    int32 Count = 0;
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid())
        {
            Center += Actor->GetActorLocation();
            ++Count;
        }
    }
    if (Count > 0)
    {
        if (AAshenCameraPawn* CameraPawn = Cast<AAshenCameraPawn>(GetPawn()))
        {
            CameraPawn->FocusOn(Center / static_cast<float>(Count));
        }
    }
}

void AAshenPlayerController::RegisterCommandFeedback(const FVector& Location, const bool bHostile)
{
    LastCommandLocation = Location;
    LastCommandLocation.Z = 8.0f;
    LastCommandFeedbackTime = GetWorld() == nullptr ? 0.0 : GetWorld()->GetTimeSeconds();
    bLastCommandHostile = bHostile;
}

bool AAshenPlayerController::IsQueueModifierDown() const
{
    return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

TArray<int32> AAshenPlayerController::SelectedUnitIds() const
{
    TArray<int32> Ids;
    Ids.Reserve(SelectedActors.Num());
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid() && IsUnitArchetype(Actor->GetArchetype()))
        {
            Ids.Add(Actor->GetEntityId());
        }
    }
    return Ids;
}

TArray<int32> AAshenPlayerController::SelectedWorkerIds() const
{
    TArray<int32> Ids;
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid() && Actor->GetArchetype() == EAshenEntityArchetype::Worker)
        {
            Ids.Add(Actor->GetEntityId());
        }
    }
    return Ids;
}

int32 AAshenPlayerController::SelectedBuildingId() const
{
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid() && !IsUnitArchetype(Actor->GetArchetype()))
        {
            return Actor->GetEntityId();
        }
    }
    return 0;
}

EAshenEntityArchetype AAshenPlayerController::PrimarySelectedArchetype() const
{
    for (const TWeakObjectPtr<AAshenEntityActor>& Actor : SelectedActors)
    {
        if (Actor.IsValid())
        {
            return Actor->GetArchetype();
        }
    }
    return static_cast<EAshenEntityArchetype>(255);
}

EAshenResearch AAshenPlayerController::ContextResearch() const
{
    return PrimarySelectedArchetype() == EAshenEntityArchetype::Command
               ? EAshenResearch::Wardcraft
               : EAshenResearch::TemperedOaths;
}

UAshenSimulationSubsystem* AAshenPlayerController::Simulation() const
{
    return GetWorld() == nullptr ? nullptr : GetWorld()->GetSubsystem<UAshenSimulationSubsystem>();
}
