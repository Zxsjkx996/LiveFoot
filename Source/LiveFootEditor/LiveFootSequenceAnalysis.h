#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "Diagnostics/AnimFootDiagnosticModel.h"
#include "UObject/StrongObjectPtr.h"

struct FLiveFootSequenceAnalysisOptions
{
	FName LeftFootBone = TEXT("foot_l");
	FName RightFootBone = TEXT("foot_r");
	FVector LeftSoleOffset = FVector::ZeroVector;
	FVector RightSoleOffset = FVector::ZeroVector;
	float SampleRate = 30.f;
	float GroundHeight = 0.f;
	FVector GroundNormal = FVector::UpVector;
	FAnimFootDiagnosticSettings DiagnosticSettings;
};

struct FLiveFootSequenceSample
{
	float Time = 0.f;
	FAnimFootDebugResult Left;
	FAnimFootDebugResult Right;
};

struct FLiveFootSequenceAnalysisResult
{
	bool bSuccess = false;
	bool bSequenceRawPose = true;
	FString AssetPath;
	FString FailureReason;
	int32 SampleCount = 0;
	FAnimFootAnalysisSummary LeftSummary;
	FAnimFootAnalysisSummary RightSummary;
	TArray<FAnimFootAnalysisInterval> LeftIntervals;
	TArray<FAnimFootAnalysisInterval> RightIntervals;
	TArray<FLiveFootSequenceSample> Samples;
};

/** Game-thread only. Owns the active sequence across GC; Step bounds work by samples and wall time. */
class LIVEFOOTEDITOR_API FLiveFootSequenceAnalysisSession
{
public:
	bool Begin(UAnimSequence* Sequence, const FLiveFootSequenceAnalysisOptions& InOptions);
	bool Step(int32 MaxSamples = 32, double TimeBudgetSeconds = 0.004);
	void Cancel();
	float GetProgress() const { return TotalSamples > 0 ? static_cast<float>(NextSample) / TotalSamples : 0.f; }
	FLiveFootSequenceAnalysisResult Result;
private:
	TStrongObjectPtr<UAnimSequence> ActiveSequence;
	FLiveFootSequenceAnalysisOptions Options;
	FGuid SourceGuid;
	FVector Normal = FVector::UpVector;
	FFrameRate EvaluationRate;
	float PlayLength = 0.f;
	int32 TotalSamples = 0;
	int32 NextSample = 0;
	bool bFinished = true;
	FAnimFootHistory LeftHistory, RightHistory;
	FAnimFootAnalysisAccumulator LeftAccumulator, RightAccumulator;
};

namespace LiveFootSequenceAnalysis
{
	LIVEFOOTEDITOR_API bool Analyze(UAnimSequence* Sequence, const FLiveFootSequenceAnalysisOptions& Options,
		FLiveFootSequenceAnalysisResult& OutResult);
	LIVEFOOTEDITOR_API bool ExportCsv(const TArray<FLiveFootSequenceAnalysisResult>& Results, const FString& Directory,
		FString& OutSummaryPath, FString& OutSamplesPath, FString& OutError);
}
