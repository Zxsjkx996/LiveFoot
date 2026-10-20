#include "LiveFootSequenceAnalysis.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

namespace
{
	bool IsFiniteTransform(const FTransform& Transform)
	{
		return Transform.IsValid() && AnimFootDiagnostics::IsFiniteVector(Transform.GetLocation())
			&& FMath::IsFinite(Transform.GetRotation().X) && FMath::IsFinite(Transform.GetRotation().Y)
			&& FMath::IsFinite(Transform.GetRotation().Z) && FMath::IsFinite(Transform.GetRotation().W);
	}

	bool BuildComponentPose(UAnimSequence* Sequence, const IAnimationDataModel* Model, double Time,
		const FFrameRate& EvaluationRate, TArray<FTransform>& OutComponentPose, FString& OutFailure)
	{
		USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
		if (!Skeleton) { OutFailure = TEXT("Animation has no Skeleton"); return false; }
		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
		const int32 BoneCount = ReferenceSkeleton.GetNum();
		if (BoneCount <= 0) { OutFailure = TEXT("Skeleton has no reference bones"); return false; }
		OutComponentPose.SetNum(BoneCount);
		const TArray<FTransform>& RefPose = ReferenceSkeleton.GetRefBonePose();
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FName BoneName = ReferenceSkeleton.GetBoneName(BoneIndex);
			FTransform Local = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
			if (Model->IsValidBoneTrackName(BoneName))
			{
				Local = Model->EvaluateBoneTrackTransform(BoneName, EvaluationRate.AsFrameTime(Time), Sequence->Interpolation);
			}
			if (!IsFiniteTransform(Local))
			{
				OutFailure = FString::Printf(TEXT("Non-finite transform for bone %s"), *BoneName.ToString());
				return false;
			}
			const int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
			OutComponentPose[BoneIndex] = ParentIndex == INDEX_NONE ? Local : Local * OutComponentPose[ParentIndex];
			if (!IsFiniteTransform(OutComponentPose[BoneIndex]))
			{
				OutFailure = FString::Printf(TEXT("Invalid component transform for bone %s"), *BoneName.ToString());
				return false;
			}
		}
		return true;
	}

	bool MakeResult(const FReferenceSkeleton& ReferenceSkeleton, const TArray<FTransform>& ComponentPose,
		FName BoneName, const FVector& SoleOffset, float GroundHeight, const FVector& GroundNormal,
		FAnimFootDebugResult& OutResult)
	{
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE || !ComponentPose.IsValidIndex(BoneIndex)) { return false; }
		OutResult = FAnimFootDebugResult();
		OutResult.Bone = BoneName;
		OutResult.BonePosition = ComponentPose[BoneIndex].GetLocation();
		OutResult.SolePosition = ComponentPose[BoneIndex].TransformPosition(SoleOffset);
		OutResult.bValidPosition = AnimFootDiagnostics::IsFiniteVector(OutResult.BonePosition)
			&& AnimFootDiagnostics::IsFiniteVector(OutResult.SolePosition);
		OutResult.bGroundHit = true;
		OutResult.bValidGround = true;
		OutResult.GroundNormal = GroundNormal;
		const FVector GroundOrigin(0.f, 0.f, GroundHeight);
		OutResult.GroundPoint = OutResult.SolePosition
			- FVector::DotProduct(OutResult.SolePosition - GroundOrigin, GroundNormal) * GroundNormal;
		OutResult.Height = FVector::DotProduct(OutResult.SolePosition - OutResult.GroundPoint, GroundNormal);
		return OutResult.bValidPosition && FMath::IsFinite(OutResult.Height);
	}
}

bool FLiveFootSequenceAnalysisSession::Begin(UAnimSequence* Sequence, const FLiveFootSequenceAnalysisOptions& InOptions)
{
	check(IsInGameThread());
	ActiveSequence.Reset();
	Options = InOptions;
	Result = FLiveFootSequenceAnalysisResult();
	FLiveFootSequenceAnalysisResult& OutResult = Result;
	bFinished = true;
	NextSample = TotalSamples = 0;
	LeftHistory = FAnimFootHistory(); RightHistory = FAnimFootHistory();
	LeftAccumulator.Reset(); RightAccumulator.Reset();
	OutResult = FLiveFootSequenceAnalysisResult();
	OutResult.AssetPath = GetPathNameSafe(Sequence);
	if (!Sequence) { OutResult.FailureReason = TEXT("Animation Sequence is null"); return false; }
	if (!Options.DiagnosticSettings.IsValid()) { OutResult.FailureReason = TEXT("Diagnostic settings are invalid"); return false; }
	if (!FMath::IsFinite(Options.SampleRate) || Options.SampleRate <= 0.f || Options.SampleRate > 240.f)
	{
		OutResult.FailureReason = TEXT("Sample rate must be finite and in (0, 240]"); return false;
	}
	if (!AnimFootDiagnostics::IsFiniteVector(Options.LeftSoleOffset)
		|| !AnimFootDiagnostics::IsFiniteVector(Options.RightSoleOffset)
		|| !AnimFootDiagnostics::IsFiniteVector(Options.GroundNormal))
	{
		OutResult.FailureReason = TEXT("Foot offsets or ground normal are not finite"); return false;
	}
	Normal = Options.GroundNormal.GetSafeNormal();
	if (!AnimFootDiagnostics::IsFiniteVector(Normal) || Normal.IsNearlyZero() || !FMath::IsFinite(Options.GroundHeight))
	{
		OutResult.FailureReason = TEXT("Ground plane is invalid"); return false;
	}
	if (Normal.Z < FMath::Cos(FMath::DegreesToRadians(Options.DiagnosticSettings.MaxGroundSlope)))
	{
		OutResult.FailureReason = TEXT("Ground plane exceeds the supported Z-up slope"); return false;
	}
	USkeleton* Skeleton = Sequence->GetSkeleton();
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	if (!Skeleton || !Model) { OutResult.FailureReason = TEXT("Animation Skeleton or Data Model is unavailable"); return false; }
	if (Sequence->GetAdditiveAnimType() != AAT_None)
	{
		OutResult.FailureReason = TEXT("Additive Animation Sequences are not supported by raw track analysis");
		return false;
	}
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	if (ReferenceSkeleton.FindBoneIndex(Options.LeftFootBone) == INDEX_NONE
		|| ReferenceSkeleton.FindBoneIndex(Options.RightFootBone) == INDEX_NONE)
	{
		OutResult.FailureReason = TEXT("Configured foot bone is missing from the Skeleton"); return false;
	}
	PlayLength = Sequence->GetPlayLength();
	if (!FMath::IsFinite(PlayLength) || PlayLength < 0.0) { OutResult.FailureReason = TEXT("Animation play length is invalid"); return false; }
	EvaluationRate = Model->GetFrameRate();
	if (EvaluationRate.Numerator <= 0 || EvaluationRate.Denominator <= 0 || !EvaluationRate.IsValid())
	{
		OutResult.FailureReason = TEXT("Animation Data Model has an invalid frame rate");
		return false;
	}
	const double SampleIntervals = FMath::CeilToDouble(PlayLength * Options.SampleRate);
	if (PlayLength <= 0.0 || SampleIntervals > 1000000.0
		|| 1.f / Options.SampleRate < Options.DiagnosticSettings.MinDeltaTime
		|| 1.f / Options.SampleRate > Options.DiagnosticSettings.MaxDeltaTime)
	{
		OutResult.FailureReason = TEXT("Animation duration or sample interval is unsupported (maximum 1000001 samples)");
		return false;
	}
	TotalSamples = static_cast<int32>(SampleIntervals) + 1;
	SourceGuid = Model->GenerateGuid();
	ActiveSequence.Reset(Sequence);
	bFinished = false;
	return true;
}

bool FLiveFootSequenceAnalysisSession::Step(int32 MaxSamples, double TimeBudgetSeconds)
{
	check(IsInGameThread());
	if (bFinished) { return true; }
	UAnimSequence* Sequence = ActiveSequence.Get();
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	FLiveFootSequenceAnalysisResult& OutResult = Result;
	if (!Model || !Sequence->GetSkeleton())
	{
		OutResult.FailureReason = TEXT("Source data became unavailable during analysis");
		bFinished = true; ActiveSequence.Reset(); return true;
	}
	const FReferenceSkeleton& ReferenceSkeleton = Sequence->GetSkeleton()->GetReferenceSkeleton();
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.0001, TimeBudgetSeconds);
	int32 Processed = 0;
	while (NextSample < TotalSamples && Processed < FMath::Max(1, MaxSamples))
	{
		const int32 SampleIndex = NextSample++;
		float Time = FMath::Min(PlayLength, SampleIndex / Options.SampleRate);
		// Merge a floating-point-sized final remainder rather than fabricating a nonzero dt.
		if (PlayLength - Time < Options.DiagnosticSettings.MinDeltaTime) { Time = PlayLength; NextSample = TotalSamples; }
		const float Dt = SampleIndex == 0 ? 1.f / Options.SampleRate : Time - OutResult.Samples.Last().Time;
		if (SampleIndex > 0 && Dt < Options.DiagnosticSettings.MinDeltaTime)
		{
			OutResult.FailureReason = TEXT("Sample interval is too small for the configured diagnostics");
			bFinished = true; ActiveSequence.Reset(); return true;
		}
		TArray<FTransform> ComponentPose;
		if (!BuildComponentPose(Sequence, Model, Time, EvaluationRate, ComponentPose, OutResult.FailureReason))
		{ bFinished = true; ActiveSequence.Reset(); return true; }
		FLiveFootSequenceSample Sample;
		if (!MakeResult(ReferenceSkeleton, ComponentPose, Options.LeftFootBone, Options.LeftSoleOffset, Options.GroundHeight, Normal, Sample.Left)
			|| !MakeResult(ReferenceSkeleton, ComponentPose, Options.RightFootBone, Options.RightSoleOffset, Options.GroundHeight, Normal, Sample.Right))
		{
			OutResult.FailureReason = TEXT("Foot transform could not be evaluated");
			bFinished = true; ActiveSequence.Reset(); return true;
		}
		Sample.Time = Time;
		AnimFootDiagnostics::Analyze(Sample.Left, LeftHistory, Options.DiagnosticSettings, Dt, false, true);
		AnimFootDiagnostics::Analyze(Sample.Right, RightHistory, Options.DiagnosticSettings, Dt, false, true);
		// t=0 is a warmup position, not an extra interval before the animation starts.
		if (SampleIndex > 0)
		{
			LeftAccumulator.AddSample(Sample.Left, Dt, Time);
			RightAccumulator.AddSample(Sample.Right, Dt, Time);
		}
		OutResult.Samples.Add(MoveTemp(Sample));
		++Processed;
		if (FPlatformTime::Seconds() >= Deadline) { break; }
	}
	if (NextSample < TotalSamples) { return false; }
	if (Model->GenerateGuid() != SourceGuid)
	{
		OutResult.FailureReason = TEXT("Source animation changed during analysis; rerun required");
		bFinished = true; ActiveSequence.Reset(); return true;
	}
	OutResult.SampleCount = OutResult.Samples.Num();
	OutResult.LeftSummary = LeftAccumulator.Summary;
	OutResult.RightSummary = RightAccumulator.Summary;
	OutResult.LeftIntervals = MoveTemp(LeftAccumulator.Intervals);
	OutResult.RightIntervals = MoveTemp(RightAccumulator.Intervals);
	OutResult.bSuccess = true;
	bFinished = true;
	ActiveSequence.Reset();
	return true;
}

void FLiveFootSequenceAnalysisSession::Cancel()
{
	check(IsInGameThread());
	Result = FLiveFootSequenceAnalysisResult();
	Result.FailureReason = TEXT("Cancelled; partial samples discarded");
	bFinished = true;
	ActiveSequence.Reset();
}

bool LiveFootSequenceAnalysis::Analyze(UAnimSequence* Sequence, const FLiveFootSequenceAnalysisOptions& Options,
	FLiveFootSequenceAnalysisResult& OutResult)
{
	FLiveFootSequenceAnalysisSession Session;
	if (Session.Begin(Sequence, Options)) { while (!Session.Step(256, 0.02)) {} }
	OutResult = MoveTemp(Session.Result);
	return OutResult.bSuccess;
}

namespace
{
	FString CsvEscape(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

bool LiveFootSequenceAnalysis::ExportCsv(const TArray<FLiveFootSequenceAnalysisResult>& Results, const FString& Directory,
	FString& OutSummaryPath, FString& OutSamplesPath, FString& OutError)
{
	OutSummaryPath.Reset(); OutSamplesPath.Reset(); OutError.Reset();
	if (Results.Num() == 0) { OutError = TEXT("There are no analysis results to export"); return false; }
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Could not create directory: %s"), *Directory); return false;
	}
	FString SummaryCsv = TEXT("asset_path,status,samples,left_contact_s,left_slip_s,left_p95_cm_s,right_contact_s,right_slip_s,right_p95_cm_s,error\n");
	FString SamplesCsv = TEXT("asset_path,time_s,left_x_cm,left_y_cm,left_z_cm,right_x_cm,right_y_cm,right_z_cm,left_phase,right_phase,left_slip,right_slip\n");
	for (const FLiveFootSequenceAnalysisResult& Result : Results)
	{
		const FString Status = Result.bSuccess ? TEXT("OK") : TEXT("FAILED");
		SummaryCsv += FString::Printf(TEXT("%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s\n"),
			*CsvEscape(Result.AssetPath), *Status, Result.SampleCount,
			Result.LeftSummary.ContactDuration, Result.LeftSummary.SuspectedSlipDuration, Result.LeftSummary.P95TangentSpeed,
			Result.RightSummary.ContactDuration, Result.RightSummary.SuspectedSlipDuration, Result.RightSummary.P95TangentSpeed,
			*CsvEscape(Result.FailureReason));
		for (const FLiveFootSequenceSample& Sample : Result.Samples)
		{
			SamplesCsv += FString::Printf(TEXT("%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d\n"),
				*CsvEscape(Result.AssetPath), Sample.Time,
				Sample.Left.SolePosition.X, Sample.Left.SolePosition.Y, Sample.Left.SolePosition.Z,
				Sample.Right.SolePosition.X, Sample.Right.SolePosition.Y, Sample.Right.SolePosition.Z,
				static_cast<int32>(Sample.Left.Phase), static_cast<int32>(Sample.Right.Phase),
				Sample.Left.bSuspectedSlip ? 1 : 0, Sample.Right.bSuspectedSlip ? 1 : 0);
		}
	}
	OutSummaryPath = Directory / TEXT("LiveFoot_summary.csv");
	OutSamplesPath = Directory / TEXT("LiveFoot_samples.csv");
	if (!FFileHelper::SaveStringToFile(SummaryCsv, *OutSummaryPath, FFileHelper::EEncodingOptions::ForceUTF8)
		|| !FFileHelper::SaveStringToFile(SamplesCsv, *OutSamplesPath, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		OutError = TEXT("Failed to write one or more CSV files"); return false;
	}
	return true;
}
