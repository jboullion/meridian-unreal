#include "Net/MRResources.h"

#include "Net/MRProtocol.h"

bool FMRResourceTable::Load(const TArray<uint8>& Rsb)
{
	FMRReader R(Rsb.GetData(), Rsb.Num());
	if (Rsb.Num() < 12 || R.U8() != 'R' || R.U8() != 'S' || R.U8() != 'C' || R.U8() != 1)
	{
		return false;
	}
	const int32 Version = R.I32();
	const int32 Count = R.I32();
	if (Version != 5 || Count < 0)
	{
		return false;
	}
	Strings.Reset();
	Strings.Reserve(Count);
	int32 Pos = 12;
	for (int32 i = 0; i < Count; ++i)
	{
		if (Pos + 8 > Rsb.Num())
		{
			return false;
		}
		FMRReader Head(Rsb.GetData(), Rsb.Num(), Pos);
		const uint32 Id = Head.U32();
		const int32 Lang = Head.I32();
		Pos += 8;
		int32 End = Pos;
		while (End < Rsb.Num() && Rsb[End] != 0)
		{
			++End;
		}
		if (End >= Rsb.Num())
		{
			return false;
		}
		// language 0 is the default; another language only fills a gap
		if (Lang == 0 || !Strings.Contains(Id))
		{
			FString S;
			S.Reserve(End - Pos);
			for (int32 k = Pos; k < End; ++k)
			{
				S.AppendChar(static_cast<TCHAR>(Rsb[k]));
			}
			Strings.Add(Id, MoveTemp(S));
		}
		Pos = End + 1;
	}
	return true;
}

TArray<uint32> FMRResourceTable::FindByText(const FString& Text) const
{
	TArray<uint32> Ids;
	for (const TPair<uint32, FString>& Pair : Strings)
	{
		if (Pair.Value.Equals(Text, ESearchCase::CaseSensitive))
		{
			Ids.Add(Pair.Key);
		}
	}
	return Ids;
}

const FString* FMRResourceTable::Find(uint32 Id) const
{
	if (const FString* D = Dynamic.Find(Id))
	{
		return D;
	}
	return Strings.Find(Id);
}

FString FMRResourceTable::Get(uint32 Id) const
{
	const FString* S = Find(Id);
	return S ? *S : FString();
}

TArray<uint8> FMRResourceTable::Bytes(uint32 Id) const
{
	const FString* S = Find(Id);
	return S ? MRProto::Latin1(*S) : TArray<uint8>();
}

// ------------------------------------------------------------------------------ server text

namespace
{
	bool FormatInto(const FMRResourceTable& Res, const FString& InFmt, FMRReader& Reader, FString& Out, int32 Depth)
	{
		if (Depth > 8)
		{
			return false;
		}
		FString Fmt = InFmt;
		int32 Budget = 256;  // %s inserts text that is scanned again; never loop forever
		for (int32 i = 0; i < Fmt.Len(); ++i)
		{
			const TCHAR C = Fmt[i];
			if (C != TEXT('%') || i + 1 >= Fmt.Len())
			{
				Out.AppendChar(C);
				continue;
			}
			const TCHAR T = Fmt[i + 1];
			// "$0" right after a formatter: consume the parameter, show nothing
			const bool bHide = i + 3 < Fmt.Len() && Fmt[i + 2] == TEXT('$') && Fmt[i + 3] == TEXT('0');
			const int32 After = i + (bHide ? 4 : 2);
			switch (T)
			{
			case TEXT('%'):
				Out.AppendChar(TEXT('%'));
				i += 1;
				break;
			case TEXT('d'):
			case TEXT('i'):
			{
				const int32 V = Reader.I32();
				if (!Reader.IsOk())
				{
					return false;
				}
				if (!bHide)
				{
					Out.AppendInt(V);
				}
				i = After - 1;
				break;
			}
			case TEXT('q'):
			{
				const FString S = Reader.Str();
				if (!Reader.IsOk())
				{
					return false;
				}
				if (!bHide)
				{
					Out += S;
				}
				i = After - 1;
				break;
			}
			case TEXT('s'):
			{
				const uint32 Id = Reader.U32();
				if (!Reader.IsOk() || --Budget < 0)
				{
					return false;
				}
				const FString Text = bHide ? FString() : Res.Get(Id);
				// the inserted text is scanned too: it may have formatters for the next parameters
				Fmt = Fmt.Left(i) + Text + Fmt.Mid(After);
				i -= 1;
				break;
			}
			case TEXT('r'):
			{
				const uint32 Id = Reader.U32();
				if (!Reader.IsOk())
				{
					return false;
				}
				FString Sub;
				const FString* SubFmt = Res.Find(Id);
				if (!FormatInto(Res, SubFmt ? *SubFmt : FString(), Reader, Sub, Depth + 1))
				{
					return false;
				}
				if (!bHide)
				{
					Out += Sub;
				}
				i = After - 1;
				break;
			}
			default:
				Out.AppendChar(C);  // a lone percent ("50% off")
				break;
			}
		}
		return true;
	}
}

bool MRServerText::Format(const FMRResourceTable& Resources, uint32 FormatId, FMRReader& Reader, FString& Out)
{
	Out.Reset();
	const FString* Fmt = Resources.Find(FormatId);
	if (!Fmt)
	{
		return false;
	}
	return FormatInto(Resources, *Fmt, Reader, Out, 0);
}

FString MRServerText::StripStyle(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	for (int32 i = 0; i < In.Len(); ++i)
	{
		if (In[i] == TEXT('~') && i + 1 < In.Len())
		{
			++i;  // skip the code letter
			continue;
		}
		Out.AppendChar(In[i]);
	}
	return Out;
}
