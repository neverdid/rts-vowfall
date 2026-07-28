#include "AshenCameraPawn.h"

#include "AshenWorldLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

AAshenCameraPawn::AAshenCameraPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(SceneRoot);
    CameraArm->SetUsingAbsoluteRotation(true);
    CameraArm->SetRelativeRotation({-58.0f, -42.0f, 0.0f});
    CameraArm->TargetArmLength = 2'850.0f;
    CameraArm->bDoCollisionTest = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    Camera->FieldOfView = 50.0f;

    SetActorLocation({1'100.0f, Ashen::WorldLayout::CenterY, 0.0f});
}

void AAshenCameraPawn::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    float EdgeForward = 0.0f;
    float EdgeRight = 0.0f;
    if (const APlayerController *PlayerController = Cast<APlayerController>(GetController()))
    {
        int32 ViewportWidth = 0;
        int32 ViewportHeight = 0;
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
        if (ViewportWidth > 0 && ViewportHeight > 0 && PlayerController->GetMousePosition(MouseX, MouseY))
        {
            constexpr float EdgeSize = 18.0f;
            EdgeRight = MouseX <= EdgeSize ? -1.0f : (MouseX >= ViewportWidth - EdgeSize ? 1.0f : 0.0f);
            EdgeForward = MouseY <= EdgeSize ? 1.0f : (MouseY >= ViewportHeight - EdgeSize ? -1.0f : 0.0f);
        }
    }

    const FRotator YawRotation(0.0f, CameraArm->GetComponentRotation().Yaw, 0.0f);
    const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
    const float MoveForward = FMath::Clamp(ForwardInput + EdgeForward, -1.0f, 1.0f);
    const float MoveRight = FMath::Clamp(RightInput + EdgeRight, -1.0f, 1.0f);
    const float MoveSpeed = FMath::GetMappedRangeValueClamped(FVector2D(750.0f, 3'600.0f), FVector2D(650.0f, 1'450.0f),
                                                              CameraArm->TargetArmLength);
    const FVector Delta = (Forward * MoveForward + Right * MoveRight) * MoveSpeed * DeltaSeconds;
    AddActorWorldOffset(Delta, false);

    FVector Location = GetActorLocation();
    Location.X = FMath::Clamp(Location.X, Ashen::WorldLayout::CameraMarginX,
                              Ashen::WorldLayout::Width - Ashen::WorldLayout::CameraMarginX);
    Location.Y = FMath::Clamp(Location.Y, Ashen::WorldLayout::CameraMarginY,
                              Ashen::WorldLayout::Height - Ashen::WorldLayout::CameraMarginY);
    Location.Z = 0.0f;
    SetActorLocation(Location);

    CameraArm->TargetArmLength = FMath::FInterpTo(CameraArm->TargetArmLength, DesiredArmLength, DeltaSeconds, 10.0f);
}

void AAshenCameraPawn::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("CameraForward"), this, &AAshenCameraPawn::SetForwardInput);
    PlayerInputComponent->BindAxis(TEXT("CameraRight"), this, &AAshenCameraPawn::SetRightInput);
    PlayerInputComponent->BindAxis(TEXT("CameraZoom"), this, &AAshenCameraPawn::AddZoomInput);
}

void AAshenCameraPawn::FocusOn(const FVector &WorldPosition)
{
    FVector Location = GetActorLocation();
    Location.X = FMath::Clamp(WorldPosition.X, Ashen::WorldLayout::CameraMarginX,
                              Ashen::WorldLayout::Width - Ashen::WorldLayout::CameraMarginX);
    Location.Y = FMath::Clamp(WorldPosition.Y, Ashen::WorldLayout::CameraMarginY,
                              Ashen::WorldLayout::Height - Ashen::WorldLayout::CameraMarginY);
    Location.Z = 0.0f;
    SetActorLocation(Location);
}

void AAshenCameraPawn::FrameRegion(const FVector &WorldPosition, const float CameraDistance)
{
    SetActorTickEnabled(false);
    FocusOn(WorldPosition);
    FVector Location = GetActorLocation();
    Location.Z = WorldPosition.Z;
    SetActorLocation(Location);
    Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
    DesiredArmLength = FMath::Clamp(CameraDistance, 750.0f, 4'700.0f);
    CameraArm->TargetArmLength = DesiredArmLength;
}

void AAshenCameraPawn::FrameWorld()
{
    SetActorTickEnabled(false);
    SetActorLocation({Ashen::WorldLayout::CenterX, Ashen::WorldLayout::CenterY, 6'500.0f});
    CameraArm->SetRelativeRotation({-90.0f, -90.0f, 0.0f});
    Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
    Camera->SetOrthoWidth(3'550.0f);
    DesiredArmLength = 0.0f;
    CameraArm->TargetArmLength = DesiredArmLength;
}

void AAshenCameraPawn::SetForwardInput(const float Value)
{
    ForwardInput = Value;
}

void AAshenCameraPawn::SetRightInput(const float Value)
{
    RightInput = Value;
}

void AAshenCameraPawn::AddZoomInput(const float Value)
{
    DesiredArmLength = FMath::Clamp(DesiredArmLength - Value * 280.0f, 750.0f, 4'700.0f);
}
