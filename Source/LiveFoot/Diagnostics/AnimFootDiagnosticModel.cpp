#include "AnimFootDiagnosticModel.h"
#include "Algo/Sort.h"

bool FAnimFootDiagnosticSettings::IsValid() const
{
	const float Values[] = {TraceStartOffset, TraceLength, ContactHeight, ContactExitHeight,
		ContactVerticalSpeed, ContactExitVerticalSpeed, ContactConfirmTime, SwingConfirmTime,
		MaxPenetration, MaxGroundSlope, SlipSpeed, SlipExitSpeed, SlipDuration, SlipClearTime,
		SmoothingTime, MinDeltaTime, MaxDeltaTime, TeleportDistance};
	for (float Value : Values) { if (!FMath::IsFinite(Value) || Value < 0.f) { return false; } }
	return TraceLength > TraceStartOffset && ContactExitHeight > ContactHeight
		&& ContactExitVerticalSpeed >= ContactVerticalSpeed && MaxGroundSlope < 90.f
		&& SlipSpeed > SlipExitSpeed && MinDeltaTime > 0.f && MaxDeltaTime > MinDeltaTime
		&& TeleportDistance > 0.f;
}

bool FAnimFootDiagnosticSettings::operator==(const FAnimFootDiagnosticSettings& O) const
{
	return TraceStartOffset == O.TraceStartOffset && TraceLength == O.TraceLength
		&& ContactHeight == O.ContactHeight && ContactExitHeight == O.ContactExitHeight
		&& ContactVerticalSpeed == O.ContactVerticalSpeed && ContactExitVerticalSpeed == O.ContactExitVerticalSpeed
		&& ContactConfirmTime == O.ContactConfirmTime && SwingConfirmTime == O.SwingConfirmTime
		&& MaxPenetration == O.MaxPenetration && MaxGroundSlope == O.MaxGroundSlope
		&& SlipSpeed == O.SlipSpeed && SlipExitSpeed == O.SlipExitSpeed
		&& SlipDuration == O.SlipDuration && SlipClearTime == O.SlipClearTime
		&& SmoothingTime == O.SmoothingTime && MinDeltaTime == O.MinDeltaTime
		&& MaxDeltaTime == O.MaxDeltaTime && TeleportDistance == O.TeleportDistance;
}

bool AnimFootDiagnostics::IsFiniteVector(const FVector& V)
{
	return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}

void AnimFootDiagnostics::Analyze(FAnimFootDebugResult& R, FAnimFootHistory& H,
	const FAnimFootDiagnosticSettings& S, float Dt, bool bAirborne, bool bStaticGround)
{
	if (!S.IsValid() || !R.bValidPosition || !IsFiniteVector(R.BonePosition) || !IsFiniteVector(R.SolePosition))
	{
		H = FAnimFootHistory();
		R.Phase = EAnimFootPhase::Unknown;
		R.Issue = EAnimFootIssue::InvalidParameters;
		return;
	}
	if (!FMath::IsFinite(Dt) || Dt < S.MinDeltaTime || Dt > S.MaxDeltaTime)
	{
		H = FAnimFootHistory();
		R.Phase = EAnimFootPhase::Unknown;
		R.Issue = EAnimFootIssue::InvalidDeltaTime;
		return;
	}
	const bool bTeleport = H.bHasPrevious && ((R.BonePosition - H.PreviousBone).Size() > S.TeleportDistance
		|| (R.SolePosition - H.PreviousSole).Size() > S.TeleportDistance);
	if (!H.bHasPrevious || bTeleport)
	{
		H = FAnimFootHistory();
		H.bHasPrevious = true;
		H.PreviousBone = R.BonePosition;
		H.PreviousSole = R.SolePosition;
		H.Phase = EAnimFootPhase::Swing;
		R.Phase = H.Phase;
		R.Issue = bTeleport ? EAnimFootIssue::Teleport : EAnimFootIssue::Warmup;
		return;
	}
	R.Velocity = (R.BonePosition - H.PreviousBone) / Dt;
	const FVector SoleVelocity = (R.SolePosition - H.PreviousSole) / Dt;
	H.PreviousBone = R.BonePosition;
	H.PreviousSole = R.SolePosition;
	if (!IsFiniteVector(R.Velocity) || !IsFiniteVector(SoleVelocity))
	{
		H = FAnimFootHistory();
		R.Phase = EAnimFootPhase::Unknown;
		R.Issue = EAnimFootIssue::InvalidDeltaTime;
		return;
	}
	R.bValidVelocity = true;
	R.Speed = R.Velocity.Size();
	R.VerticalSpeed = SoleVelocity.Z; // World Z vertical speed, cm/s (not tangent speed).
	if (R.bGroundHit && (!FMath::IsFinite(R.Height) || R.Height < -S.MaxPenetration))
	{
		H = FAnimFootHistory();
		R.Phase = EAnimFootPhase::Unknown;
		R.Issue = EAnimFootIssue::Penetration;
		return;
	}
	if (bAirborne)
	{
		H.Phase = EAnimFootPhase::Swing;
		H.ContactTimer = H.SwingTimer = 0.f;
	}
	else if (H.Phase != EAnimFootPhase::Contact)
	{
		const bool bNearAndSlowVertical = R.bValidGround && R.Height <= S.ContactHeight
			&& FMath::Abs(R.VerticalSpeed) <= S.ContactVerticalSpeed;
		H.ContactTimer = bNearAndSlowVertical ? H.ContactTimer + Dt : 0.f;
		if (bNearAndSlowVertical && H.ContactTimer + KINDA_SMALL_NUMBER >= S.ContactConfirmTime)
		{
			H.Phase = EAnimFootPhase::Contact;
			H.ContactTimer = H.SwingTimer = 0.f;
		}
		else { H.Phase = EAnimFootPhase::Swing; }
	}
	else
	{
		const bool bLeaving = !R.bValidGround || R.Height > S.ContactExitHeight
			|| FMath::Abs(R.VerticalSpeed) > S.ContactExitVerticalSpeed;
		H.SwingTimer = bLeaving ? H.SwingTimer + Dt : 0.f;
		if (bLeaving && H.SwingTimer + KINDA_SMALL_NUMBER >= S.SwingConfirmTime)
		{
			H.Phase = EAnimFootPhase::Swing;
			H.SwingTimer = H.ContactTimer = 0.f;
		}
	}
	R.Phase = H.Phase;
	if (R.bValidGround)
	{
		// Static world ground: do NOT subtract the moving character root velocity.
		const FVector Tangent = SoleVelocity - FVector::DotProduct(SoleVelocity, R.GroundNormal) * R.GroundNormal;
		R.RawTangentSpeed = Tangent.Size();
		const float Alpha = S.SmoothingTime > 0.f ? 1.f - FMath::Exp(-Dt / S.SmoothingTime) : 1.f;
		H.FilteredSpeed = H.bHasFilteredSpeed ? FMath::Lerp(H.FilteredSpeed, R.RawTangentSpeed, Alpha) : R.RawTangentSpeed;
		H.bHasFilteredSpeed = true;
		R.TangentSpeed = H.FilteredSpeed;
	}
	else { H.bHasFilteredSpeed = false; }
	// Even during phase-exit debounce, fresh geometry and vertical speed must support contact.
	R.bSlipEvaluated = H.Phase == EAnimFootPhase::Contact && R.bValidGround && bStaticGround && !bAirborne
		&& R.Height <= S.ContactExitHeight && FMath::Abs(R.VerticalSpeed) <= S.ContactExitVerticalSpeed;
	if (!R.bSlipEvaluated)
	{
		H.SlipTimer = H.ClearTimer = 0.f;
		H.bSlipping = false;
	}
	else if (!H.bSlipping)
	{
		H.SlipTimer = R.TangentSpeed > S.SlipSpeed ? H.SlipTimer + Dt : 0.f;
		H.bSlipping = R.TangentSpeed > S.SlipSpeed && H.SlipTimer + KINDA_SMALL_NUMBER >= S.SlipDuration;
	}
	else
	{
		H.ClearTimer = R.TangentSpeed < S.SlipExitSpeed ? H.ClearTimer + Dt : 0.f;
		if (R.TangentSpeed < S.SlipExitSpeed && H.ClearTimer + KINDA_SMALL_NUMBER >= S.SlipClearTime)
		{
			H.bSlipping = false;
			H.SlipTimer = H.ClearTimer = 0.f;
		}
	}
	R.bSuspectedSlip = H.bSlipping;
	R.Issue = !R.bGroundHit ? EAnimFootIssue::NoGround : !R.bValidGround ? EAnimFootIssue::UnsupportedSlope
		: !bStaticGround ? EAnimFootIssue::MovingGround : EAnimFootIssue::None;
}

const TCHAR* AnimFootDiagnostics::PhaseText(EAnimFootPhase P)
{
	switch (P) { case EAnimFootPhase::Contact: return TEXT("CONTACT"); case EAnimFootPhase::Swing: return TEXT("SWING"); default: return TEXT("UNKNOWN"); }
}

const TCHAR* AnimFootDiagnostics::IssueText(EAnimFootIssue I)
{
	switch (I)
	{
	case EAnimFootIssue::None: return TEXT("OK (heuristic)");
	case EAnimFootIssue::Disabled: return TEXT("disabled");
	case EAnimFootIssue::MissingMesh: return TEXT("missing character Mesh");
	case EAnimFootIssue::MissingAnimation: return TEXT("missing animation instance");
	case EAnimFootIssue::MissingBone: return TEXT("missing bone: configure LeftFootBone/RightFootBone");
	case EAnimFootIssue::PoseNotReady: return TEXT("pose not refreshed/ready");
	case EAnimFootIssue::InvalidParameters: return TEXT("invalid Settings/sole offset");
	case EAnimFootIssue::InvalidDeltaTime: return TEXT("invalid dt: history reset");
	case EAnimFootIssue::Warmup: return TEXT("first sample: velocity N/A");
	case EAnimFootIssue::Teleport: return TEXT("teleport: history reset");
	case EAnimFootIssue::NoGround: return TEXT("no ground hit");
	case EAnimFootIssue::UnsupportedSlope: return TEXT("unsupported ground normal/slope");
	case EAnimFootIssue::Penetration: return TEXT("deep penetration: check sole offset");
	case EAnimFootIssue::MovingGround: return TEXT("moving/non-static ground: slip N/A");
	default: return TEXT("unknown");
	}
}

void FAnimFootAnalysisAccumulator::Reset()
{
	Summary = FAnimFootAnalysisSummary();
	Intervals.Reset();
	RecentTangentSpeeds.Reset();
	TangentSpeedSum = 0.0;
	ActivePhaseInterval = INDEX_NONE;
	ActiveSlipInterval = INDEX_NONE;
}

void FAnimFootAnalysisAccumulator::AddInterval(const FAnimFootAnalysisInterval& Interval, int32& ActiveIndex)
{
	if (Intervals.Num() >= MaxIntervals)
	{
		Intervals.RemoveAt(0, 1, EAllowShrinking::No);
		if (ActivePhaseInterval != INDEX_NONE) { --ActivePhaseInterval; }
		if (ActiveSlipInterval != INDEX_NONE) { --ActiveSlipInterval; }
		if (ActivePhaseInterval < 0) { ActivePhaseInterval = INDEX_NONE; }
		if (ActiveSlipInterval < 0) { ActiveSlipInterval = INDEX_NONE; }
	}
	ActiveIndex = Intervals.Add(Interval);
}

void FAnimFootAnalysisAccumulator::UpdatePhaseInterval(const FAnimFootDebugResult& Result, float StartTime, float EndTime)
{
	if (ActivePhaseInterval != INDEX_NONE && Intervals.IsValidIndex(ActivePhaseInterval)
		&& Intervals[ActivePhaseInterval].Kind == EAnimFootIntervalKind::Phase
		&& Intervals[ActivePhaseInterval].Phase == Result.Phase)
	{
		FAnimFootAnalysisInterval& Interval = Intervals[ActivePhaseInterval];
		Interval.EndTime = FMath::Max(Interval.EndTime, EndTime);
		Interval.Duration = FMath::Max(0.f, Interval.EndTime - Interval.StartTime);
		Interval.Issue = Result.Issue;
		Interval.MaxTangentSpeed = FMath::Max(Interval.MaxTangentSpeed, Result.bValidGround && Result.bValidVelocity ? Result.TangentSpeed : 0.f);
		return;
	}

	FAnimFootAnalysisInterval Interval;
	Interval.Kind = EAnimFootIntervalKind::Phase;
	Interval.Phase = Result.Phase;
	Interval.Issue = Result.Issue;
	Interval.StartTime = StartTime;
	Interval.EndTime = EndTime;
	Interval.Duration = FMath::Max(0.f, EndTime - StartTime);
	Interval.MaxTangentSpeed = Result.bValidGround && Result.bValidVelocity ? FMath::Max(0.f, Result.TangentSpeed) : 0.f;
	AddInterval(Interval, ActivePhaseInterval);
}

void FAnimFootAnalysisAccumulator::UpdateSlipInterval(const FAnimFootDebugResult& Result, float StartTime, float EndTime)
{
	if (!Result.bSuspectedSlip)
	{
		ActiveSlipInterval = INDEX_NONE;
		return;
	}

	if (ActiveSlipInterval != INDEX_NONE && Intervals.IsValidIndex(ActiveSlipInterval)
		&& Intervals[ActiveSlipInterval].Kind == EAnimFootIntervalKind::SuspectedSlip)
	{
		FAnimFootAnalysisInterval& Interval = Intervals[ActiveSlipInterval];
		Interval.EndTime = FMath::Max(Interval.EndTime, EndTime);
		Interval.Duration = FMath::Max(0.f, Interval.EndTime - Interval.StartTime);
		Interval.Issue = Result.Issue;
		Interval.MaxTangentSpeed = FMath::Max(Interval.MaxTangentSpeed, Result.bValidVelocity ? Result.TangentSpeed : 0.f);
		return;
	}

	FAnimFootAnalysisInterval Interval;
	Interval.Kind = EAnimFootIntervalKind::SuspectedSlip;
	Interval.Phase = Result.Phase;
	Interval.Issue = Result.Issue;
	Interval.bSuspectedSlip = true;
	Interval.StartTime = StartTime;
	Interval.EndTime = EndTime;
	Interval.Duration = FMath::Max(0.f, EndTime - StartTime);
	Interval.MaxTangentSpeed = Result.bValidVelocity ? FMath::Max(0.f, Result.TangentSpeed) : 0.f;
	AddInterval(Interval, ActiveSlipInterval);
}

void FAnimFootAnalysisAccumulator::UpdateP95()
{
	if (RecentTangentSpeeds.Num() == 0)
	{
		Summary.P95TangentSpeed = 0.f;
		Summary.bP95UsesRecentWindow = false;
		return;
	}
	TArray<float> Sorted = RecentTangentSpeeds;
	Sorted.Sort();
	const int32 Index = FMath::Clamp(FMath::CeilToInt(static_cast<float>(Sorted.Num()) * 0.95f) - 1, 0, Sorted.Num() - 1);
	Summary.P95TangentSpeed = Sorted[Index];
	Summary.bP95UsesRecentWindow = Summary.ValidVelocitySamples > RecentSpeedWindow;
}

void FAnimFootAnalysisAccumulator::AddSample(const FAnimFootDebugResult& Result, float DeltaTime, float SampleTime)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f || !FMath::IsFinite(SampleTime)
		|| !Result.bValidPosition || !AnimFootDiagnostics::IsFiniteVector(Result.SolePosition))
	{
		return;
	}

	const float StartTime = FMath::Max(0.f, SampleTime - DeltaTime);
	Summary.SampleCount++;
	Summary.ValidPositionSamples++;
	Summary.SampleDuration += DeltaTime;
	Summary.LastPhase = Result.Phase;
	Summary.LastIssue = Result.Issue;

	if (Result.bValidVelocity && FMath::IsFinite(Result.TangentSpeed) && Result.TangentSpeed >= 0.f)
	{
		Summary.ValidVelocitySamples++;
		RecentTangentSpeeds.Add(Result.TangentSpeed);
		TangentSpeedSum += Result.TangentSpeed;
		if (RecentTangentSpeeds.Num() > RecentSpeedWindow)
		{
			RecentTangentSpeeds.RemoveAt(0, 1, EAllowShrinking::No);
		}
		Summary.bHasTangentSpeed = true;
		Summary.MeanTangentSpeed = static_cast<float>(TangentSpeedSum / FMath::Max(1, Summary.ValidVelocitySamples));
		Summary.MaxTangentSpeed = FMath::Max(Summary.MaxTangentSpeed, Result.TangentSpeed);
	}
	else if (Summary.ValidVelocitySamples == 0)
	{
		Summary.MeanTangentSpeed = 0.f;
	}

	switch (Result.Phase)
	{
	case EAnimFootPhase::Contact: ++Summary.ContactSamples; Summary.ContactDuration += DeltaTime; break;
	case EAnimFootPhase::Swing: ++Summary.SwingSamples; Summary.SwingDuration += DeltaTime; break;
	default: ++Summary.UnknownSamples; Summary.UnknownDuration += DeltaTime; break;
	}
	if (Result.bGroundHit && Result.bValidGround) { ++Summary.GroundSamples; }
	if (Result.bSuspectedSlip) { ++Summary.SuspectedSlipSamples; Summary.SuspectedSlipDuration += DeltaTime; }

	UpdatePhaseInterval(Result, StartTime, SampleTime);
	UpdateSlipInterval(Result, StartTime, SampleTime);
	UpdateP95();
}
