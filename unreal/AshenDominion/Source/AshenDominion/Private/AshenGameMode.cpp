#include "AshenGameMode.h"

#include "AshenArena.h"
#include "AshenCameraPawn.h"
#include "AshenHUD.h"
#include "AshenPlayerController.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

AAshenGameMode::AAshenGameMode()
{
    DefaultPawnClass = AAshenCameraPawn::StaticClass();
    PlayerControllerClass = AAshenPlayerController::StaticClass();
    HUDClass = AAshenHUD::StaticClass();
}

void AAshenGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<AAshenArena>();

#if !UE_BUILD_SHIPPING
    const bool bCaptureFrontEnd = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureFrontEnd"));
    const bool bCaptureCampaign = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureCampaign"));
    const bool bCaptureStoryBattle = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureStoryBattle"));
    const bool bCaptureBattle = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureBattle"));
    const bool bCaptureBlackridge = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureBlackridge"));
    const bool bCaptureGravewood = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureGravewood"));
    const bool bCaptureWorld = FParse::Param(FCommandLine::Get(), TEXT("AshenCaptureWorld"));
    if (!bCaptureFrontEnd && !bCaptureCampaign && !bCaptureStoryBattle && !bCaptureBattle &&
        !bCaptureBlackridge && !bCaptureGravewood && !bCaptureWorld)
    {
        return;
    }

    if (bCaptureCampaign || bCaptureStoryBattle || bCaptureBattle || bCaptureBlackridge ||
        bCaptureGravewood || bCaptureWorld)
    {
        FTimerHandle StartHandle;
        GetWorldTimerManager().SetTimer(
            StartHandle,
            [this, bCaptureCampaign, bCaptureStoryBattle, bCaptureBlackridge, bCaptureGravewood,
             bCaptureWorld]()
            {
                if (AAshenPlayerController *Controller =
                        Cast<AAshenPlayerController>(GetWorld()->GetFirstPlayerController()))
                {
                    if (bCaptureCampaign)
                    {
                        Controller->OpenStoryCampaign();
                    }
                    else if (bCaptureStoryBattle)
                    {
                        Controller->StartStoryPrologue();
                    }
                    else
                    {
                        Controller->StartSkirmish();
                    }
                    if (bCaptureBlackridge || bCaptureGravewood || bCaptureWorld)
                    {
                        if (AHUD *HUD = Controller->GetHUD())
                        {
                            HUD->bShowHUD = false;
                        }
                        if (AAshenCameraPawn *Camera = Cast<AAshenCameraPawn>(Controller->GetPawn()))
                        {
                            if (bCaptureWorld)
                            {
                                Camera->FrameWorld();
                            }
                            else if (bCaptureGravewood)
                            {
                                Camera->FrameRegion({3'470.0f, 2'110.0f, 170.0f}, 1'850.0f);
                            }
                            else
                            {
                                Camera->FrameRegion({1'360.0f, 880.0f, 180.0f}, 2'250.0f);
                            }
                        }
                    }
                }
            },
            0.35f, false);
    }

    const float CaptureDelay =
        bCaptureStoryBattle || bCaptureBattle || bCaptureBlackridge || bCaptureGravewood || bCaptureWorld
            ? 8.0f
            : 2.5f;
    FTimerHandle CaptureHandle;
    GetWorldTimerManager().SetTimer(
        CaptureHandle,
        [bCaptureCampaign, bCaptureStoryBattle, bCaptureBattle, bCaptureBlackridge, bCaptureGravewood,
         bCaptureWorld]()
        {
            const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/Automation"));
            IFileManager::Get().MakeDirectory(*Directory, true);
            const TCHAR *ScreenshotName = TEXT("FrontEnd.png");
            if (bCaptureWorld)
            {
                ScreenshotName = TEXT("World.png");
            }
            else if (bCaptureCampaign)
            {
                ScreenshotName = TEXT("Campaign.png");
            }
            else if (bCaptureStoryBattle)
            {
                ScreenshotName = TEXT("StoryBattle.png");
            }
            else if (bCaptureBlackridge)
            {
                ScreenshotName = TEXT("Blackridge.png");
            }
            else if (bCaptureGravewood)
            {
                ScreenshotName = TEXT("Gravewood.png");
            }
            else if (bCaptureBattle)
            {
                ScreenshotName = TEXT("Battle.png");
            }
            const FString Filename = FPaths::Combine(Directory, ScreenshotName);
            FScreenshotRequest::RequestScreenshot(Filename, true, false);
        },
        CaptureDelay, false);

    FTimerHandle ExitHandle;
    GetWorldTimerManager().SetTimer(
        ExitHandle, []() { FPlatformMisc::RequestExit(false); }, CaptureDelay + 1.5f, false);
#endif
}
