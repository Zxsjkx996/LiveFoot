#pragma once

#include "AnimFootDiagnosticTypes.h"

/** Pure game-thread-independent numerical state; contains no UObject references. */
struct FAnimFootHistory
{
	bool bHasPrevious = false;
	bool bHasFilteredSpeed = false;
	FVector PreviousBone = FVector::ZeroVector;
	FVector PreviousSole = FVector::ZeroVector;
	EAnimFootPhase Phase = EAnimFootPhase::Unknown;
	float ContactTimer = 0.f;
	float SwingTimer = 0.f;
	float SlipTimer = 0.f;
	float ClearTimer = 0.f;
	float FilteredSpeed = 0.f;
	bool bSlipping = false;
};

namespace AnimFootDiagnostics
{
	LIVEFOOT_API bool IsFiniteVector(const FVector& V);
	/** Updates an already acquired world-space sample. Raycast/objects are handled by the component. */
	LIVEFOOT_API void Analyze(FAnimFootDebugResult& Result, FAnimFootHistory& History,
		const FAnimFootDiagnosticSettings& Settings, float DeltaTime, bool bAirborne, bool bStaticGround);
	LIVEFOOT_API const TCHAR* IssueText(EAnimFootIssue Issue);
	LIVEFOOT_API const TCHAR* PhaseText(EAnimFootPhase Phase);
}

/**
 * Bounded, UObject-free runtime aggregation. It intentionally keeps only a recent
 * tangent-speed window so long-running PIE sessions cannot grow memory without limit.
 */
struct LIVEFOOT_API FAnimFootAnalysisAccumulator
{
	static constexpr int32 MaxIntervals = 64;
	static constexpr int32 RecentSpeedWindow = 256;

	FAnimFootAnalysisSummary Summary;
	TArray<FAnimFootAnalysisInterval> Intervals;

	void Reset();
	void AddSample(const FAnimFootDebugResult& Result, float DeltaTime, float SampleTime);

private:
	TArray<float> RecentTangentSpeeds;
	double TangentSpeedSum = 0.0;
	int32 ActivePhaseInterval = INDEX_NONE;
	int32 ActiveSlipInterval = INDEX_NONE;

	void AddInterval(const FAnimFootAnalysisInterval& Interval, int32& ActiveIndex);
	void UpdatePhaseInterval(const FAnimFootDebugResult& Result, float StartTime, float EndTime);
	void UpdateSlipInterval(const FAnimFootDebugResult& Result, float StartTime, float EndTime);
	void UpdateP95();
};
