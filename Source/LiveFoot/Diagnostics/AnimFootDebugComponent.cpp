#include "AnimFootDebugComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogLiveFoot, Log, All);

namespace
{
	bool GFootEnabled = true;
	bool GFootDraw = true;
	bool GFootFreezePose = false;
	FAutoConsoleVariableRef CVarEnabled(TEXT("LiveFoot.Enabled"), GFootEnabled, TEXT("0 disables all foot diagnostics and releases temporary mesh settings."));
	FAutoConsoleVariableRef CVarDraw(TEXT("LiveFoot.Draw"), GFootDraw, TEXT("0 hides foot geometry and screen data; analysis continues."));
	FAutoConsoleVariableRef CVarFreeze(TEXT("LiveFoot.FreezePose"), GFootFreezePose, TEXT("TEST ONLY: 1 temporarily pauses mesh animation to demonstrate sliding while moving. 0 restores."), ECVF_Cheat);
	uint64 NextScreenKey = 0x4C460000ULL;

	FName FindFootBone(USkeletalMeshComponent* Mesh, bool bLeft)
	{
		// Prefer anatomical feet; explicitly exclude IK/virtual/toe/twist controls.
		const TCHAR* CandidatesLeft[] = {TEXT("foot_l"), TEXT("Foot_L"), TEXT("LeftFoot"), TEXT("foot_left"), TEXT("left_foot"), TEXT("l_foot"), TEXT("LeftAnkle")};
		const TCHAR* CandidatesRight[] = {TEXT("foot_r"), TEXT("Foot_R"), TEXT("RightFoot"), TEXT("foot_right"), TEXT("right_foot"), TEXT("r_foot"), TEXT("RightAnkle")};
		for (int32 I = 0; I < UE_ARRAY_COUNT(CandidatesLeft); ++I)
		{
			const FName Candidate(bLeft ? CandidatesLeft[I] : CandidatesRight[I]);
			if (Mesh->GetBoneIndex(Candidate) != INDEX_NONE) { return Candidate; }
		}
		TArray<FName> Names;
		Mesh->GetBoneNames(Names);
		for (FName Name : Names)
		{
			FString Text = Name.ToString().ToLower();
			if (Text.Contains(TEXT("ik")) || Text.Contains(TEXT("twist")) || Text.StartsWith(TEXT("vb"))) { continue; }
			// Common namespaces, e.g. mixamorig:LeftFoot. No fuzzy left/right guessing.
			int32 Colon = INDEX_NONE;
			if (Text.FindLastChar(TEXT(':'), Colon)) { Text.RightChopInline(Colon + 1); }
			Text.ReplaceInline(TEXT("_"), TEXT("")); Text.ReplaceInline(TEXT("."), TEXT("")); Text.ReplaceInline(TEXT("-"), TEXT(""));
			if (Text == (bLeft ? TEXT("leftfoot") : TEXT("rightfoot")) || Text == (bLeft ? TEXT("footl") : TEXT("footr"))) { return Name; }
		}
		return NAME_None;
	}
}

UAnimFootDebugComponent::UAnimFootDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

void UAnimFootDebugComponent::BeginPlay()
{
	Super::BeginPlay();
	MessageKeys[0] = NextScreenKey++; MessageKeys[1] = NextScreenKey++;
}

void UAnimFootDebugComponent::OnPoseFinalized()
{
	check(IsInGameThread());
	bFreshPose = true;
}

void UAnimFootDebugComponent::BindMesh(USkeletalMeshComponent* Mesh)
{
	ReleaseMesh();
	BoundMesh = Mesh;
	BoundAsset = Mesh->GetSkeletalMeshAsset();
	MeshLabel = GetNameSafe(Mesh->GetSkeletalMeshAsset());
	SavedVisibility = Mesh->VisibilityBasedAnimTickOption;
	bSavedUpdateRate = Mesh->bEnableUpdateRateOptimizations;
	bSavedPause = Mesh->bPauseAnims;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	AddTickPrerequisiteComponent(Mesh);
	PoseDelegate = Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &UAnimFootDebugComponent::OnPoseFinalized));
	ResetSamples();
	ResolveBones();
	CachedSettings = Settings; CachedChannel = GroundTraceChannel;
	bCachedAutoFind = bAutoFindFootBones; bSettingsCached = true;
	UE_LOG(LogLiveFoot, Display, TEXT("Owner=%s Class=%s Mesh=%s Anim=%s Left=%s Right=%s; height cm, speeds cm/s; diagnostic is heuristic."),
		*GetNameSafe(GetOwner()), *GetNameSafe(GetOwner()->GetClass()), *MeshLabel,
		*GetNameSafe(Mesh->GetAnimInstance()), *Feet[0].ResolvedBone.ToString(), *Feet[1].ResolvedBone.ToString());
}

void UAnimFootDebugComponent::ReleaseMesh()
{
	if (USkeletalMeshComponent* Mesh = BoundMesh.Get())
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(PoseDelegate);
		RemoveTickPrerequisiteComponent(Mesh);
		Mesh->VisibilityBasedAnimTickOption = SavedVisibility;
		Mesh->bEnableUpdateRateOptimizations = bSavedUpdateRate;
		if (bTestFrozen) { Mesh->bPauseAnims = bSavedPause; }
	}
	BoundMesh.Reset(); BoundAsset.Reset(); PoseDelegate.Reset();
	bFreshPose = bTestFrozen = false;
}

void UAnimFootDebugComponent::ResetSamples()
{
	for (FFootRuntime& F : Feet)
	{
		F.History = FAnimFootHistory(); F.Analysis.Reset(); F.AnalysisTime = 0.f;
		F.Ground.Reset(); F.bGroundMoved = false;
	}
	LeftFoot = FAnimFootDebugResult(); RightFoot = FAnimFootDebugResult();
	LeftSummary = FAnimFootAnalysisSummary(); RightSummary = FAnimFootAnalysisSummary();
	LeftIntervals.Reset(); RightIntervals.Reset();
	LastRealSampleTime = 0.;
}

void UAnimFootDebugComponent::ResolveBones()
{
	USkeletalMeshComponent* Mesh = BoundMesh.Get();
	if (!Mesh) { return; }
	for (int32 I = 0; I < 2; ++I)
	{
		FFootRuntime& F = Feet[I];
		F.RequestedBone = I == 0 ? LeftFootBone : RightFootBone;
		F.CachedOffset = I == 0 ? LeftSoleOffset : RightSoleOffset;
		F.ResolvedBone = F.RequestedBone;
		F.BoneIndex = Mesh->GetBoneIndex(F.RequestedBone);
		if (F.BoneIndex == INDEX_NONE && bAutoFindFootBones)
		{
			F.ResolvedBone = FindFootBone(Mesh, I == 0);
			F.BoneIndex = Mesh->GetBoneIndex(F.ResolvedBone);
		}
		if (I == 1 && F.BoneIndex == Feet[0].BoneIndex) { F.BoneIndex = INDEX_NONE; F.ResolvedBone = NAME_None; }
		if (F.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogLiveFoot, Warning, TEXT("Mesh=%s missing %s foot '%s'. Set %s in AnimFootDebug component Details (auto-match %s)."),
				*MeshLabel, I == 0 ? TEXT("left") : TEXT("right"), *F.RequestedBone.ToString(),
				I == 0 ? TEXT("LeftFootBone") : TEXT("RightFootBone"), bAutoFindFootBones ? TEXT("failed") : TEXT("disabled"));
		}
		else if (F.ResolvedBone != F.RequestedBone)
		{
			UE_LOG(LogLiveFoot, Display, TEXT("Mesh=%s auto-matched %s -> %s; configure %s to override."), *MeshLabel,
				*F.RequestedBone.ToString(), *F.ResolvedBone.ToString(), I == 0 ? TEXT("LeftFootBone") : TEXT("RightFootBone"));
		}
	}
}

void UAnimFootDebugComponent::SetGlobalIssue(EAnimFootIssue Issue)
{
	ResetSamples();
	LeftFoot.Issue = RightFoot.Issue = Issue;
	if (Issue != LastGlobalIssue && Issue != EAnimFootIssue::Disabled && Issue != EAnimFootIssue::PoseNotReady)
	{
		UE_LOG(LogLiveFoot, Warning, TEXT("%s Mesh=%s: %s"), *GetNameSafe(GetOwner()), *MeshLabel, AnimFootDiagnostics::IssueText(Issue));
	}
	LastGlobalIssue = Issue;
}

void UAnimFootDebugComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Dt, TickType, TickFunction);
	check(IsInGameThread());
	if (!bEnableFootDiagnostics || !GFootEnabled)
	{
		if (bWasEnabled || LeftFoot.Issue != EAnimFootIssue::Disabled) { ReleaseMesh(); SetGlobalIssue(EAnimFootIssue::Disabled); ClearScreen(); }
		bWasEnabled = false;
		return; // No raycasts, animation reads, or drawing while disabled.
	}
	bWasEnabled = true;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset())
	{
		ReleaseMesh(); SetGlobalIssue(EAnimFootIssue::MissingMesh);
	}
	else
	{
		if (Mesh != BoundMesh.Get() || Mesh->GetSkeletalMeshAsset() != BoundAsset.Get()) { BindMesh(Mesh); }
		const bool bValidConfig = Settings.IsValid() && AnimFootDiagnostics::IsFiniteVector(LeftSoleOffset)
			&& AnimFootDiagnostics::IsFiniteVector(RightSoleOffset) && GroundTraceChannel < ECC_MAX;
		const bool bConfigChanged = bValidConfig && (!bSettingsCached || !(Settings == CachedSettings)
			|| Feet[0].RequestedBone != LeftFootBone || Feet[1].RequestedBone != RightFootBone
			|| Feet[0].CachedOffset != LeftSoleOffset || Feet[1].CachedOffset != RightSoleOffset
			|| CachedChannel != GroundTraceChannel || bCachedAutoFind != bAutoFindFootBones);
		if (bConfigChanged)
		{
			ResetSamples(); ResolveBones(); CachedSettings = Settings; CachedChannel = GroundTraceChannel;
			bCachedAutoFind = bAutoFindFootBones; bSettingsCached = true;
		}
		if (GFootFreezePose != bTestFrozen)
		{
			if (GFootFreezePose) { bSavedPause = Mesh->bPauseAnims; Mesh->bPauseAnims = true; }
			else { Mesh->bPauseAnims = bSavedPause; }
			bTestFrozen = GFootFreezePose; ResetSamples();
		}
		if (!bValidConfig) { SetGlobalIssue(EAnimFootIssue::InvalidParameters); }
		else if (!Mesh->GetAnimInstance()) { SetGlobalIssue(EAnimFootIssue::MissingAnimation); }
		else if (!bFreshPose || Mesh->IsRunningParallelEvaluation()) { SetGlobalIssue(EAnimFootIssue::PoseNotReady); }
		else
		{
			const double Now = FPlatformTime::Seconds();
			// World dt can be normal on the first frame after a long editor pause.
			if (LastRealSampleTime > 0. && Now - LastRealSampleTime > Settings.MaxDeltaTime) { ResetSamples(); }
			LastRealSampleTime = Now;
			const bool bAirborne = Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling();
			SampleFoot(0, Dt, bAirborne); SampleFoot(1, Dt, bAirborne);
			LastGlobalIssue = EAnimFootIssue::None;
		}
		bFreshPose = false;
	}
	if (!bDrawDebugVisualization || !GFootDraw) { ClearScreen(); return; }
	ScreenUpdateTime += FMath::IsFinite(Dt) ? FMath::Clamp(Dt, 0.f, 0.25f) : 0.1f;
	const bool bUpdateScreen = ScreenUpdateTime >= 0.1f;
	if (bUpdateScreen) { ScreenUpdateTime = 0.f; }
	if (!bShowScreenData) { ClearScreen(); }
	DrawFoot(0, bUpdateScreen); DrawFoot(1, bUpdateScreen);
}

void UAnimFootDebugComponent::SampleFoot(int32 I, float Dt, bool bAirborne)
{
	FFootRuntime& F = Feet[I];
	FAnimFootDebugResult& R = I == 0 ? LeftFoot : RightFoot;
	R = FAnimFootDebugResult(); R.Bone = F.ResolvedBone;
	USkeletalMeshComponent* Mesh = BoundMesh.Get();
	if (F.BoneIndex == INDEX_NONE || F.BoneIndex >= Mesh->GetNumComponentSpaceTransforms())
	{
		R.Issue = F.BoneIndex == INDEX_NONE ? EAnimFootIssue::MissingBone : EAnimFootIssue::PoseNotReady;
		F.History = FAnimFootHistory(); return;
	}
	const FTransform Bone = Mesh->GetBoneTransform(F.BoneIndex);
	R.BonePosition = Bone.GetLocation();
	R.SolePosition = Bone.TransformPosition(I == 0 ? LeftSoleOffset : RightSoleOffset);
	R.bValidPosition = AnimFootDiagnostics::IsFiniteVector(R.BonePosition) && AnimFootDiagnostics::IsFiniteVector(R.SolePosition);
	if (!R.bValidPosition) { F.History = FAnimFootHistory(); R.Issue = EAnimFootIssue::PoseNotReady; return; }
	const FVector Start = R.SolePosition + FVector::UpVector * Settings.TraceStartOffset;
	const FVector End = Start - FVector::UpVector * Settings.TraceLength;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LiveFootGround), false, GetOwner());
	FHitResult Hit;
	R.bGroundHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, GroundTraceChannel, Params);
	UPrimitiveComponent* Ground = Hit.GetComponent();
	bool bStaticGround = false;
	if (R.bGroundHit)
	{
		R.GroundPoint = Hit.ImpactPoint; R.GroundNormal = Hit.ImpactNormal.GetSafeNormal();
		R.Height = FVector::DotProduct(R.SolePosition - R.GroundPoint, R.GroundNormal);
		R.bValidGround = !Hit.bStartPenetrating && AnimFootDiagnostics::IsFiniteVector(R.GroundNormal)
			&& !R.GroundNormal.IsNearlyZero() && FVector::DotProduct(R.GroundNormal, FVector::UpVector)
			>= FMath::Cos(FMath::DegreesToRadians(Settings.MaxGroundSlope));
		if (Hit.bStartPenetrating) { R.Height = -Settings.MaxPenetration - 1.f; }
	}
	if (Ground != F.Ground.Get())
	{
		F.History = FAnimFootHistory(); F.Ground = Ground; F.bGroundMoved = false;
		if (Ground) { F.GroundTransform = Ground->GetComponentTransform(); }
	}
	if (Ground)
	{
		F.bGroundMoved |= !Ground->GetComponentTransform().Equals(F.GroundTransform, 0.001f);
		F.GroundTransform = Ground->GetComponentTransform();
		bStaticGround = Ground->GetMobility() == EComponentMobility::Static && !F.bGroundMoved && !Ground->IsSimulatingPhysics();
	}
	AnimFootDiagnostics::Analyze(R, F.History, Settings, Dt, bAirborne, bStaticGround);
	if (R.Issue == EAnimFootIssue::InvalidParameters || R.Issue == EAnimFootIssue::InvalidDeltaTime
		|| R.Issue == EAnimFootIssue::Teleport || R.Issue == EAnimFootIssue::Penetration)
	{
		F.Analysis.Reset();
		F.AnalysisTime = 0.f;
		if (I == 0) { LeftSummary = F.Analysis.Summary; LeftIntervals.Reset(); }
		else { RightSummary = F.Analysis.Summary; RightIntervals.Reset(); }
		return;
	}
	F.AnalysisTime += Dt;
	F.Analysis.AddSample(R, Dt, F.AnalysisTime);
	if (I == 0)
	{
		LeftSummary = F.Analysis.Summary;
		LeftIntervals = F.Analysis.Intervals;
	}
	else
	{
		RightSummary = F.Analysis.Summary;
		RightIntervals = F.Analysis.Intervals;
	}
}

void UAnimFootDebugComponent::DrawFoot(int32 I, bool bUpdateScreen)
{
	const FAnimFootDebugResult& R = I == 0 ? LeftFoot : RightFoot;
	const FColor Color = R.bSuspectedSlip ? FColor::Red : R.Phase == EAnimFootPhase::Unknown ? FColor::Yellow
		: R.Phase == EAnimFootPhase::Contact ? FColor::Green : FColor::Cyan;
	if (R.bValidPosition && GetWorld())
	{
		DrawDebugSphere(GetWorld(), R.BonePosition, 3.f, 8, Color, false, 0.f);
		DrawDebugPoint(GetWorld(), R.SolePosition, 7.f, Color, false, 0.f);
		const FVector Start = R.SolePosition + FVector::UpVector * Settings.TraceStartOffset;
		DrawDebugLine(GetWorld(), Start, Start - FVector::UpVector * Settings.TraceLength, Color, false, 0.f, 0, 1.f);
		if (R.bGroundHit)
		{
			DrawDebugPoint(GetWorld(), R.GroundPoint, 8.f, FColor::White, false, 0.f);
			DrawDebugDirectionalArrow(GetWorld(), R.GroundPoint, R.GroundPoint + R.GroundNormal * 12.f, 4.f, FColor::Orange, false, 0.f);
		}
		if (R.bValidVelocity) { DrawDebugDirectionalArrow(GetWorld(), R.BonePosition, R.BonePosition + R.Velocity * 0.1f, 4.f, Color, false, 0.f); }
	}
	// Formatting at 10 Hz only; no per-frame accumulating messages, asset loads or log output.
	if (bShowScreenData && bUpdateScreen && GEngine)
	{
		const FString Height = R.bGroundHit ? FString::Printf(TEXT("%.1f"), R.Height) : TEXT("N/A");
		const FString Speed = R.bValidVelocity ? FString::Printf(TEXT("%.1f"), R.Speed) : TEXT("N/A");
		const FString Vertical = R.bValidVelocity ? FString::Printf(TEXT("%.1f"), R.VerticalSpeed) : TEXT("N/A");
		const FString Tangent = R.bValidGround && R.bValidVelocity ? FString::Printf(TEXT("%.1f"), R.TangentSpeed) : TEXT("N/A");
		const FString Message = FString::Printf(TEXT("LiveFoot %s [%s] Mesh=%s | %s hit=%s h=%s cm | speed=%s vz=%s tangent=%s cm/s | %s | %s%s"),
			I == 0 ? TEXT("L") : TEXT("R"), *R.Bone.ToString(), *MeshLabel, AnimFootDiagnostics::PhaseText(R.Phase),
			R.bGroundHit ? TEXT("yes") : TEXT("no"), *Height, *Speed, *Vertical, *Tangent,
			R.bSlipEvaluated ? (R.bSuspectedSlip ? TEXT("SUSPECTED SLIP") : TEXT("slip=no")) : TEXT("slip=N/A"),
			AnimFootDiagnostics::IssueText(R.Issue), bTestFrozen ? TEXT(" [TEST: POSE FROZEN]") : TEXT(""));
		GEngine->AddOnScreenDebugMessage(MessageKeys[I], 0.5f, Color, Message, false);
	}
}

void UAnimFootDebugComponent::ClearScreen()
{
	if (GEngine && MessageKeys[0] != 0)
	{
		GEngine->RemoveOnScreenDebugMessage(MessageKeys[0]); GEngine->RemoveOnScreenDebugMessage(MessageKeys[1]);
	}
}

void UAnimFootDebugComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseMesh(); ResetSamples(); ClearScreen();
	Super::EndPlay(Reason);
}
