#pragma once

#include "CoreMinimal.h"
#include "LiveFootSequenceAnalysis.h"

struct FStreamableHandle;
enum class ELiveFootJobState : uint8 { Pending, Loading, Running, Succeeded, Failed, Cancelled };
enum class ELiveFootSort : uint8 { Path, Slip, P95, Contact, Unknown };

struct FLiveFootAnalysisJob
{
	FSoftObjectPath Asset;
	ELiveFootJobState State = ELiveFootJobState::Pending;
	FLiveFootSequenceAnalysisOptions Options;
	TSharedPtr<const FLiveFootSequenceAnalysisResult> Result;
	FString ConfigHash, ConfigText, SourceHash, Error;
	bool bCacheHit = false;
	double ElapsedSeconds = 0.0;
};

/** Main-thread queue. Only the active asset is retained; completed rows/cache contain values. */
class FLiveFootBatchAnalysis
{
public:
	static constexpr int32 MaxJobs = 4096;
	static constexpr int32 MaxSamplesPerAsset = 50000;
	static constexpr int64 MaxResultBytes = 128ll * 1024 * 1024;
	static constexpr int64 MaxCacheBytes = 32ll * 1024 * 1024;
	~FLiveFootBatchAnalysis();
	bool Start(const TArray<FSoftObjectPath>& Assets, const FLiveFootSequenceAnalysisOptions& Options, FString& OutError);
	void Tick(int32 SampleBudget = 32, double TimeBudgetSeconds = 0.004);
	void Cancel();
	void ClearCache();
	bool IsRunning() const { return bRunning; }
	float Progress() const;
	int32 Completed() const;
	int64 RetainedBytes() const { return ResultBytes; }
	int64 CachedBytes() const { return CacheBytes; }
	int32 CachedCount() const { return Cache.Num(); }
	const TArray<TSharedPtr<FLiveFootAnalysisJob>>& GetJobs() const { return Jobs; }
	static FString ConfigText(const FLiveFootSequenceAnalysisOptions& Options);
	static FString ConfigHash(const FLiveFootSequenceAnalysisOptions& Options);
	static FString SourceHash(UAnimSequence* Sequence);
	static const TCHAR* StateText(ELiveFootJobState State);
	static void FilterAndSort(const TArray<TSharedPtr<FLiveFootAnalysisJob>>& InJobs,
		const FString& Search, int32 StateFilter, bool bIssuesOnly, ELiveFootSort Sort, bool bDescending,
		TArray<TSharedPtr<FLiveFootAnalysisJob>>& OutJobs);
private:
	void Finish(TSharedPtr<const FLiveFootSequenceAnalysisResult> Result, const FString& Error = FString());
	void ReleaseActive();
	TArray<TSharedPtr<FLiveFootAnalysisJob>> Jobs;
	TMap<FString, TSharedPtr<const FLiveFootSequenceAnalysisResult>> Cache;
	TArray<FString> CacheOrder;
	TStrongObjectPtr<UAnimSequence> ActiveAsset;
	TUniquePtr<FLiveFootSequenceAnalysisSession> Session;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FString ActiveKey;
	int32 NextJob = 0;
	int64 CacheBytes = 0, ResultBytes = 0;
	bool bRunning = false;
	double StartedAt = 0.0;
};
