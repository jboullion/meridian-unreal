#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MRPlayerController.generated.h"

UCLASS()
class MERIDIANREMASTERED_API AMRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMRPlayerController();

protected:
	virtual void BeginPlay() override;
};
