#include "LiveFootAnimationBake.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SavePackage.h"

namespace
{
	bool IsFiniteTransform(const FTransform& Transform)
	{
		return Transform.IsValid() && AnimFootDiagnostics::IsFiniteVector(Transform.GetLocation())
			&& AnimFootDiagnostics::IsFiniteVector(Transform.GetScale3D())
			&& FMath::IsFinite(Transform.GetRotation().X) && FMath::IsFinite(Transform.GetRotation().Y)
			&& FMath::IsFinite(Transform.GetRotation().Z) && FMath::IsFinite(Transform.GetRotation().W);
	}

	bool BuildComponentPose(UAnimSequence* Sequence, const IAnimationDataModel* Model, const FFrameTime& FrameTime,
		TArray<FTransform>& OutPose, FString& OutError)
	{
		if (!Sequence || !Model || !Sequence->GetSkeleton()) { OutError = TEXT("Animation data became unavailable"); return false; }
		const FReferenceSkeleton& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
		const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
		OutPose.SetNum(Ref.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
		{
			const FName BoneName = Ref.GetBoneName(BoneIndex);
			FTransform Local = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
			if (Model->IsValidBoneTrackName(BoneName))
			{
				Local = Model->EvaluateBoneTrackTransform(BoneName, FrameTime, Sequence->Interpolation);
			}
			if (!IsFiniteTransform(Local)) { OutError = FString::Printf(TEXT("Non-finite source transform for %s"), *BoneName.ToString()); return false; }
			const int32 Parent = Ref.GetParentIndex(BoneIndex);
			OutPose[BoneIndex] = Parent == INDEX_NONE ? Local : Local * OutPose[Parent];
			if (!IsFiniteTransform(OutPose[BoneIndex])) { OutError = FString::Printf(TEXT("Invalid component transform for %s"), *BoneName.ToString()); return false; }
		}
		return true;
	}

	const FLiveFootSequenceSample* FindSample(const FLiveFootSequenceAnalysisResult& Analysis, float Time)
	{
		if (Analysis.Samples.Num() == 0) { return nullptr; }
		int32 Low = 0, High = Analysis.Samples.Num() - 1;
		while (Low < High)
		{
			const int32 Mid = (Low + High + 1) / 2;
			if (Analysis.Samples[Mid].Time <= Time) { Low = Mid; } else { High = Mid - 1; }
		}
		return &Analysis.Samples[Low];
	}

	bool IsAncestor(const FReferenceSkeleton& Ref, int32 MaybeAncestor, int32 Bone)
	{
		for (int32 Parent = Ref.GetParentIndex(Bone); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		{
			if (Parent == MaybeAncestor) { return true; }
		}
		return false;
	}

	bool BuildTrack(UAnimSequence* Source, const IAnimationDataModel* Model, FName BoneName, bool bEnabled,
		const FLiveFootSequenceAnalysisResult& Analysis, bool bLeft, TArray<FVector>& OutPos, TArray<FQuat>& OutRot,
		TArray<FVector>& OutScale, int32& OutCorrected, float& OutMaxCorrection, float& OutMaxAfterHeight, FString& OutError)
	{
		OutPos.Reset(); OutRot.Reset(); OutScale.Reset();
		if (!bEnabled) { return true; }
		if (!Model->IsValidBoneTrackName(BoneName)) { OutError = FString::Printf(TEXT("Foot bone %s has no writable animation track"), *BoneName.ToString()); return false; }
		TArray<FTransform> LocalKeys;
		Model->GetBoneTrackTransforms(BoneName, LocalKeys);
		if (LocalKeys.Num() == 0) { OutError = FString::Printf(TEXT("Foot bone %s has no animation keys"), *BoneName.ToString()); return false; }
		const FReferenceSkeleton& Ref = Source->GetSkeleton()->GetReferenceSkeleton();
		const int32 BoneIndex = Ref.FindBoneIndex(BoneName);
		const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
		const FFrameRate Rate = Model->GetFrameRate();
		if (!Rate.IsValid() || Rate.AsDecimal() <= 0.f) { OutError = TEXT("Source animation frame rate is invalid"); return false; }
		for (int32 KeyIndex = 0; KeyIndex < LocalKeys.Num(); ++KeyIndex)
		{
			const FFrameTime FrameTime(FFrameNumber(KeyIndex), 0.f);
			TArray<FTransform> Pose;
			if (!BuildComponentPose(Source, Model, FrameTime, Pose, OutError)) { return false; }
			FTransform Corrected = LocalKeys[KeyIndex];
			const float Time = static_cast<float>(FrameTime.AsDecimal() / Rate.AsDecimal());
			const FLiveFootSequenceSample* Sample = FindSample(Analysis, Time);
			const FAnimFootDebugResult* Foot = Sample ? (bLeft ? &Sample->Left : &Sample->Right) : nullptr;
			// Raw Sequence analysis can report a positive contact height even when
			// the final Anim Blueprint/Foot IK already places the foot correctly.
			// Correcting every Contact frame would therefore double-apply IK and can
			// sink an otherwise healthy animation. The direct-track path is
			// deliberately conservative: only an explicitly diagnosed suspected-slip
			// Contact sample that is actually below the configured ground plane is
			// eligible. This path can lift penetration out of the floor, but never
			// pushes a healthy raw contact down into a final Foot IK result.
			if (Foot && Foot->bValidPosition && Foot->bValidGround && Foot->Phase == EAnimFootPhase::Contact
				&& Foot->bSuspectedSlip && Foot->Height < -KINDA_SMALL_NUMBER)
			{
				const float Height = Foot->Height;
				const FVector Normal = Foot->GroundNormal.GetSafeNormal();
				if (FMath::Abs(Height) > KINDA_SMALL_NUMBER && AnimFootDiagnostics::IsFiniteVector(Normal) && !Normal.IsNearlyZero())
				{
					const FVector Delta = -Height * Normal;
					FTransform DesiredComponent = Pose[BoneIndex];
					DesiredComponent.AddToTranslation(Delta);
					const FTransform DesiredLocal = ParentIndex == INDEX_NONE ? DesiredComponent : DesiredComponent.GetRelativeTransform(Pose[ParentIndex]);
					if (!IsFiniteTransform(DesiredLocal)) { OutError = TEXT("Computed corrected foot transform is invalid"); return false; }
					Corrected.SetTranslation(DesiredLocal.GetTranslation());
					Corrected.SetRotation(DesiredLocal.GetRotation());
					Corrected.SetScale3D(DesiredLocal.GetScale3D());
					++OutCorrected;
					OutMaxCorrection = FMath::Max(OutMaxCorrection, Delta.Size());
				}
			}
			OutPos.Add(Corrected.GetTranslation());
			OutRot.Add(Corrected.GetRotation());
			OutScale.Add(Corrected.GetScale3D());
		}
		return true;
	}
}

bool LiveFootAnimationBake::BakeCorrectedCopy(UAnimSequence* Source, const FLiveFootSequenceAnalysisOptions& Options,
	const FLiveFootSequenceAnalysisResult& Analysis, const FLiveFootBakeOptions& BakeOptions, FLiveFootBakeResult& OutResult)
{
	OutResult = FLiveFootBakeResult();
	if (!Source || !Analysis.bSuccess || Analysis.Samples.Num() == 0) { OutResult.Error = TEXT("Select a successful analysis result before baking"); return false; }
	if (!BakeOptions.OutputFolder.StartsWith(TEXT("/Game/")) || BakeOptions.OutputFolder.Contains(TEXT(".")))
	{ OutResult.Error = TEXT("Output folder must be a project Content folder such as /Game/LiveFootGenerated"); return false; }
	const IAnimationDataModel* Model = Source->GetDataModel();
	USkeleton* Skeleton = Source->GetSkeleton();
	if (!Model || !Skeleton) { OutResult.Error = TEXT("Source animation has no usable data model or skeleton"); return false; }
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	const int32 LeftIndex = Ref.FindBoneIndex(Options.LeftFootBone);
	const int32 RightIndex = Ref.FindBoneIndex(Options.RightFootBone);
	if (LeftIndex == INDEX_NONE || RightIndex == INDEX_NONE) { OutResult.Error = TEXT("Configured foot bone is missing from the source skeleton"); return false; }
	if (IsAncestor(Ref, LeftIndex, RightIndex) || IsAncestor(Ref, RightIndex, LeftIndex))
	{ OutResult.Error = TEXT("The two configured foot bones cannot be in the same parent chain for independent correction"); return false; }

	FString PackageName, AssetName;
	FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(
		BakeOptions.OutputFolder / Source->GetName(), BakeOptions.NameSuffix, PackageName, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) { OutResult.Error = TEXT("Could not create destination package"); return false; }
	UAnimSequence* Copy = DuplicateObject<UAnimSequence>(Source, Package, FName(*AssetName));
	if (!Copy) { OutResult.Error = TEXT("Could not duplicate source animation"); return false; }
	Copy->SetFlags(RF_Public | RF_Standalone);

	TArray<FVector> LeftPos, RightPos, LeftScale, RightScale;
	TArray<FQuat> LeftRot, RightRot;
	FString Error;
	if (!BuildTrack(Source, Model, Options.LeftFootBone, BakeOptions.bCorrectLeft, Analysis, true, LeftPos, LeftRot, LeftScale,
		OutResult.CorrectedKeys, OutResult.MaxCorrection, OutResult.MaxAfterContactHeight, Error)
		|| !BuildTrack(Source, Model, Options.RightFootBone, BakeOptions.bCorrectRight, Analysis, false, RightPos, RightRot, RightScale,
		OutResult.CorrectedKeys, OutResult.MaxCorrection, OutResult.MaxAfterContactHeight, Error))
	{ OutResult.Error = Error; return false; }

	IAnimationDataController& Controller = Copy->GetController();
	{
		IAnimationDataController::FScopedBracket Bracket(Controller, NSLOCTEXT("LiveFoot", "BakeFootCorrection", "Bake LiveFoot foot correction"));
		if (BakeOptions.bCorrectLeft && !Controller.SetBoneTrackKeys(Options.LeftFootBone, LeftPos, LeftRot, LeftScale))
		{ OutResult.Error = TEXT("Could not write corrected left foot track"); return false; }
		if (BakeOptions.bCorrectRight && !Controller.SetBoneTrackKeys(Options.RightFootBone, RightPos, RightRot, RightScale))
		{ OutResult.Error = TEXT("Could not write corrected right foot track"); return false; }
	}
	FLiveFootSequenceAnalysisResult Validation;
	FLiveFootSequenceAnalysisOptions ValidationOptions = Options;
	ValidationOptions.SampleRate = Model->GetFrameRate().AsDecimal();
	if (!LiveFootSequenceAnalysis::Analyze(Copy, ValidationOptions, Validation))
	{ OutResult.Error = TEXT("Corrected copy failed re-sampling validation: ") + Validation.FailureReason; return false; }
	OutResult.BeforeSlipDuration = Analysis.LeftSummary.SuspectedSlipDuration + Analysis.RightSummary.SuspectedSlipDuration;
	OutResult.AfterSlipDuration = Validation.LeftSummary.SuspectedSlipDuration + Validation.RightSummary.SuspectedSlipDuration;
	for (const FLiveFootSequenceSample& Sample : Validation.Samples)
	{
		for (const FAnimFootDebugResult* Foot : {&Sample.Left, &Sample.Right})
		{
			if (Foot->bValidGround && Foot->Phase == EAnimFootPhase::Contact) { OutResult.MaxAfterContactHeight = FMath::Max(OutResult.MaxAfterContactHeight, FMath::Abs(Foot->Height)); }
		}
	}

	if (!BakeOptions.bSaveToDisk)
	{
		OutResult.bSuccess = true;
		OutResult.AssetPath = Copy->GetPathName();
		return true;
	}
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Copy);
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Copy, *Filename, SaveArgs))
	{ OutResult.Error = FString::Printf(TEXT("Failed to save corrected copy: %s"), *Filename); return false; }
	OutResult.bSuccess = true;
	OutResult.AssetPath = Copy->GetPathName();
	OutResult.PackageFilename = Filename;
	const FString AuditDirectory = FPaths::ProjectSavedDir() / TEXT("LiveFootAnalysis");
	IFileManager::Get().MakeDirectory(*AuditDirectory, true);
	OutResult.AuditFilename = AuditDirectory / (AssetName + TEXT("_audit.txt"));
	const FString Audit = FString::Printf(TEXT("LiveFoot V2.0 direct track bake\nsource=%s\noutput=%s\ncorrected_keys=%d\nmax_correction_cm=%.6f\nbefore_slip_s=%.6f\nafter_slip_s=%.6f\nmax_after_contact_height_cm=%.6f\nmode=explicit opt-in; suspected-slip Contact penetration lift only; source unchanged\n"),
		*Source->GetPathName(), *OutResult.AssetPath, OutResult.CorrectedKeys, OutResult.MaxCorrection, OutResult.BeforeSlipDuration, OutResult.AfterSlipDuration, OutResult.MaxAfterContactHeight);
	FFileHelper::SaveStringToFile(Audit, *OutResult.AuditFilename, FFileHelper::EEncodingOptions::ForceUTF8);
	return true;
}
