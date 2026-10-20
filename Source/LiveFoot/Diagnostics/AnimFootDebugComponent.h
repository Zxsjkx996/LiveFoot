#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "AnimFootDiagnosticModel.h"
#include "AnimFootDebugComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UPrimitiveComponent;

/** Runtime foot diagnostics only: never edits animation assets or the animation graph. */
UCLASS(ClassGroup=(LiveFoot), meta=(BlueprintSpawnableComponent))
class LIVEFOOT_API UAnimFootDebugComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAnimFootDebugComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") FName LeftFootBone = TEXT("foot_l");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") FName RightFootBone = TEXT("foot_r");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") bool bEnableFootDiagnostics = true;
	/** Master visualization switch controls BOTH geometry and screen text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") bool bDrawDebugVisualization = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") bool bShowScreenData = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") bool bAutoFindFootBones = true;
	/** Bone-local offset to sole in cm; zero retains ankle height for the initial 18 cm threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot", meta=(Units="cm")) FVector LeftSoleOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot", meta=(Units="cm")) FVector RightSoleOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot") TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LiveFoot", meta=(ShowOnlyInnerProperties)) FAnimFootDiagnosticSettings Settings;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") FAnimFootDebugResult LeftFoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") FAnimFootDebugResult RightFoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") FAnimFootAnalysisSummary LeftSummary;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") FAnimFootAnalysisSummary RightSummary;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") TArray<FAnimFootAnalysisInterval> LeftIntervals;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="LiveFoot|Results") TArray<FAnimFootAnalysisInterval> RightIntervals;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	struct FFootRuntime
	{
		FAnimFootHistory History;
		FAnimFootAnalysisAccumulator Analysis;
		float AnalysisTime = 0.f;
		int32 BoneIndex = INDEX_NONE;
		FName ResolvedBone = NAME_None;
		FName RequestedBone = NAME_None;
		FVector CachedOffset = FVector::ZeroVector;
		TWeakObjectPtr<UPrimitiveComponent> Ground;
		FTransform GroundTransform = FTransform::Identity;
		bool bGroundMoved = false;
	};
	FFootRuntime Feet[2];
	TWeakObjectPtr<USkeletalMeshComponent> BoundMesh;
	TWeakObjectPtr<USkeletalMesh> BoundAsset;
	FDelegateHandle PoseDelegate;
	bool bFreshPose = false;
	bool bWasEnabled = false;
	bool bSettingsCached = false;
	bool bCachedAutoFind = true;
	ECollisionChannel CachedChannel = ECC_Visibility;
	FAnimFootDiagnosticSettings CachedSettings;
	EVisibilityBasedAnimTickOption SavedVisibility = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	bool bSavedUpdateRate = false;
	bool bSavedPause = false;
	bool bTestFrozen = false;
	EAnimFootIssue LastGlobalIssue = EAnimFootIssue::None;
	FString MeshLabel;
	double LastRealSampleTime = 0.;
	float ScreenUpdateTime = 0.f;
	uint64 MessageKeys[2] = {0, 0};

	void BindMesh(USkeletalMeshComponent* Mesh);
	void ReleaseMesh();
	void OnPoseFinalized();
	void ResolveBones();
	void ResetSamples();
	void SetGlobalIssue(EAnimFootIssue Issue);
	void SampleFoot(int32 FootIndex, float DeltaTime, bool bAirborne);
	void DrawFoot(int32 FootIndex, bool bUpdateScreen);
	void ClearScreen();
};
