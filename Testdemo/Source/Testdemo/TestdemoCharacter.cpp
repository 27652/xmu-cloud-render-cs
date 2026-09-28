// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestdemoCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

//////////////////////////////////////////////////////////////////////////
// ATestdemoCharacter

namespace
{
	bool IsSupportedAutoScenarioProfile(const FString& Profile)
	{
		return Profile.Equals(TEXT("standard"), ESearchCase::IgnoreCase) ||
			Profile.Equals(TEXT("patrol"), ESearchCase::IgnoreCase) ||
			Profile.Equals(TEXT("orbit"), ESearchCase::IgnoreCase);
	}
}

ATestdemoCharacter::ATestdemoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;
	bAutoScenarioEnabled = false;
	bAutoScenarioJumpHeld = false;
	AutoScenarioElapsedSeconds = 0.0f;
	AutoScenarioStartDelaySeconds = 5.0f;
	AutoScenarioDurationSeconds = 0.0f;
	AutoScenarioProfile = TEXT("standard");

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)
}

void ATestdemoCharacter::BeginPlay()
{
	Super::BeginPlay();
	ConfigureAutoScenarioFromCommandLine();
}

void ATestdemoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bAutoScenarioEnabled)
	{
		return;
	}

	AutoScenarioElapsedSeconds += DeltaSeconds;
	const float ScenarioSeconds = AutoScenarioElapsedSeconds - AutoScenarioStartDelaySeconds;
	if (ScenarioSeconds < 0.0f)
	{
		SetAutoScenarioJumpHeld(false);
		return;
	}

	if (AutoScenarioDurationSeconds > 0.0f && ScenarioSeconds > AutoScenarioDurationSeconds)
	{
		SetAutoScenarioJumpHeld(false);
		return;
	}

	ApplyAutoScenario(ScenarioSeconds, DeltaSeconds);
}

void ATestdemoCharacter::ConfigureAutoScenarioFromCommandLine()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FString ParsedProfile;
	const bool bHasProfile = FParse::Value(CommandLine, TEXT("AutoScenarioProfile="), ParsedProfile);
	const bool bHasAutoScenarioFlag = FParse::Param(CommandLine, TEXT("AutoScenario"));

	if (!bHasProfile && !bHasAutoScenarioFlag)
	{
		return;
	}

	if (!bHasProfile)
	{
		ParsedProfile = TEXT("standard");
	}
	ParsedProfile = ParsedProfile.TrimStartAndEnd();
	if (ParsedProfile.IsEmpty() ||
		ParsedProfile.Equals(TEXT("off"), ESearchCase::IgnoreCase) ||
		ParsedProfile.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		return;
	}

	AutoScenarioProfile = ParsedProfile.ToLower();
	FParse::Value(CommandLine, TEXT("AutoScenarioStartDelay="), AutoScenarioStartDelaySeconds);
	FParse::Value(CommandLine, TEXT("AutoScenarioDuration="), AutoScenarioDurationSeconds);
	AutoScenarioStartDelaySeconds = FMath::Max(0.0f, AutoScenarioStartDelaySeconds);
	AutoScenarioDurationSeconds = FMath::Max(0.0f, AutoScenarioDurationSeconds);

	if (!IsSupportedAutoScenarioProfile(AutoScenarioProfile))
	{
		UE_LOG(LogTemp, Warning, TEXT("Unknown AutoScenarioProfile '%s'; fixed input replay is disabled. Supported profiles: standard, patrol, orbit."), *ParsedProfile);
		return;
	}

	bAutoScenarioEnabled = true;
	AutoScenarioElapsedSeconds = 0.0f;
	UE_LOG(LogTemp, Display, TEXT("AutoScenario enabled: profile=%s startDelay=%.3fs duration=%.3fs"),
		*AutoScenarioProfile,
		AutoScenarioStartDelaySeconds,
		AutoScenarioDurationSeconds);
}

void ATestdemoCharacter::ApplyAutoScenario(float ScenarioSeconds, float DeltaSeconds)
{
	if (AutoScenarioProfile.Equals(TEXT("orbit"), ESearchCase::IgnoreCase))
	{
		const float CycleSeconds = FMath::Fmod(ScenarioSeconds, 8.0f);

		if (CycleSeconds < 3.0f)
		{
			AddAutoScenarioMovement(0.75f, 0.85f);
			AddAutoScenarioLook(0.32f, 0.0f, DeltaSeconds);
		}
		else if (CycleSeconds < 5.5f)
		{
			AddAutoScenarioMovement(0.55f, -0.75f);
			AddAutoScenarioLook(-0.28f, 0.08f, DeltaSeconds);
		}
		else
		{
			AddAutoScenarioMovement(-0.35f, 0.0f);
			AddAutoScenarioLook(0.18f, -0.08f, DeltaSeconds);
		}

		SetAutoScenarioJumpHeld(CycleSeconds >= 3.2f && CycleSeconds < 3.45f);
		return;
	}

	if (AutoScenarioProfile.Equals(TEXT("patrol"), ESearchCase::IgnoreCase))
	{
		const float CycleSeconds = FMath::Fmod(ScenarioSeconds, 12.0f);

		if (CycleSeconds < 2.0f)
		{
			AddAutoScenarioMovement(0.8f, 0.0f);
			AddAutoScenarioLook(0.12f, 0.0f, DeltaSeconds);
		}
		else if (CycleSeconds < 3.5f)
		{
			AddAutoScenarioMovement(0.0f, 0.75f);
			AddAutoScenarioLook(0.35f, 0.0f, DeltaSeconds);
		}
		else if (CycleSeconds < 5.5f)
		{
			AddAutoScenarioMovement(0.7f, 0.0f);
			AddAutoScenarioLook(-0.18f, 0.05f, DeltaSeconds);
		}
		else if (CycleSeconds < 7.0f)
		{
			AddAutoScenarioMovement(0.0f, -0.75f);
			AddAutoScenarioLook(-0.35f, 0.0f, DeltaSeconds);
		}
		else if (CycleSeconds < 9.0f)
		{
			AddAutoScenarioMovement(-0.45f, 0.0f);
			AddAutoScenarioLook(0.16f, -0.05f, DeltaSeconds);
		}
		else
		{
			AddAutoScenarioMovement(0.35f, -0.25f);
			AddAutoScenarioLook(0.08f, 0.0f, DeltaSeconds);
		}

		SetAutoScenarioJumpHeld(CycleSeconds >= 8.2f && CycleSeconds < 8.45f);
		return;
	}

	const float CycleSeconds = FMath::Fmod(ScenarioSeconds, 10.0f);

	if (CycleSeconds < 2.0f)
	{
		AddAutoScenarioMovement(1.0f, 0.0f);
		AddAutoScenarioLook(0.15f, 0.0f, DeltaSeconds);
	}
	else if (CycleSeconds < 4.0f)
	{
		AddAutoScenarioMovement(1.0f, 0.0f);
		AddAutoScenarioLook(0.45f, 0.0f, DeltaSeconds);
	}
	else if (CycleSeconds < 5.5f)
	{
		AddAutoScenarioMovement(0.0f, 1.0f);
		AddAutoScenarioLook(-0.35f, 0.0f, DeltaSeconds);
	}
	else if (CycleSeconds < 7.0f)
	{
		AddAutoScenarioMovement(-0.45f, 0.0f);
		AddAutoScenarioLook(0.0f, 0.20f, DeltaSeconds);
	}
	else if (CycleSeconds < 8.5f)
	{
		AddAutoScenarioMovement(0.0f, -1.0f);
		AddAutoScenarioLook(0.25f, 0.0f, DeltaSeconds);
	}
	else
	{
		AddAutoScenarioMovement(0.35f, 0.0f);
		AddAutoScenarioLook(0.0f, -0.15f, DeltaSeconds);
	}

	SetAutoScenarioJumpHeld(CycleSeconds >= 4.2f && CycleSeconds < 4.45f);
}

void ATestdemoCharacter::AddAutoScenarioMovement(float ForwardValue, float RightValue)
{
	if (ForwardValue == 0.0f && RightValue == 0.0f)
	{
		return;
	}

	const FRotator Rotation = Controller != nullptr ? Controller->GetControlRotation() : GetActorRotation();
	const FRotator YawRotation(0.0f, Rotation.Yaw, 0.0f);
	if (ForwardValue != 0.0f)
	{
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, ForwardValue, true);
	}
	if (RightValue != 0.0f)
	{
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Direction, RightValue, true);
	}
}

void ATestdemoCharacter::AddAutoScenarioLook(float YawScale, float PitchScale, float DeltaSeconds)
{
	const float YawInput = YawScale * BaseTurnRate * DeltaSeconds;
	if (YawInput != 0.0f)
	{
		if (Controller != nullptr)
		{
			AddControllerYawInput(YawInput);
		}
		else
		{
			AddActorWorldRotation(FRotator(0.0f, YawInput, 0.0f));
		}
	}

	const float PitchInput = PitchScale * BaseLookUpRate * DeltaSeconds;
	if (PitchInput != 0.0f && Controller != nullptr)
	{
		AddControllerPitchInput(PitchInput);
	}
}

void ATestdemoCharacter::SetAutoScenarioJumpHeld(bool bShouldJump)
{
	if (bShouldJump && !bAutoScenarioJumpHeld)
	{
		Jump();
		bAutoScenarioJumpHeld = true;
	}
	else if (!bShouldJump && bAutoScenarioJumpHeld)
	{
		StopJumping();
		bAutoScenarioJumpHeld = false;
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void ATestdemoCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &ATestdemoCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &ATestdemoCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &ATestdemoCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &ATestdemoCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &ATestdemoCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &ATestdemoCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ATestdemoCharacter::OnResetVR);
}


void ATestdemoCharacter::OnResetVR()
{
	// If Testdemo is added to a project via 'Add Feature' in the Unreal Editor the dependency on HeadMountedDisplay in Testdemo.Build.cs is not automatically propagated
	// and a linker error will result.
	// You will need to either:
	//		Add "HeadMountedDisplay" to [YourProject].Build.cs PublicDependencyModuleNames in order to build successfully (appropriate if supporting VR).
	// or:
	//		Comment or delete the call to ResetOrientationAndPosition below (appropriate if not supporting VR)
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void ATestdemoCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void ATestdemoCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void ATestdemoCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void ATestdemoCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void ATestdemoCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void ATestdemoCharacter::MoveRight(float Value)
{
	if ( (Controller != nullptr) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}
