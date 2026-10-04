#include "Player/MRPlayerController.h"

AMRPlayerController::AMRPlayerController()
{
}

void AMRPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}
