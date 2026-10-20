#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "LiveFootSequenceAnalysis.h"
#include "LiveFootBatchAnalysis.h"
#include "LiveFootReports.h"
#include "LiveFootAnimationBake.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootSequenceAnalysisAssetTest, "LiveFoot.Editor.SequenceAnalysis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootSequenceAnalysisAssetTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (!TestNotNull(TEXT("Known idle Animation Sequence loads"), Sequence)) { return false; }
	const bool bWasDirty = Sequence->GetOutermost()->IsDirty();
	FLiveFootSequenceAnalysisOptions Options;
	Options.SampleRate = 30.f;
	FLiveFootSequenceAnalysisResult Result;
	TestTrue(TEXT("Sequence analysis succeeds"), LiveFootSequenceAnalysis::Analyze(Sequence, Options, Result));
	TestTrue(TEXT("Result explicitly identifies raw Sequence pose"), Result.bSuccess && Result.bSequenceRawPose);
	TestTrue(TEXT("Fixed-rate sampling produces multiple samples"), Result.SampleCount > 1 && Result.SampleCount == Result.Samples.Num());
	TestTrue(TEXT("Both configured foot transforms are valid"), Result.Samples[0].Left.bValidPosition && Result.Samples[0].Right.bValidPosition);
	TestTrue(TEXT("Both summaries receive samples"), Result.LeftSummary.SampleCount > 0 && Result.RightSummary.SampleCount > 0);
	TestFalse(TEXT("Analysis does not dirty the source asset"), Sequence->GetOutermost()->IsDirty() && !bWasDirty);

	Options.LeftFootBone = TEXT("MissingFootForTest");
	FLiveFootSequenceAnalysisResult Failure;
	TestFalse(TEXT("Missing foot bone fails structurally"), LiveFootSequenceAnalysis::Analyze(Sequence, Options, Failure));
	TestTrue(TEXT("Missing foot failure has a reason"), !Failure.FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootSequenceControlledTest, "LiveFoot.Editor.SequenceControlled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootSequenceControlledTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (!TestNotNull(TEXT("Fixture loads"), Source)) { return false; }
	const bool bWasDirty = Source->GetOutermost()->IsDirty();
	// Retain a fully initialized Data Model on a transient copy; new sequences can use a Sequencer model.
	UAnimSequence* Fixture = DuplicateObject<UAnimSequence>(Source, GetTransientPackage());
	if (Fixture->AssetImportData) { Fixture->AssetImportData->ConditionalPostLoad(); }
	IAnimationDataController& Controller = Fixture->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("LiveFoot transient controlled fixture")), false);
	Controller.RemoveAllBoneTracks(false);
	Controller.SetFrameRate(FFrameRate(60, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(60), false);
	const FName RootBone = Fixture->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0);
	Controller.AddBoneCurve(RootBone, false);
	TArray<FVector3f> Positions;
	TArray<FQuat4f> Rotations;
	TArray<FVector3f> Scales;
	for (int32 Key = 0; Key <= 60; ++Key)
	{
		Positions.Add(FVector3f(Key * 0.5f, 0.f, 0.f));
		Rotations.Add(FQuat4f::Identity);
		Scales.Add(FVector3f::OneVector);
	}
	const bool bKeysSet = Controller.SetBoneTrackKeys(RootBone, Positions, Rotations, Scales, false);
	Controller.CloseBracket(false);
	if (!TestTrue(TEXT("Controlled root track created"), bKeysSet)) { return false; }
	FLiveFootSequenceAnalysisOptions Options;
	Options.DiagnosticSettings.SmoothingTime = 0.f;
	FLiveFootSequenceAnalysisResult Result;
	if (!TestTrue(TEXT("Controlled fixture evaluates"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result))) { return false; }
	Options.GroundHeight = Result.Samples[0].Left.SolePosition.Z;
	for (float Rate : {30.f, 60.f, 120.f, 29.97f})
	{
		Options.SampleRate = Rate;
		if (!TestTrue(TEXT("Controlled sampling succeeds"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result))) { return false; }
		TestTrue(TEXT("Source frame rate is independent of analysis rate"),
			FMath::IsNearlyEqual(Result.Samples.Last().Left.SolePosition.X - Result.Samples[0].Left.SolePosition.X, 30.0, 0.01));
		TestTrue(TEXT("Mean speed is 30 cm/s"), FMath::IsNearlyEqual(Result.LeftSummary.MeanTangentSpeed, 30.f, 0.05f));
		TestTrue(TEXT("Summary duration equals animation duration"), FMath::IsNearlyEqual(Result.LeftSummary.SampleDuration, 1.f, 0.0001f));
		TestTrue(TEXT("Controlled motion produces suspected slip"), Result.LeftSummary.SuspectedSlipDuration > 0.f);
		FLiveFootSequenceAnalysisResult Repeated;
		LiveFootSequenceAnalysis::Analyze(Fixture, Options, Repeated);
		TestEqual(TEXT("Repeated sample count"), Repeated.SampleCount, Result.SampleCount);
		TestEqual(TEXT("Repeated summary speed"), Repeated.LeftSummary.MeanTangentSpeed, Result.LeftSummary.MeanTangentSpeed);
	}
	Options.GroundNormal = FVector(0.2, 0.0, 1.0).GetSafeNormal();
	TestTrue(TEXT("Supported sloped plane evaluates"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result));
	const FAnimFootDebugResult& Last = Result.Samples.Last().Left;
	TestTrue(TEXT("Ground point lies on configured plane"), FMath::IsNearlyZero(
		FVector::DotProduct(Last.GroundPoint - FVector(0, 0, Options.GroundHeight), Options.GroundNormal), 0.001));
	Options.GroundNormal = FVector::ForwardVector;
	TestFalse(TEXT("Unsupported slope fails"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result));
	Options.GroundNormal = FVector::UpVector;
	Options.SampleRate = 0.f;
	TestFalse(TEXT("Invalid sample rate fails"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result));
	TestTrue(TEXT("Failure has reason"), !Result.FailureReason.IsEmpty());
	Options.SampleRate = 30.f;
	Fixture->AdditiveAnimType = AAT_LocalSpaceBase;
	TestFalse(TEXT("Additive source is rejected explicitly"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result));
	Fixture->AdditiveAnimType = AAT_None;
	for (FVector3f& Position : Positions) { Position = FVector3f::ZeroVector; }
	TestTrue(TEXT("Stationary root track created"), Controller.SetBoneTrackKeys(RootBone, Positions, Rotations, Scales, false));
	TestTrue(TEXT("Stationary fixture evaluates"), LiveFootSequenceAnalysis::Analyze(Fixture, Options, Result));
	TestTrue(TEXT("Stationary foot speed is zero"), FMath::IsNearlyZero(Result.LeftSummary.MeanTangentSpeed));
	TestTrue(TEXT("Stationary foot has no suspected slip"), FMath::IsNearlyZero(Result.LeftSummary.SuspectedSlipDuration));
	TestEqual(TEXT("Source dirty state unchanged after transient fixture"), Source->GetOutermost()->IsDirty(), bWasDirty);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootAnimationBakeTest, "LiveFoot.Editor.AnimationBake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootAnimationBakeTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (!TestNotNull(TEXT("Bake fixture loads"), Source)) { return false; }
	const bool bWasDirty = Source->GetOutermost()->IsDirty();
	FLiveFootSequenceAnalysisOptions Options;
	FLiveFootSequenceAnalysisResult Analysis;
	if (!TestTrue(TEXT("Bake fixture analyzes"), LiveFootSequenceAnalysis::Analyze(Source, Options, Analysis))) { return false; }
	FLiveFootBakeOptions BakeOptions;
	BakeOptions.bSaveToDisk = false;
	FLiveFootBakeResult BakeResult;
	TestTrue(TEXT("Transient corrected duplicate can be built and re-sampled"),
		LiveFootAnimationBake::BakeCorrectedCopy(Source, Options, Analysis, BakeOptions, BakeResult));
	TestEqual(TEXT("Clean animation receives no speculative contact correction"), BakeResult.CorrectedKeys, 0);
	TestTrue(TEXT("Bake reports a corrected duplicate path without saving"), BakeResult.AssetPath.Contains(TEXT("MM_Idle_FootFixed")) && BakeResult.PackageFilename.IsEmpty());
	TestTrue(TEXT("Bake leaves source package dirty state unchanged"), Source->GetOutermost()->IsDirty() == bWasDirty);

	// Exercise the opt-in correction path with an explicitly diagnosed Contact
	// sample. This keeps the regression test independent of the raw-vs-final
	// Anim Blueprint pose distinction while proving that eligible keys are still
	// written and re-sampled.
	bool bMarkedContact = false;
	for (FLiveFootSequenceSample& Sample : Analysis.Samples)
	{
		if (Sample.Left.Phase == EAnimFootPhase::Contact && Sample.Left.bValidGround)
		{
			Sample.Left.bSuspectedSlip = true;
			Sample.Left.Height = -1.f;
			bMarkedContact = true;
		}
		if (Sample.Right.Phase == EAnimFootPhase::Contact && Sample.Right.bValidGround)
		{
			Sample.Right.bSuspectedSlip = true;
			bMarkedContact = true;
		}
	}
	TestTrue(TEXT("Bake fixture has an eligible Contact sample"), bMarkedContact);
	FLiveFootBakeResult IssueBakeResult;
	TestTrue(TEXT("Explicit issue correction can be built and re-sampled"),
		LiveFootAnimationBake::BakeCorrectedCopy(Source, Options, Analysis, BakeOptions, IssueBakeResult));
	TestTrue(TEXT("Explicit issue bake reports corrected keys"), IssueBakeResult.CorrectedKeys > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootSequenceCsvTest, "LiveFoot.Editor.CsvExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootSequenceCsvTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (!TestNotNull(TEXT("CSV fixture loads"), Sequence)) { return false; }
	FLiveFootSequenceAnalysisOptions Options;
	FLiveFootSequenceAnalysisResult Result;
	if (!TestTrue(TEXT("CSV fixture analyzes"), LiveFootSequenceAnalysis::Analyze(Sequence, Options, Result))) { return false; }
	TArray<FLiveFootSequenceAnalysisResult> Results;
	Results.Add(Result);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/LiveFoot-V1.2-CsvTest");
	FString SummaryPath, SamplesPath, Error;
	TestTrue(TEXT("CSV export succeeds"), LiveFootSequenceAnalysis::ExportCsv(Results, Directory, SummaryPath, SamplesPath, Error));
	FString SummaryText, SamplesText;
	TestTrue(TEXT("Summary CSV can be read back"), FFileHelper::LoadFileToString(SummaryText, *SummaryPath));
	TestTrue(TEXT("Sample CSV can be read back"), FFileHelper::LoadFileToString(SamplesText, *SamplesPath));
	TestTrue(TEXT("Summary header is stable"), SummaryText.StartsWith(TEXT("asset_path,status,samples,left_contact_s")));
	TestTrue(TEXT("Sample header is stable"), SamplesText.StartsWith(TEXT("asset_path,time_s,left_x_cm")));
	TArray<FString> SampleLines;
	SamplesText.ParseIntoArrayLines(SampleLines);
	TestEqual(TEXT("Sample row count matches result"), SampleLines.Num(), Result.SampleCount + 1);
	TestTrue(TEXT("CSV contains raw pose marker asset path"), SummaryText.Contains(TEXT("MM_Idle")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootSequenceSessionTest, "LiveFoot.Editor.AnalysisSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootSequenceSessionTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (!TestNotNull(TEXT("Session fixture loads"), Sequence)) { return false; }
	FLiveFootSequenceAnalysisOptions Options;
	FLiveFootSequenceAnalysisSession Session;
	TestTrue(TEXT("Session begins"), Session.Begin(Sequence, Options));
	int32 Steps = 0;
	while (!Session.Step(1, 0.0001)) { ++Steps; if (Steps > 10000) { AddError(TEXT("Session did not finish")); return false; } }
	TestTrue(TEXT("Session produces a complete result"), Session.Result.bSuccess && Session.Result.SampleCount > 1);
	TestTrue(TEXT("Session required multiple bounded steps"), Steps > 0);
	TestTrue(TEXT("Session cancellation discards partial result"), Session.Begin(Sequence, Options));
	Session.Step(1, 0.0001);
	Session.Cancel();
	TestFalse(TEXT("Cancelled session is not successful"), Session.Result.bSuccess);
	TestTrue(TEXT("Cancellation has explicit reason"), Session.Result.FailureReason.Contains(TEXT("Cancelled")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLiveFootBatchQueueTest, "LiveFoot.Editor.BatchQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLiveFootBatchQueueTest::RunTest(const FString& Parameters)
{
	const FSoftObjectPath Idle(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	const FSoftObjectPath Land(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land"));
	FLiveFootSequenceAnalysisOptions Options;
	FLiveFootBatchAnalysis Batch;
	FString Error;
	TestTrue(TEXT("Batch accepts distinct assets and removes duplicates"), Batch.Start({Idle, Idle, Land}, Options, Error));
	int32 Ticks = 0;
	while (Batch.IsRunning() && Ticks++ < 10000) { Batch.Tick(64, 0.002); }
	TestFalse(TEXT("Batch finishes within bounded ticks"), Batch.IsRunning());
	TestEqual(TEXT("Duplicate asset is analyzed once"), Batch.GetJobs().Num(), 2);
	TestTrue(TEXT("Each valid asset succeeds or has a structured error"), Batch.GetJobs()[0]->Result.IsValid() || !Batch.GetJobs()[0]->Error.IsEmpty());
	TArray<TSharedPtr<FLiveFootAnalysisJob>> CompletedRows = Batch.GetJobs();
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/LiveFoot-V1.2-BatchReport");
	FString ReportDirectory, ReportError;
	TestTrue(TEXT("Batch report export succeeds"), LiveFootReports::Export(CompletedRows, Directory, ReportDirectory, ReportError));
	FString Summary, Samples, Errors, Intervals;
	TestTrue(TEXT("Summary report is readable"), FFileHelper::LoadFileToString(Summary, *(ReportDirectory / TEXT("LiveFoot_summary.csv"))));
	TestTrue(TEXT("Sample report is readable"), FFileHelper::LoadFileToString(Samples, *(ReportDirectory / TEXT("LiveFoot_samples.csv"))));
	TestTrue(TEXT("Error report is readable"), FFileHelper::LoadFileToString(Errors, *(ReportDirectory / TEXT("LiveFoot_errors.csv"))));
	TestTrue(TEXT("Interval report is readable"), FFileHelper::LoadFileToString(Intervals, *(ReportDirectory / TEXT("LiveFoot_intervals.csv"))));
	TestTrue(TEXT("Summary has reproducibility metadata and ratios"), Summary.Contains(TEXT("config_hash")) && Summary.Contains(TEXT("contact_ratio")));
	TestTrue(TEXT("Reports identify raw Sequence pose"), Samples.Contains(TEXT("Sequence raw pose")));
	FLiveFootBatchAnalysis FailedBatch;
	TestTrue(TEXT("A missing asset can be queued for failure isolation"), FailedBatch.Start({FSoftObjectPath(TEXT("/Game/LiveFootMissing/DoesNotExist.DoesNotExist"))}, Options, Error));
	int32 FailedTicks = 0;
	while (FailedBatch.IsRunning() && FailedTicks++ < 1000) { FailedBatch.Tick(64, 0.002); }
	TestFalse(TEXT("Missing asset reaches a terminal failure state"), FailedBatch.IsRunning());
	FString FailedReportDirectory, FailedReportError;
	TestTrue(TEXT("Failed asset report export succeeds"), LiveFootReports::Export(FailedBatch.GetJobs(), Directory, FailedReportDirectory, FailedReportError));
	FString FailedErrors;
	TestTrue(TEXT("Failed asset is written to errors.csv"), FFileHelper::LoadFileToString(FailedErrors, *(FailedReportDirectory / TEXT("LiveFoot_errors.csv"))) && FailedErrors.Contains(TEXT("FAILED")));
	TArray<TSharedPtr<FLiveFootAnalysisJob>> Filtered;
	FLiveFootBatchAnalysis::FilterAndSort(CompletedRows, TEXT("MM_Idle"), -1, false, ELiveFootSort::Path, false, Filtered);
	TestTrue(TEXT("Asset search filter narrows the report list"), Filtered.Num() == 1 && Filtered[0]->Asset.ToString().Contains(TEXT("MM_Idle")));
	FLiveFootBatchAnalysis::FilterAndSort(CompletedRows, TEXT("missing"), -1, true, ELiveFootSort::Slip, true, Filtered);
	TestEqual(TEXT("Issues-only filter excludes clean successful rows"), Filtered.Num(), 0);
	TestTrue(TEXT("Second run starts with the same configuration"), Batch.Start({Idle}, Options, Error));
	while (Batch.IsRunning() && Ticks++ < 20000) { Batch.Tick(64, 0.002); }
	TestFalse(TEXT("Cached run finishes"), Batch.IsRunning());
	TestEqual(TEXT("Second run has one job"), Batch.GetJobs().Num(), 1);
	TestTrue(TEXT("Second run reports a cache hit"), Batch.GetJobs()[0]->bCacheHit);

	TestTrue(TEXT("Cancellation queue starts"), Batch.Start({Idle, Land}, Options, Error));
	Batch.Tick(1, 0.0001);
	Batch.Cancel();
	TestFalse(TEXT("Cancellation stops queue"), Batch.IsRunning());
	for (const auto& Job : Batch.GetJobs())
	{
		TestTrue(TEXT("Cancelled job has explicit terminal state"), Job->State == ELiveFootJobState::Cancelled || Job->State == ELiveFootJobState::Succeeded || Job->State == ELiveFootJobState::Failed);
	}

	return true;
}

#endif
