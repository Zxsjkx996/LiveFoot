#pragma once

#include "CoreMinimal.h"
#include "LiveFootSequenceAnalysis.h"

/** Explicit opt-in settings for creating a corrected animation copy. */
struct FLiveFootBakeOptions
{
	FString OutputFolder = TEXT("/Game/LiveFootGenerated");
	FString NameSuffix = TEXT("_FootFixed");
	bool bCorrectLeft = true;
	bool bCorrectRight = true;
	/** Tests may keep the corrected duplicate transient; production UI leaves this enabled. */
	bool bSaveToDisk = true;
};

struct FLiveFootBakeResult
{
	bool bSuccess = false;
	FString AssetPath;
	FString PackageFilename;
	FString AuditFilename;
	FString Error;
	int32 CorrectedKeys = 0;
	float MaxCorrection = 0.f;
	float BeforeSlipDuration = 0.f;
	float AfterSlipDuration = 0.f;
	float MaxAfterContactHeight = 0.f;
};

namespace LiveFootAnimationBake
{
	/** Creates and saves a new UAnimSequence. The source sequence is never modified or saved. */
	LIVEFOOTEDITOR_API bool BakeCorrectedCopy(UAnimSequence* Source, const FLiveFootSequenceAnalysisOptions& Options,
		const FLiveFootSequenceAnalysisResult& Analysis, const FLiveFootBakeOptions& BakeOptions, FLiveFootBakeResult& OutResult);
}
