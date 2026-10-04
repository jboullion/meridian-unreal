#include "Tests/MRLookDevTour.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "MeridianRemastered.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Zones/MRZoneSubsystem.h"

namespace
{
	/** Seconds after a camera cut before capturing (Lumen, virtual shadow maps and auto exposure settle). */
	constexpr float SettleSeconds = 3.f;
}

bool UMRLookDevTour::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("MRLookDev"));
}

FString UMRLookDevTour::FormatBookmark(const FString& Name, const FVector& Location, const FRotator& Rotation, float Fov)
{
	return FString::Printf(TEXT("{\"name\": \"%s\", \"location_cm\": [%.0f, %.0f, %.0f], \"rotation\": [%.1f, %.1f], \"fov\": %.0f}"),
		*Name, Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Fov);
}

bool UMRLookDevTour::LoadShots()
{
	const FString Path = FPaths::Combine(UMRZoneSubsystem::GetDataDir(), TEXT("environment"), TEXT("lookdev_cameras.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		UE_LOG(LogMeridian, Error, TEXT("MRLookDev: could not read %s"), *Path);
		return false;
	}

	FString OnlyList;
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevOnly="), OnlyList, /*bShouldStopOnSeparator*/ false);
	TArray<FString> Only;
	OnlyList.ParseIntoArray(Only, TEXT(","));

	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cameras")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& L = O->GetArrayField(TEXT("location_cm"));
		const TArray<TSharedPtr<FJsonValue>>& R = O->GetArrayField(TEXT("rotation"));
		if (L.Num() != 3 || R.Num() != 2)
		{
			UE_LOG(LogMeridian, Warning, TEXT("MRLookDev: skipping malformed camera %s"), *O->GetStringField(TEXT("name")));
			continue;
		}
		FShot Shot;
		Shot.Name = O->GetStringField(TEXT("name"));
		if (Only.Num() && !Only.Contains(Shot.Name))
		{
			continue;
		}
		Shot.Location = FVector(L[0]->AsNumber(), L[1]->AsNumber(), L[2]->AsNumber());
		Shot.Rotation = FRotator(R[0]->AsNumber(), R[1]->AsNumber(), 0.f);
		double Fov = 90.0;
		O->TryGetNumberField(TEXT("fov"), Fov);
		Shot.Fov = Fov;
		Shots.Add(Shot);
	}
	return Shots.Num() > 0;
}

void UMRLookDevTour::Start(APlayerController* InController)
{
	Controller = InController;
	FString Label = TEXT("latest");
	FParse::Value(FCommandLine::Get(), TEXT("MRLookDevLabel="), Label);
	OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("MRLookDev"), Label);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	if (!LoadShots())
	{
		InController->ConsoleCommand(TEXT("quit"));
		return;
	}
	Index = -1;
	// give zone streaming and the first Lumen pass time to settle
	InController->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRLookDevTour::Next), 6.f, false);
}

void UMRLookDevTour::Next()
{
	APlayerController* PC = Controller.Get();
	if (!PC)
	{
		return;
	}
	if (!Camera)
	{
		Camera = PC->GetWorld()->SpawnActor<ACameraActor>();
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorHiddenInGame(true);
	}
	if (++Index >= Shots.Num())
	{
		UE_LOG(LogMeridian, Display, TEXT("MRLookDev: done, %d shots in %s"), Shots.Num(), *OutDir);
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FShot& Shot = Shots[Index];
	Camera->SetActorLocationAndRotation(Shot.Location, Shot.Rotation);
	Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	PC->SetViewTarget(Camera);

	const FString File = FPaths::Combine(OutDir, Shot.Name + TEXT(".png"));
	PC->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, File]()
	{
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogMeridian, Display, TEXT("MRLookDev: %s"), *File);
		if (APlayerController* P = Controller.Get())
		{
			P->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UMRLookDevTour::Next), 0.5f, false);
		}
	}), SettleSeconds, false);
}
