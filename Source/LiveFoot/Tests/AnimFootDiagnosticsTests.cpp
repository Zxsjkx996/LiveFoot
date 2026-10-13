#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Diagnostics/AnimFootDebugComponent.h"
#include "LiveFootCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr EAutomationTestFlags LiveFootTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	FAnimFootDebugResult Frame(const FVector& Position, bool bGround = true)
	{
		FAnimFootDebugResult R;
		R.BonePosition = R.SolePosition = Position;
		R.bValidPosition = true;
		R.bGroundHit = R.bValidGround = bGround;
		R.GroundPoint = FVector(Position.X, Position.Y, 0.);
		R.Height = Position.Z;
		return R;
	}
	UClass* PawnBlueprint()
	{
		return LoadClass<ACharacter>(nullptr, TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootNumericalTest, "LiveFoot.NumericsAndTiming", LiveFootTestFlags)
bool FFootNumericalTest::RunTest(const FString& Parameters)
{
	FAnimFootDiagnosticSettings S; S.SmoothingTime = 0.f;
	for (const int32 Hz : {30, 60, 120})
	{
		const float Dt = 1.f / Hz;
		FAnimFootHistory H;
		FAnimFootDebugResult R = Frame(FVector(0., 0., 10.));
		AnimFootDiagnostics::Analyze(R, H, S, Dt, false, true);
		TestFalse(TEXT("First sample has no fake velocity"), R.bValidVelocity);
		float AlarmTime = -1.f;
		for (int32 I = 1; I <= Hz; ++I)
		{
			R = Frame(FVector(50. * I * Dt, 0., 10.));
			AnimFootDiagnostics::Analyze(R, H, S, Dt, false, true);
			TestTrue(TEXT("Known world speed is 50 cm/s"), FMath::IsNearlyEqual(R.Speed, 50.f, 0.05f));
			TestTrue(TEXT("Known tangent speed is 50 cm/s"), FMath::IsNearlyEqual(R.TangentSpeed, 50.f, 0.05f));
			if (AlarmTime < 0.f && R.bSuspectedSlip) { AlarmTime = I * Dt; }
		}
		TestTrue(TEXT("High tangent speed does not reject Contact"), R.Phase == EAnimFootPhase::Contact);
		TestTrue(TEXT("Sustained slide raises an alarm"), R.bSuspectedSlip);
		TestTrue(TEXT("Seconds-based contact+slip latency within frame quantization"),
			AlarmTime >= S.SlipDuration && AlarmTime <= S.ContactConfirmTime + S.SlipDuration + Dt + 0.001f);
		for (int32 I = 0; I < Hz; ++I)
		{
			R = Frame(FVector(50., 0., 10.)); AnimFootDiagnostics::Analyze(R, H, S, Dt, false, true);
		}
		TestFalse(TEXT("Stationary support clears alarm"), R.bSuspectedSlip);
		TestTrue(TEXT("Stationary support remains Contact"), R.Phase == EAnimFootPhase::Contact);
		TestTrue(TEXT("Stationary tangent speed zero"), FMath::IsNearlyZero(R.TangentSpeed));
	}
	// Motion along the normal must not count as tangent motion (45 degree plane).
	FAnimFootHistory H;
	FAnimFootDebugResult R = Frame(FVector(0., 0., 10.)); R.GroundNormal = FVector(1., 0., 1.).GetSafeNormal();
	AnimFootDiagnostics::Analyze(R, H, S, 0.02f, false, true);
	R = Frame(FVector(0.1, 0., 10.1)); R.GroundNormal = FVector(1., 0., 1.).GetSafeNormal();
	AnimFootDiagnostics::Analyze(R, H, S, 0.02f, false, true);
	TestTrue(TEXT("Normal velocity removed by plane projection"), R.RawTangentSpeed < 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootBoundaryTest, "LiveFoot.Boundaries", LiveFootTestFlags)
bool FFootBoundaryTest::RunTest(const FString& Parameters)
{
	FAnimFootDiagnosticSettings S; S.SmoothingTime = 0.f;
	FAnimFootHistory H;
	FAnimFootDebugResult R;
	auto Step = [&](const FVector& P, float Dt = 0.02f, bool bGround = true, bool bAir = false, bool bStatic = true)
	{
		R = Frame(P, bGround); AnimFootDiagnostics::Analyze(R, H, S, Dt, bAir, bStatic);
	};
	for (int32 I = 0; I < 10; ++I) { Step(FVector(0., 0., 10.)); }
	TestTrue(TEXT("Still foot contacts"), R.Phase == EAnimFootPhase::Contact);
	Step(FVector(1., 0., 10.)); Step(FVector(2., 0., 10.)); // 40 ms, shorter than slip duration.
	TestFalse(TEXT("Short spike is not a slip"), R.bSuspectedSlip);
	Step(FVector(2., 0., 10.));
	TestFalse(TEXT("Subthreshold frame resets candidate"), R.bSuspectedSlip);
	Step(FVector(3., 0., 10.), 0.02f, false);
	TestFalse(TEXT("Lost ground disables slip before debounce ends"), R.bSlipEvaluated);
	for (int32 I = 0; I < 20; ++I) { Step(FVector(I * 1., 0., 10.), 0.02f, true, false, false); }
	TestFalse(TEXT("Movable ground never raises slip"), R.bSuspectedSlip);
	TestTrue(TEXT("Moving ground has explicit N/A reason"), R.Issue == EAnimFootIssue::MovingGround);
	Step(FVector(20., 0., 10.), 0.02f, true, true);
	TestTrue(TEXT("Airborne forces Swing even near ground"), R.Phase == EAnimFootPhase::Swing);
	Step(FVector(20., 0., 10.), 0.f);
	TestTrue(TEXT("Zero dt invalidates phase"), R.Phase == EAnimFootPhase::Unknown);
	TestFalse(TEXT("Zero dt velocity unavailable"), R.bValidVelocity);
	Step(FVector(20., 0., 10.), 0.02f);
	TestFalse(TEXT("Resuming seeds history"), R.bValidVelocity);
	Step(FVector(21., 0., 10.), 1.f);
	TestTrue(TEXT("Large dt rejected"), R.Issue == EAnimFootIssue::InvalidDeltaTime);
	Step(FVector(0., 0., 10.)); Step(FVector(1000., 0., 10.));
	TestTrue(TEXT("Teleport resets"), R.Issue == EAnimFootIssue::Teleport);
	TestFalse(TEXT("Teleport has no spike"), R.bValidVelocity);
	Step(FVector(1000., 0., -20.));
	TestTrue(TEXT("Penetration invalidates support"), R.Phase == EAnimFootPhase::Unknown);
	S.ContactExitHeight = S.ContactHeight;
	TestFalse(TEXT("Bad hysteresis rejected"), S.IsValid());
	Step(FVector(0., 0., 10.)); TestTrue(TEXT("Invalid settings are explicit"), R.Issue == EAnimFootIssue::InvalidParameters);
	S = FAnimFootDiagnosticSettings(); H = FAnimFootHistory();
	for (int32 I = 0; I < 8; ++I) { Step(FVector(0., 0., 9. + I)); }
	TestTrue(TEXT("Near-ground high vertical speed does not enter contact"), R.Phase == EAnimFootPhase::Swing);
	for (int32 I = 0; I < 10; ++I) { Step(FVector(0., 0., 16.)); }
	TestTrue(TEXT("Settled vertical motion enters contact"), R.Phase == EAnimFootPhase::Contact);
	Step(FVector(0., 0., 16.), -0.02f); TestFalse(TEXT("Negative dt has no velocity"), R.bValidVelocity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootAssetTest, "LiveFoot.ProjectAssets", LiveFootTestFlags)
bool FFootAssetTest::RunTest(const FString& Parameters)
{
	UClass* PawnClass = PawnBlueprint();
	if (!TestNotNull(TEXT("Existing pawn Blueprint loads"), PawnClass)) { return false; }
	TestTrue(TEXT("Redirected parent is ALiveFootCharacter"), PawnClass->IsChildOf(ALiveFootCharacter::StaticClass()));
	ACharacter* CDO = Cast<ACharacter>(PawnClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Character CDO"), CDO)) { return false; }
	TestNotNull(TEXT("Inherited native diagnostic component"), CDO->FindComponentByClass<UAnimFootDebugComponent>());
	USkeletalMeshComponent* Mesh = CDO->GetMesh();
	if (!TestNotNull(TEXT("Mesh component"), Mesh) || !TestNotNull(TEXT("Mesh asset"), Mesh->GetSkeletalMeshAsset())) { return false; }
	TestTrue(TEXT("foot_l exists"), Mesh->GetBoneIndex(TEXT("foot_l")) != INDEX_NONE);
	TestTrue(TEXT("foot_r exists"), Mesh->GetBoneIndex(TEXT("foot_r")) != INDEX_NONE);
	TestTrue(TEXT("Uses animation Blueprint"), Mesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint);
	TestNotNull(TEXT("AnimClass set"), Mesh->GetAnimClass());
	FString GameModePath;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
	UClass* ModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
	if (TestNotNull(TEXT("Configured GameMode loads"), ModeClass))
	{
		TestTrue(TEXT("Configured default Pawn matches verified character"), ModeClass->GetDefaultObject<AGameModeBase>()->DefaultPawnClass == PawnClass);
	}
	AddInfo(FString::Printf(TEXT("Verified Pawn=%s parent=%s Mesh=%s AnimClass=%s"), *PawnClass->GetPathName(),
		*PawnClass->GetSuperClass()->GetPathName(), *Mesh->GetSkeletalMeshAsset()->GetPathName(), *GetNameSafe(Mesh->GetAnimClass())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootComponentTest, "LiveFoot.ComponentIntegration", LiveFootTestFlags)
bool FFootComponentTest::RunTest(const FString& Parameters)
{
	UClass* PawnClass = PawnBlueprint();
	if (!PawnClass || !GEngine) { AddError(TEXT("Pawn class or engine unavailable")); return false; }
	UWorld::InitializationValues Init;
	Init.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).RequiresHitProxies(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Transient physics test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetBoxExtent(FVector(5000., 5000., 5.));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent(); Box->SetWorldLocation(FVector(0., 0., -5.));
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Pawn = World->SpawnActor<ACharacter>(PawnClass, FVector(0., 0., 96.), FRotator::ZeroRotator, Spawn);
	if (!Pawn) { AddError(TEXT("Test Pawn spawn failed")); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
	USkeletalMeshComponent* Mesh = Pawn->GetMesh();
	UAnimFootDebugComponent* Debug = Pawn->FindComponentByClass<UAnimFootDebugComponent>();
	if (!Debug) { AddError(TEXT("Native component absent")); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return false; }
	Debug->bDrawDebugVisualization = false;
	Pawn->GetCharacterMovement()->DisableMovement();
	const EVisibilityBasedAnimTickOption OriginalVisibility = Mesh->VisibilityBasedAnimTickOption;
	const bool OriginalURO = Mesh->bEnableUpdateRateOptimizations;
	// Synchronous manual pose refresh is TEST ONLY. Production uses late Tick + finalized-pose delegate.
	auto Tick = [&](float Dt = 1.f / 60.f)
	{
		Mesh->TickAnimation(1.f / 60.f, false); Mesh->RefreshBoneTransforms();
		Debug->TickComponent(Dt, LEVELTICK_All, nullptr);
	};
	for (int32 I = 0; I < 60; ++I) { Tick(); }
	TestTrue(TEXT("Left samples real skeletal pose"), Debug->LeftFoot.bValidPosition);
	TestTrue(TEXT("Right samples real skeletal pose"), Debug->RightFoot.bValidPosition);
	TestTrue(TEXT("Own capsule ignored: ground trace hits floor"), Debug->LeftFoot.bGroundHit && Debug->RightFoot.bGroundHit);
	AddInfo(FString::Printf(TEXT("Real Quinn ankle heights L=%.3f R=%.3f cm; phases L=%s R=%s"), Debug->LeftFoot.Height,
		Debug->RightFoot.Height, AnimFootDiagnostics::PhaseText(Debug->LeftFoot.Phase), AnimFootDiagnostics::PhaseText(Debug->RightFoot.Phase)));
	// Pause an actual pose then translate the actual character: world-space slide, no data injection.
	const bool OriginalPause = Mesh->bPauseAnims;
	Mesh->bPauseAnims = true;
	for (int32 I = 0; I < 60; ++I) { Pawn->AddActorWorldOffset(FVector(1., 0., 0.)); Tick(); }
	TestTrue(TEXT("Frozen-pose actual world slide alarms on at least one foot"), Debug->LeftFoot.bSuspectedSlip || Debug->RightFoot.bSuspectedSlip);
	Debug->LeftFootBone = TEXT("nonexistent_foot"); Tick(); Tick();
	TestTrue(TEXT("Auto-match recovers real left bone"), Debug->LeftFoot.Bone == FName(TEXT("foot_l")));
	AddExpectedMessagePlain(TEXT("Mesh=SKM_Quinn_Simple missing left foot 'nonexistent_foot'"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	Debug->bAutoFindFootBones = false; Tick(); Tick(); Tick();
	TestTrue(TEXT("Explicit missing left bone handled"), Debug->LeftFoot.Issue == EAnimFootIssue::MissingBone);
	TestTrue(TEXT("Right continues after missing left"), Debug->RightFoot.bValidPosition);
	Debug->LeftFootBone = TEXT("foot_l"); Tick(); Tick();
	TestTrue(TEXT("Restored left recovers"), Debug->LeftFoot.bValidPosition);
	UClass* OriginalAnimClass = Mesh->GetAnimClass();
	AddExpectedMessagePlain(TEXT("missing animation instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Mesh->SetAnimInstanceClass(nullptr); Tick(); Tick();
	TestTrue(TEXT("Missing animation safely reported"), Debug->LeftFoot.Issue == EAnimFootIssue::MissingAnimation);
	Mesh->SetAnimInstanceClass(OriginalAnimClass); Tick(); Tick();
	TestTrue(TEXT("Animation instance restoration recovers"), Debug->LeftFoot.bValidPosition);
	Tick(0.f); TestTrue(TEXT("Component handles invalid dt"), Debug->LeftFoot.Issue == EAnimFootIssue::InvalidDeltaTime);
	Tick(); TestFalse(TEXT("Component resumes without fake velocity"), Debug->LeftFoot.bValidVelocity);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Tick(); Tick(); Tick();
	TestFalse(TEXT("No floor means no hit"), Debug->LeftFoot.bGroundHit);
	TestFalse(TEXT("No ground means no slip"), Debug->LeftFoot.bSuspectedSlip);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Tick(); Tick();
	Box->SetMobility(EComponentMobility::Movable); Tick(); Tick();
	TestTrue(TEXT("Real movable component marked unsupported"), Debug->LeftFoot.Issue == EAnimFootIssue::MovingGround);
	TestFalse(TEXT("Movable component slip N/A"), Debug->LeftFoot.bSlipEvaluated);
	Debug->bEnableFootDiagnostics = false; Tick();
	TestTrue(TEXT("Disable clears output"), Debug->LeftFoot.Issue == EAnimFootIssue::Disabled);
	TestTrue(TEXT("Disable restores visibility policy"), Mesh->VisibilityBasedAnimTickOption == OriginalVisibility);
	TestTrue(TEXT("Disable restores URO"), !!Mesh->bEnableUpdateRateOptimizations == OriginalURO);
	Mesh->bPauseAnims = OriginalPause;
	Debug->bEnableFootDiagnostics = true; Tick(); Tick();
	IConsoleVariable* Freeze = IConsoleManager::Get().FindConsoleVariable(TEXT("LiveFoot.FreezePose"));
	const bool PreviousFreeze = Freeze->GetBool();
	Freeze->Set(1, ECVF_SetByCode); Tick();
	TestTrue(TEXT("Test CVar freezes real mesh"), !!Mesh->bPauseAnims);
	World->EndPlay(EEndPlayReason::Quit);
	TestTrue(TEXT("EndPlay restores animation pause"), !!Mesh->bPauseAnims == OriginalPause);
	TestTrue(TEXT("EndPlay restores visibility"), Mesh->VisibilityBasedAnimTickOption == OriginalVisibility);
	TestTrue(TEXT("EndPlay restores URO"), !!Mesh->bEnableUpdateRateOptimizations == OriginalURO);
	Freeze->Set(PreviousFreeze ? 1 : 0, ECVF_SetByCode);
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

class FFootLiveMapCommand : public IAutomationLatentCommand
{
public:
	explicit FFootLiveMapCommand(FAutomationTestBase* InTest) : Test(InTest), Start(FPlatformTime::Seconds()) {}
	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Start < 3.) { return false; }
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->WorldType != EWorldType::Game || !World->HasBegunPlay()) { continue; }
			ACharacter* Pawn = UGameplayStatics::GetPlayerCharacter(World, 0);
			if (!Pawn) { continue; }
			UAnimFootDebugComponent* Debug = Pawn->FindComponentByClass<UAnimFootDebugComponent>();
			Test->TestNotNull(TEXT("Running default-map Pawn owns native component"), Debug);
			if (Debug)
			{
				Test->TestTrue(TEXT("Scheduled late Tick gets final left pose"), Debug->LeftFoot.bValidPosition);
				Test->TestTrue(TEXT("Scheduled late Tick gets final right pose"), Debug->RightFoot.bValidPosition);
				Test->TestTrue(TEXT("Scheduled velocity updates"), Debug->LeftFoot.bValidVelocity && Debug->RightFoot.bValidVelocity);
				Test->TestTrue(TEXT("Actual map supports contact"), Debug->LeftFoot.Phase == EAnimFootPhase::Contact || Debug->RightFoot.Phase == EAnimFootPhase::Contact);
				Test->AddInfo(FString::Printf(TEXT("HEADLESS GAME (not PIE): Map=%s Pawn=%s Mesh=%s Anim=%s hL=%.2f hR=%.2f"),
					*World->GetMapName(), *Pawn->GetClass()->GetPathName(), *GetNameSafe(Pawn->GetMesh()->GetSkeletalMeshAsset()),
					*GetNameSafe(Pawn->GetMesh()->GetAnimInstance()), Debug->LeftFoot.Height, Debug->RightFoot.Height));
			}
			return true;
		}
		Test->AddError(TEXT("LiveFoot.LiveMapSmoke requires launching the default ThirdPerson map with -game; no active player world found."));
		return true;
	}
private:
	FAutomationTestBase* Test;
	double Start;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFootLiveMapTest, "LiveFoot.LiveMapSmoke", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFootLiveMapTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFootLiveMapCommand(this));
	return true;
}

#endif
