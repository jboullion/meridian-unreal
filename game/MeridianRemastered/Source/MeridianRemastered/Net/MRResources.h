#pragma once

#include "CoreMinimal.h"

class FMRReader;

/**
 * The server's string resources: its compiled rsc0000.rsb (names of rooms, bitmaps, objects,
 * message formats, the security redbook) plus the dynamic ones BP_CHANGE_RESOURCE and the player
 * lists add at runtime. It has to be *the server's* rsb: the redbook and every id depend on its build.
 *
 * File format (util/rscload.c): "RSC\x01", i32 version (5), i32 count, then per entry
 * i32 id, i32 language, NUL-terminated string. Language 0 is the default text.
 */
class MERIDIANREMASTERED_API FMRResourceTable
{
public:
	/** Parse an rsb; false if it isn't one. */
	bool Load(const TArray<uint8>& Rsb);
	bool IsLoaded() const { return Strings.Num() > 0; }
	int32 Num() const { return Strings.Num(); }

	/** The text of a resource (dynamic first), or null. */
	const FString* Find(uint32 Id) const;
	/** The raw bytes of a resource (the redbook is used byte by byte). */
	TArray<uint8> Bytes(uint32 Id) const;
	/** The text, or "" for an unknown id. */
	FString Get(uint32 Id) const;
	/** The rsb's resources with exactly this text (Kod's message formats, to recognise a message). */
	TArray<uint32> FindByText(const FString& Text) const;

	void SetDynamic(uint32 Id, const FString& Text) { Dynamic.Add(Id, Text); }
	void ClearDynamic() { Dynamic.Reset(); }

private:
	TMap<uint32, FString> Strings;
	TMap<uint32, FString> Dynamic;
};

/**
 * Expands a server message: a format resource plus its parameters, as the original client does
 * (clientd3d srvrstr.c semantics, written fresh):
 *   %d %i  a 32-bit integer
 *   %s     a resource id: its text, which may hold further formatters for the parameters after it
 *   %q     a literal string parameter (u16 length + text), inserted as-is
 *   %r     a resource id formatted as a whole message with the parameters that follow
 *   %%     a percent sign
 * The text's style codes (~B bold, ~I italic, ~U underline, ~n normal, ~<colour letter>) are
 * dropped: the chat log draws plain text for now.
 */
/** A run of chat text in one colour and style (the server's "~" codes: MRServerText::Runs). */
struct FMRTextRun
{
	FString Text;
	/** Unset: the line's own colour. */
	TOptional<FLinearColor> Color;
	/** MRServerText::STYLE_* bits. */
	uint8 Style = 0;
};

namespace MRServerText
{
	constexpr uint8 STYLE_ITALIC = 1;
	constexpr uint8 STYLE_BOLD = 2;
	constexpr uint8 STYLE_UNDERLINE = 4;

	/**
	 * Text in runs by its style codes (clientd3d srvrstr.c DisplayMessage): "~" or "`" and a letter:
	 * a colour (r f g l b k w y p o a c q t s v m), B, I or U toggling bold, italic or underline, n
	 * back to the line's own colour and style. Any other letter after "~" stays as typed.
	 */
	MERIDIANREMASTERED_API TArray<FMRTextRun> Runs(const FString& In);
	/** The colour of a code letter, if it is one. */
	MERIDIANREMASTERED_API bool CodeColor(TCHAR Code, FLinearColor& Out);
	/** Format FormatId reading parameters from Reader. False if a parameter is missing. */
	MERIDIANREMASTERED_API bool Format(const FMRResourceTable& Resources, uint32 FormatId, FMRReader& Reader, FString& Out);
	/** Remove the style codes (as Runs reads them). */
	MERIDIANREMASTERED_API FString StripStyle(const FString& In);
}
