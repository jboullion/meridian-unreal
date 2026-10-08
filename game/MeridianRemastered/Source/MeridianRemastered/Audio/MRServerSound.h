#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "MRServerSound.generated.h"

/** A decoded sound: 16-bit PCM, interleaved. Shared by every play of it. */
struct FMRPcmSound
{
	TArray<int16> Samples;
	int32 SampleRate = 22050;
	int32 Channels = 1;

	float DurationSeconds() const { return Channels > 0 && SampleRate > 0 ? float(Samples.Num()) / float(Channels * SampleRate) : 0.f; }
};

/**
 * A sound from the server's files (docs/adr/0012 M7): one we haven't imported, downloaded through the
 * asset cache and decoded at runtime (MRServerSound::Decode). One wave per play: it hands out the
 * shared samples from its own place, from the start again when it loops.
 */
UCLASS()
class MERIDIANREMASTERED_API UMRServerSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** Play this sound; bLoop: over and over until stopped. */
	void Init(TSharedPtr<const FMRPcmSound> InPcm, bool bLoop);
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
	TSharedPtr<const FMRPcmSound> Pcm;
	int32 Cursor = 0;
	bool bLoopPcm = false;
};

namespace MRServerSound
{
	/** Decode an Ogg Vorbis file (the original's .ogg) to PCM; null with Error if it isn't one. */
	MERIDIANREMASTERED_API TSharedPtr<FMRPcmSound> Decode(const TArray<uint8>& Ogg, FString& Error);
}
