#pragma once

#include "CoreMinimal.h"
#include "AnimFootDiagnosticTypes.generated.h"

UENUM(BlueprintType)
enum class EAnimFootPhase : uint8 { Unknown, Contact, Swing };

UENUM(BlueprintType)
enum class EAnimFootIssue : uint8
{
	None, Disabled, MissingMesh, MissingAnimation, MissingBone, PoseNotReady,
	InvalidParameters, InvalidDeltaTime, Warmup, Teleport, NoGround, UnsupportedSlope,
	Penetration, MovingGround
};

UENUM(BlueprintType)
enum class EAnimFootIntervalKind : uint8
{
	Phase,
	SuspectedSlip
};

/** Distances are cm (UE world units), speeds cm/s, times seconds, slope degrees. */
USTRUCT(BlueprintType)
struct LIVEFOOT_API FAnimFootDiagnosticSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(ClampMin="0", Units="cm"))
	float TraceStartOffset = 20.f;
	/** Total downward ray length, measured from the elevated start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trace", meta=(ClampMin="1", Units="cm"))
	float TraceLength = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="cm"))
	float ContactHeight = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="cm"))
	float ContactExitHeight = 23.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="cm/s"))
	float ContactVerticalSpeed = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="cm/s"))
	float ContactExitVerticalSpeed = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="s"))
	float ContactConfirmTime = 0.04f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="s"))
	float SwingConfirmTime = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", Units="cm"))
	float MaxPenetration = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Contact", meta=(ClampMin="0", ClampMax="89", Units="deg"))
	float MaxGroundSlope = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slip", meta=(ClampMin="0", Units="cm/s"))
	float SlipSpeed = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slip", meta=(ClampMin="0", Units="cm/s"))
	float SlipExitSpeed = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slip", meta=(ClampMin="0", Units="s"))
	float SlipDuration = 0.10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slip", meta=(ClampMin="0", Units="s"))
	float SlipClearTime = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling", meta=(ClampMin="0", Units="s"))
	float SmoothingTime = 0.04f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling", meta=(ClampMin="0.000001", Units="s"))
	float MinDeltaTime = 0.00001f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling", meta=(ClampMin="0.001", Units="s"))
	float MaxDeltaTime = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling", meta=(ClampMin="1", Units="cm"))
	float TeleportDistance = 100.f;

	bool IsValid() const;
	bool operator==(const FAnimFootDiagnosticSettings& Other) const;
};

/** N/A flags distinguish unavailable measurements from actual zero values. */
USTRUCT(BlueprintType)
struct LIVEFOOT_API FAnimFootDebugResult
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FName Bone = NAME_None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") EAnimFootPhase Phase = EAnimFootPhase::Unknown;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") EAnimFootIssue Issue = EAnimFootIssue::Warmup;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bValidPosition = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bValidVelocity = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bGroundHit = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bValidGround = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bSlipEvaluated = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") bool bSuspectedSlip = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FVector BonePosition = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FVector SolePosition = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FVector Velocity = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FVector GroundPoint = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot") FVector GroundNormal = FVector::UpVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot", meta=(Units="cm")) float Height = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot", meta=(Units="cm/s")) float Speed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot", meta=(Units="cm/s")) float VerticalSpeed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot", meta=(Units="cm/s")) float RawTangentSpeed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot", meta=(Units="cm/s")) float TangentSpeed = 0.f;
};

/** A bounded runtime interval. Times are seconds in the component's continuous sample timeline. */
USTRUCT(BlueprintType)
struct LIVEFOOT_API FAnimFootAnalysisInterval
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") EAnimFootIntervalKind Kind = EAnimFootIntervalKind::Phase;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") EAnimFootPhase Phase = EAnimFootPhase::Unknown;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") EAnimFootIssue Issue = EAnimFootIssue::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") bool bSuspectedSlip = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float StartTime = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float EndTime = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float Duration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="cm/s")) float MaxTangentSpeed = 0.f;
};

/** Bounded, incrementally maintained metrics for one foot. P95 uses a recent sample window. */
USTRUCT(BlueprintType)
struct LIVEFOOT_API FAnimFootAnalysisSummary
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 SampleCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 ValidPositionSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 ValidVelocitySamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 ContactSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 SwingSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 UnknownSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 GroundSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") int32 SuspectedSlipSamples = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float SampleDuration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float ContactDuration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float SwingDuration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float UnknownDuration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="s")) float SuspectedSlipDuration = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="cm/s")) float MeanTangentSpeed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="cm/s")) float MaxTangentSpeed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis", meta=(Units="cm/s")) float P95TangentSpeed = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") bool bHasTangentSpeed = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") bool bP95UsesRecentWindow = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") EAnimFootPhase LastPhase = EAnimFootPhase::Unknown;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LiveFoot|Analysis") EAnimFootIssue LastIssue = EAnimFootIssue::Warmup;
};
