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
