#include "LiveFootBatchAnalysis.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	FString HashBytes(const TArray<uint8>& Bytes)
	{
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash);
		return BytesToHex(Hash, FSHA1::DigestSize);
	}
	int64 BytesUsed(const FLiveFootSequenceAnalysisResult& R)
	{
		return sizeof(R) + R.Samples.GetAllocatedSize() + R.LeftIntervals.GetAllocatedSize()
			+ R.RightIntervals.GetAllocatedSize() + R.AssetPath.GetAllocatedSize() + R.FailureReason.GetAllocatedSize();
	}
	double Metric(const FLiveFootAnalysisJob& Job, ELiveFootSort Sort)
	{
		if (!Job.Result || !Job.Result->bSuccess) { return -1.0; }
		const auto& L = Job.Result->LeftSummary; const auto& R = Job.Result->RightSummary;
		switch (Sort)
		{
		case ELiveFootSort::Slip: return L.SuspectedSlipDuration + R.SuspectedSlipDuration;
		case ELiveFootSort::P95: return FMath::Max(L.P95TangentSpeed, R.P95TangentSpeed);
		case ELiveFootSort::Contact: return (L.ContactDuration + R.ContactDuration) / FMath::Max(0.00001f, L.SampleDuration + R.SampleDuration);
		case ELiveFootSort::Unknown: return (L.UnknownDuration + R.UnknownDuration) / FMath::Max(0.00001f, L.SampleDuration + R.SampleDuration);
		default: return 0.0;
		}
	}
}

FString FLiveFootBatchAnalysis::ConfigText(const FLiveFootSequenceAnalysisOptions& O)
{
	FString Text = FString::Printf(TEXT("bones=%s/%s;hz=%.9g;ground_z=%.9g;normal=(%.17g,%.17g,%.17g);sole_l=(%.17g,%.17g,%.17g);sole_r=(%.17g,%.17g,%.17g);settings="),
		*O.LeftFootBone.ToString(), *O.RightFootBone.ToString(), O.SampleRate, O.GroundHeight,
		O.GroundNormal.X, O.GroundNormal.Y, O.GroundNormal.Z, O.LeftSoleOffset.X, O.LeftSoleOffset.Y, O.LeftSoleOffset.Z,
		O.RightSoleOffset.X, O.RightSoleOffset.Y, O.RightSoleOffset.Z);
	const auto& S = O.DiagnosticSettings;
	const float Values[] = {S.TraceStartOffset, S.TraceLength, S.ContactHeight, S.ContactExitHeight,
		S.ContactVerticalSpeed, S.ContactExitVerticalSpeed, S.ContactConfirmTime, S.SwingConfirmTime,
		S.MaxPenetration, S.MaxGroundSlope, S.SlipSpeed, S.SlipExitSpeed, S.SlipDuration, S.SlipClearTime,
		S.SmoothingTime, S.MinDeltaTime, S.MaxDeltaTime, S.TeleportDistance};
	for (float Value : Values) { Text += FString::Printf(TEXT("%.9g;"), Value); }
	return Text;
}

FString FLiveFootBatchAnalysis::ConfigHash(const FLiveFootSequenceAnalysisOptions& InOptions)
{
	// Serialize values without rounding, padding bytes, or property-layout dependence.
	FLiveFootSequenceAnalysisOptions O = InOptions;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
	FString Version = TEXT("LiveFoot-1.2-algorithm-2|") + FEngineVersion::Current().ToString();
	Writer << Version << O.LeftFootBone << O.RightFootBone << O.LeftSoleOffset << O.RightSoleOffset
		<< O.SampleRate << O.GroundHeight << O.GroundNormal;
	auto& S = O.DiagnosticSettings;
	Writer << S.TraceStartOffset << S.TraceLength << S.ContactHeight << S.ContactExitHeight
		<< S.ContactVerticalSpeed << S.ContactExitVerticalSpeed << S.ContactConfirmTime << S.SwingConfirmTime
		<< S.MaxPenetration << S.MaxGroundSlope << S.SlipSpeed << S.SlipExitSpeed << S.SlipDuration
		<< S.SlipClearTime << S.SmoothingTime << S.MinDeltaTime << S.MaxDeltaTime << S.TeleportDistance;
	return HashBytes(Bytes);
}

FString FLiveFootBatchAnalysis::SourceHash(UAnimSequence* Sequence)
{
	if (!Sequence || !Sequence->GetSkeleton() || !Sequence->GetDataModel()) { return FString(); }
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
	FGuid DataGuid = Sequence->GetDataModel()->GenerateGuid();
	FString Path = Sequence->GetPathName(), SkeletonPath = Sequence->GetSkeleton()->GetPathName();
	int32 Interpolation = static_cast<int32>(Sequence->Interpolation), Additive = static_cast<int32>(Sequence->GetAdditiveAnimType());
	Writer << Path << SkeletonPath << DataGuid << Interpolation << Additive;
	const FReferenceSkeleton& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
	{
		FName Name = Ref.GetBoneName(Index); int32 Parent = Ref.GetParentIndex(Index);
		FTransform Pose = Ref.GetRefBonePose()[Index]; Writer << Name << Parent << Pose;
	}
	return HashBytes(Bytes);
}

FLiveFootBatchAnalysis::~FLiveFootBatchAnalysis() { Cancel(); }

bool FLiveFootBatchAnalysis::Start(const TArray<FSoftObjectPath>& Assets, const FLiveFootSequenceAnalysisOptions& Options, FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	if (bRunning) { OutError = TEXT("Cancel the current queue before starting another"); return false; }
	TArray<FSoftObjectPath> Unique;
	for (const auto& Asset : Assets) { if (!Asset.IsNull()) { Unique.AddUnique(Asset); } }
	if (Unique.IsEmpty() || Unique.Num() > MaxJobs)
	{ OutError = FString::Printf(TEXT("Select 1 to %d distinct assets"), MaxJobs); return false; }
	Unique.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
	Jobs.Reset(); ResultBytes = 0; NextJob = 0;
	for (const auto& Asset : Unique)
	{
		auto Job = MakeShared<FLiveFootAnalysisJob>(); Job->Asset = Asset; Job->Options = Options;
		Job->ConfigHash = ConfigHash(Options); Job->ConfigText = ConfigText(Options); Jobs.Add(Job);
	}
	bRunning = true;
	return true;
}

void FLiveFootBatchAnalysis::ReleaseActive()
{
	Session.Reset(); ActiveAsset.Reset();
	if (LoadHandle) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
	ActiveKey.Reset();
}

void FLiveFootBatchAnalysis::Finish(TSharedPtr<const FLiveFootSequenceAnalysisResult> Result, const FString& Error)
{
	auto& Job = *Jobs[NextJob];
	Job.ElapsedSeconds = FPlatformTime::Seconds() - StartedAt;
	Job.Error = Error;
	if (Result && Result->bSuccess && ResultBytes + BytesUsed(*Result) > MaxResultBytes)
	{ Job.Error = TEXT("Result memory budget reached (128 MiB); use a smaller batch"); Result.Reset(); }
	Job.Result = Result;
	Job.State = Result && Result->bSuccess && Job.Error.IsEmpty() ? ELiveFootJobState::Succeeded : ELiveFootJobState::Failed;
	if (Result) { ResultBytes += BytesUsed(*Result); if (!Result->bSuccess) { Job.Error = Result->FailureReason; } }
	if (Job.State == ELiveFootJobState::Succeeded && !Job.bCacheHit)
	{
		const int64 Size = BytesUsed(*Result);
		while (CacheOrder.Num() && CacheBytes + Size > MaxCacheBytes)
		{
			if (const auto* Old = Cache.Find(CacheOrder[0])) { CacheBytes -= BytesUsed(**Old); }
			Cache.Remove(CacheOrder[0]); CacheOrder.RemoveAt(0);
		}
		if (Size <= MaxCacheBytes)
		{
			if (const auto* Old = Cache.Find(ActiveKey)) { CacheBytes -= BytesUsed(**Old); CacheOrder.Remove(ActiveKey); }
			Cache.Add(ActiveKey, Result); CacheOrder.Add(ActiveKey); CacheBytes += Size;
		}
	}
	ReleaseActive(); ++NextJob;
	if (NextJob >= Jobs.Num()) { bRunning = false; }
}

void FLiveFootBatchAnalysis::Tick(int32 SampleBudget, double TimeBudgetSeconds)
{
	check(IsInGameThread());
	if (!bRunning) { return; }
	auto& Job = *Jobs[NextJob];
	if (Job.State == ELiveFootJobState::Pending)
	{
		StartedAt = FPlatformTime::Seconds(); Job.State = ELiveFootJobState::Loading;
		const FAssetData AssetData = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
			.Get().GetAssetByObjectPath(Job.Asset, true);
		if (!AssetData.IsValid())
		{
			Finish(nullptr, TEXT("Asset Registry has no record for this asset path"));
			return;
		}
		LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Job.Asset);
		if (!LoadHandle) { Finish(nullptr, TEXT("Asset load request failed")); }
		return;
	}
	if (Job.State == ELiveFootJobState::Loading)
	{
		if (!LoadHandle->HasLoadCompleted()) { return; }
		ActiveAsset.Reset(Cast<UAnimSequence>(LoadHandle->GetLoadedAsset()));
		if (!ActiveAsset) { Finish(nullptr, TEXT("Asset is unavailable or is not an Animation Sequence")); return; }
		Job.SourceHash = SourceHash(ActiveAsset.Get()); ActiveKey = Job.Asset.ToString() + Job.SourceHash + Job.ConfigHash;
		if (const auto* Cached = Cache.Find(ActiveKey)) { Job.bCacheHit = true; Finish(*Cached); return; }
		const double Estimate = FMath::CeilToDouble(ActiveAsset->GetPlayLength() * static_cast<double>(Job.Options.SampleRate)) + 1.0;
		if (!FMath::IsFinite(Estimate) || Estimate > MaxSamplesPerAsset)
		{ Finish(nullptr, TEXT("Per-asset sample limit reached (50000); reduce sample rate")); return; }
		Session = MakeUnique<FLiveFootSequenceAnalysisSession>();
		if (!Session->Begin(ActiveAsset.Get(), Job.Options))
		{ Finish(MakeShared<FLiveFootSequenceAnalysisResult>(MoveTemp(Session->Result))); return; }
		Job.State = ELiveFootJobState::Running;
		return;
	}
	if (Session->Step(SampleBudget, TimeBudgetSeconds))
	{
		if (SourceHash(ActiveAsset.Get()) != Job.SourceHash)
		{ Finish(nullptr, TEXT("Animation or Skeleton changed during analysis; rerun required")); return; }
		Finish(MakeShared<FLiveFootSequenceAnalysisResult>(MoveTemp(Session->Result)));
	}
}

void FLiveFootBatchAnalysis::Cancel()
{
	if (!bRunning) { return; }
	if (Session) { Session->Cancel(); }
	ReleaseActive();
	for (int32 Index = NextJob; Index < Jobs.Num(); ++Index)
	{ Jobs[Index]->State = ELiveFootJobState::Cancelled; Jobs[Index]->Error = TEXT("Cancelled; partial samples discarded"); }
	bRunning = false;
}

void FLiveFootBatchAnalysis::ClearCache() { Cache.Reset(); CacheOrder.Reset(); CacheBytes = 0; }
int32 FLiveFootBatchAnalysis::Completed() const { return bRunning ? NextJob : Jobs.Num(); }
float FLiveFootBatchAnalysis::Progress() const
{
	return Jobs.Num() ? (bRunning ? (NextJob + (Session ? Session->GetProgress() : 0.f)) / Jobs.Num() : 1.f) : 0.f;
}

const TCHAR* FLiveFootBatchAnalysis::StateText(ELiveFootJobState State)
{
	switch (State)
	{
	case ELiveFootJobState::Pending: return TEXT("Pending"); case ELiveFootJobState::Loading: return TEXT("Loading");
	case ELiveFootJobState::Running: return TEXT("Running"); case ELiveFootJobState::Succeeded: return TEXT("OK");
	case ELiveFootJobState::Failed: return TEXT("FAILED"); default: return TEXT("Cancelled");
	}
}

void FLiveFootBatchAnalysis::FilterAndSort(const TArray<TSharedPtr<FLiveFootAnalysisJob>>& InJobs, const FString& Search,
	int32 StateFilter, bool bIssuesOnly, ELiveFootSort Sort, bool bDescending, TArray<TSharedPtr<FLiveFootAnalysisJob>>& OutJobs)
{
	OutJobs.Reset();
	for (const auto& Job : InJobs)
	{
		if (!Search.IsEmpty() && !Job->Asset.ToString().Contains(Search) && !Job->Error.Contains(Search)) { continue; }
		if (StateFilter >= 0 && static_cast<int32>(Job->State) != StateFilter) { continue; }
		if (bIssuesOnly && Job->State != ELiveFootJobState::Failed && Metric(*Job, ELiveFootSort::Slip) <= 0.0
			&& Metric(*Job, ELiveFootSort::Unknown) <= 0.0) { continue; }
		OutJobs.Add(Job);
	}
	OutJobs.Sort([Sort, bDescending](const auto& A, const auto& B)
	{
		const double AV = Metric(*A, Sort), BV = Metric(*B, Sort);
		if (Sort == ELiveFootSort::Path) { return bDescending ? A->Asset.ToString() > B->Asset.ToString() : A->Asset.ToString() < B->Asset.ToString(); }
		if (AV == BV) { return A->Asset.ToString() < B->Asset.ToString(); }
		return bDescending ? AV > BV : AV < BV;
	});
}
