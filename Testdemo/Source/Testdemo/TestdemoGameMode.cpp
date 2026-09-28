// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestdemoGameMode.h"
#include "TestdemoCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATestdemoGameMode::ATestdemoGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPersonCPP/Blueprints/ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
