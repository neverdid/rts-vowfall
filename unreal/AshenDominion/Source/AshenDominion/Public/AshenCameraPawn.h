#pragma once

#include "GameFramework/Pawn.h"
#include "AshenCameraPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

UCLASS()
class ASHENDOMINION_API AAshenCameraPawn final : public APawn
{
    GENERATED_BODY()

public:
    AAshenCameraPawn();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent *PlayerInputComponent) override;
    void FocusOn(const FVector &WorldPosition);
    void FrameRegion(const FVector &WorldPosition, float CameraDistance);
    void FrameWorld();

private:
    void SetForwardInput(float Value);
    void SetRightInput(float Value);
    void AddZoomInput(float Value);

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UCameraComponent> Camera;

    float ForwardInput = 0.0f;
    float RightInput = 0.0f;
    float DesiredArmLength = 2850.0f;
};
