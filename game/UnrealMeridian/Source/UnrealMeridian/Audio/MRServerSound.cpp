#include "Audio/MRServerSound.h"

#include "Decoders/VorbisAudioInfo.h"
#include "Interfaces/IAudioFormat.h"

void UMRServerSoundWave::Init(TSharedPtr<const FMRPcmSound> InPcm, bool bLoop)
{
	Pcm = InPcm;
	Cursor = 0;
	bLoopPcm = bLoop;
	SetSampleRate(Pcm->SampleRate);
	NumChannels = Pcm->Channels;
	Duration = bLoop ? INDEFINITELY_LOOPING_DURATION : Pcm->DurationSeconds();
	bLooping = bLoop;
	SoundGroup = SOUNDGROUP_Default;
}

int32 UMRServerSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	// NumSamples is in samples across the channels; the mixer wants 16-bit bytes
	OutAudio.Reset();
	if (!Pcm.IsValid() || Pcm->Samples.Num() == 0)
	{
		return 0;
	}
	const TArray<int16>& S = Pcm->Samples;
	OutAudio.SetNumZeroed(NumSamples * sizeof(int16));
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	int32 Written = 0;
	while (Written < NumSamples)
	{
		if (Cursor >= S.Num())
		{
			if (!bLoopPcm)
			{
				break;  // the rest stays silent; the component is stopped at the sound's end
			}
			Cursor = 0;
		}
		const int32 N = FMath::Min(NumSamples - Written, S.Num() - Cursor);
		FMemory::Memcpy(Out + Written, S.GetData() + Cursor, N * sizeof(int16));
		Cursor += N;
		Written += N;
	}
	return NumSamples;
}

TSharedPtr<FMRPcmSound> MRServerSound::Decode(const TArray<uint8>& Ogg, FString& Error)
{
#if WITH_OGGVORBIS
	LoadVorbisLibraries();
	FVorbisAudioInfo Info;
	FSoundQualityInfo Quality;
	FMemory::Memzero(Quality);
	// an Ogg stream starts "OggS" (the engine's reader logs an error on anything else)
	if (Ogg.Num() < 4 || FMemory::Memcmp(Ogg.GetData(), "OggS", 4) != 0 || !Info.ReadCompressedInfo(Ogg.GetData(), Ogg.Num(), &Quality) || Quality.SampleDataSize == 0)
	{
		Error = TEXT("not an Ogg Vorbis file");
		return nullptr;
	}
	TSharedPtr<FMRPcmSound> Pcm = MakeShared<FMRPcmSound>();
	Pcm->SampleRate = static_cast<int32>(Quality.SampleRate);
	Pcm->Channels = static_cast<int32>(Quality.NumChannels);
	Pcm->Samples.SetNumZeroed(Quality.SampleDataSize / sizeof(int16));
	Info.ExpandFile(reinterpret_cast<uint8*>(Pcm->Samples.GetData()), &Quality);
	return Pcm;
#else
	Error = TEXT("no Ogg Vorbis decoder on this platform");
	return nullptr;
#endif
}
