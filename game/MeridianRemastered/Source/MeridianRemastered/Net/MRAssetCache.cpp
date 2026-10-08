#include "Net/MRAssetCache.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "MeridianRemastered.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/SecureHash.h"

void FMRAssetCache::Configure(const FString& InBaseUrl, const FString& InDir)
{
	CancelAll();
	BaseUrl = InBaseUrl;
	Dir = InDir;
	Hashes.Reset();
	Verified.Reset();
}

void FMRAssetCache::SetManifest(const TSharedPtr<FJsonObject>& Files)
{
	Hashes.Reset();
	Verified.Reset();
	if (!Files.IsValid())
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Files->Values)
	{
		const TSharedPtr<FJsonObject> Entry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
		FString Hash;
		if (Entry.IsValid() && Entry->TryGetStringField(TEXT("hash"), Hash))
		{
			Hashes.Add(Pair.Key.ToLower(), Hash.ToLower());
		}
	}
	UE_LOG(LogMeridian, Log, TEXT("MRNet: the server lists %d game files"), Hashes.Num());
}

FString FMRAssetCache::HashBytes(const TArray<uint8>& Bytes)
{
	uint8 Digest[FSHA1::DigestSize];
	FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
	return BytesToHex(Digest, 8).ToLower();
}

FString FMRAssetCache::PathOf(const FString& Name) const
{
	return FPaths::Combine(Dir, TEXT("assets"), Name);
}

bool FMRAssetCache::Load(const FString& InName, TArray<uint8>& Out)
{
	const FString Name = InName.ToLower();
	const FString* Hash = Hashes.Find(Name);
	if (!Hash || Dir.IsEmpty() || !FFileHelper::LoadFileToArray(Out, *PathOf(Name), FILEREAD_Silent))
	{
		Out.Reset();
		return false;
	}
	if (!Verified.Contains(Name))
	{
		if (HashBytes(Out) != *Hash)
		{
			Out.Reset();
			return false;  // an older version: Fetch replaces it
		}
		Verified.Add(Name);
	}
	return true;
}

void FMRAssetCache::Fetch(const FString& InName, FDone Done)
{
	const FString Name = InName.ToLower();
	TArray<uint8> Bytes;
	if (Load(Name, Bytes))
	{
		Done(true, Bytes);
		return;
	}
	if (!Hashes.Contains(Name) || BaseUrl.IsEmpty())
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: %s isn't among the server's game files"), *Name);
		Done(false, TArray<uint8>());
		return;
	}
	if (FJob* Running = Active.Find(Name))
	{
		Running->Waiting.Add(MoveTemp(Done));
		return;
	}
	if (FJob* Queued = Queue.FindByPredicate([&Name](const FJob& J) { return J.Name == Name; }))
	{
		Queued->Waiting.Add(MoveTemp(Done));
		return;
	}
	FJob& Job = Queue.AddDefaulted_GetRef();
	Job.Name = Name;
	Job.Waiting.Add(MoveTemp(Done));
	Pump();
}

void FMRAssetCache::Pump()
{
	while (Active.Num() < MaxParallel && Queue.Num() > 0)
	{
		FJob Job = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		const FString Name = Job.Name;
		Active.Add(Name, MoveTemp(Job));
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		// the hash in the query string: a changed file is never served from an HTTP cache
		Req->SetURL(FString::Printf(TEXT("%s/%s?v=%s"), *BaseUrl, *FGenericPlatformHttp::UrlEncode(Name), *Hashes.FindRef(Name)));
		Req->SetVerb(TEXT("GET"));
		Req->SetTimeout(60.f);
		TWeakPtr<FMRAssetCache> Weak = AsShared();
		Req->OnProcessRequestComplete().BindLambda([Weak, Name](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk)
		{
			if (TSharedPtr<FMRAssetCache> Self = Weak.Pin())
			{
				Self->OnDownloaded(Request, Response, bOk, Name);
			}
		});
		Requests.Add(Req);
		Req->ProcessRequest();
	}
}

void FMRAssetCache::OnDownloaded(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk, FString Name)
{
	Requests.Remove(Request);
	if (!Active.Contains(Name))
	{
		return;  // cancelled
	}
	const FString* Hash = Hashes.Find(Name);
	if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200 || !Hash)
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: couldn't download %s (%d)"), *Name, Response.IsValid() ? Response->GetResponseCode() : 0);
		Finish(Name, false, TArray<uint8>());
		return;
	}
	const TArray<uint8>& Bytes = Response->GetContent();
	const FString Got = HashBytes(Bytes);
	if (Got != *Hash)
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: %s doesn't match the server's manifest (%s, expected %s)"), *Name, *Got, **Hash);
		Finish(Name, false, TArray<uint8>());
		return;
	}
	const FString Path = PathOf(Name);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		UE_LOG(LogMeridian, Warning, TEXT("MRNet: couldn't save %s"), *Path);
	}
	Verified.Add(Name);
	Finish(Name, true, Bytes);
}

void FMRAssetCache::Finish(const FString& Name, bool bOk, const TArray<uint8>& Bytes)
{
	FJob Job;
	Active.RemoveAndCopyValue(Name, Job);
	for (FDone& Done : Job.Waiting)
	{
		Done(bOk, Bytes);
	}
	Pump();
}

void FMRAssetCache::CancelAll()
{
	Queue.Reset();
	Active.Reset();
	for (const FHttpRequestPtr& Req : Requests)
	{
		Req->OnProcessRequestComplete().Unbind();
		Req->CancelRequest();
	}
	Requests.Reset();
}
