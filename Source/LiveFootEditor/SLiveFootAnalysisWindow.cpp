#include "SLiveFootAnalysisWindow.h"
#include "SLiveFootAnalysisPreview.h"
#include "LiveFootAnimationBake.h"
#include "LiveFootReports.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "IAnimationEditor.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/Paths.h"

namespace
{
	FString NormalizeContentFolderPath(FString InFolder)
	{
		InFolder.TrimStartAndEndInline();
		InFolder.ReplaceInline(TEXT("\\"), TEXT("/"));
		// Content Browser's virtual root is exposed as /All/Game/... while the
		// Asset Registry expects the package path /Game/.... Normalize at the UI
		// boundary so selected assets and folder scans use the same path space.
		if (InFolder.Equals(TEXT("/All"), ESearchCase::IgnoreCase))
		{
			return TEXT("/");
		}
		if (InFolder.StartsWith(TEXT("/All/"), ESearchCase::IgnoreCase))
		{
			InFolder.RightChopInline(4);
		}
		return InFolder;
	}
}

void SLiveFootAnalysisWindow::Construct(const FArguments& Arguments)
{
	ControlRigCapability = LiveFootControlRigPreview::Inspect();
	const auto SettingsEnabled = [this] { return !Batch.IsRunning(); };
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(
			TEXT("LiveFoot final | Sequence raw pose | cm, cm/s, seconds | static plane | P95: recent 256 samples | intervals: latest 64 per foot | bake is explicit opt-in")))]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SUniformGridPanel).SlotPadding(3)
			+ SUniformGridPanel::Slot(0,0)[SNew(SButton).Text(FText::FromString(TEXT("Analyze selected Sequences"))).IsEnabled_Lambda(SettingsEnabled).OnClicked_Lambda([this] { AnalyzeSelected(); return FReply::Handled(); })]
			+ SUniformGridPanel::Slot(1,0)[SNew(SButton).Text(FText::FromString(TEXT("Analyze folder recursively"))).IsEnabled_Lambda(SettingsEnabled).OnClicked_Lambda([this] { AnalyzeFolder(); return FReply::Handled(); })]
			+ SUniformGridPanel::Slot(2,0)[SNew(SButton).Text(this, &SLiveFootAnalysisWindow::CancelText).IsEnabled_Lambda([this] { return Batch.IsRunning() || Selected.IsValid(); }).OnClicked_Lambda([this] { Cancel(); return FReply::Handled(); })]
			+ SUniformGridPanel::Slot(3,0)[SNew(SButton).Text(FText::FromString(TEXT("Clear cache"))).IsEnabled_Lambda(SettingsEnabled).OnClicked_Lambda([this] { Batch.ClearCache(); Message = TEXT("Cache cleared."); Refresh(); return FReply::Handled(); })]
			+ SUniformGridPanel::Slot(4,0)[SNew(SButton).Text(FText::FromString(TEXT("Export visible rows CSV"))).IsEnabled_Lambda(SettingsEnabled).OnClicked(this, &SLiveFootAnalysisWindow::Export)]
			+ SUniformGridPanel::Slot(5,0)[SNew(SButton).Text(FText::FromString(TEXT("Bake corrected copy"))).IsEnabled_Lambda([this] { return !Batch.IsRunning() && Selected && Selected->Result && Selected->Result->bSuccess; }).OnClicked_Lambda([this] { BakeSelected(); return FReply::Handled(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("Output package folder: ")))]
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).IsEnabled_Lambda(SettingsEnabled).Text_Lambda([this] { return FText::FromString(OutputFolder); })
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { OutputFolder = Text.ToString().TrimStartAndEnd(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("Folder: ")))]
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox).IsEnabled_Lambda(SettingsEnabled).Text_Lambda([this] { return FText::FromString(Folder); })
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { Folder = Text.ToString().TrimStartAndEnd(); })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Use selected folder"))).IsEnabled_Lambda(SettingsEnabled).OnClicked_Lambda([this]
			{
				TArray<FString> Folders; FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedFolders(Folders);
				if (Folders.Num()) { Folder = NormalizeContentFolderPath(Folders[0]); } else { Message = TEXT("Select a folder in Content Browser first."); }
				return FReply::Handled();
			})]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SUniformGridPanel).SlotPadding(3)
			+ SUniformGridPanel::Slot(0,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Sample rate (Hz)")))]
			+ SUniformGridPanel::Slot(1,0)[SNew(SNumericEntryBox<float>).IsEnabled_Lambda(SettingsEnabled).Value_Lambda([this] { return TOptional<float>(Options.SampleRate); }).MinValue(5.f).MaxValue(240.f).OnValueChanged_Lambda([this](float V) { Options.SampleRate = V; })]
			+ SUniformGridPanel::Slot(2,0)[SNew(STextBlock).Text(FText::FromString(TEXT("Ground Z (cm)")))]
			+ SUniformGridPanel::Slot(3,0)[SNew(SNumericEntryBox<float>).IsEnabled_Lambda(SettingsEnabled).Value_Lambda([this] { return TOptional<float>(Options.GroundHeight); }).OnValueChanged_Lambda([this](float V) { Options.GroundHeight = V; })]
			+ SUniformGridPanel::Slot(0,1)[SNew(STextBlock).Text(FText::FromString(TEXT("Left foot bone")))]
			+ SUniformGridPanel::Slot(1,1)[SNew(SEditableTextBox).IsEnabled_Lambda(SettingsEnabled).Text_Lambda([this] { return FText::FromName(Options.LeftFootBone); }).OnTextCommitted_Lambda([this](const FText& V, ETextCommit::Type) { Options.LeftFootBone = FName(*V.ToString()); })]
			+ SUniformGridPanel::Slot(2,1)[SNew(STextBlock).Text(FText::FromString(TEXT("Right foot bone")))]
			+ SUniformGridPanel::Slot(3,1)[SNew(SEditableTextBox).IsEnabled_Lambda(SettingsEnabled).Text_Lambda([this] { return FText::FromName(Options.RightFootBone); }).OnTextCommitted_Lambda([this](const FText& V, ETextCommit::Type) { Options.RightFootBone = FName(*V.ToString()); })]
			+ SUniformGridPanel::Slot(0,2)[SNew(STextBlock).Text(FText::FromString(TEXT("Left sole local Z (cm)")))]
			+ SUniformGridPanel::Slot(1,2)[SNew(SNumericEntryBox<float>).IsEnabled_Lambda(SettingsEnabled).Value_Lambda([this] { return TOptional<float>(Options.LeftSoleOffset.Z); }).OnValueChanged_Lambda([this](float V) { Options.LeftSoleOffset.Z = V; })]
			+ SUniformGridPanel::Slot(2,2)[SNew(STextBlock).Text(FText::FromString(TEXT("Right sole local Z (cm)")))]
			+ SUniformGridPanel::Slot(3,2)[SNew(SNumericEntryBox<float>).IsEnabled_Lambda(SettingsEnabled).Value_Lambda([this] { return TOptional<float>(Options.RightSoleOffset.Z); }).OnValueChanged_Lambda([this](float V) { Options.RightSoleOffset.Z = V; })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SProgressBar).Percent_Lambda([this] { return TOptional<float>(Batch.Progress()); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text(this, &SLiveFootAnalysisWindow::ProgressText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text(this, &SLiveFootAnalysisWindow::ControlRigText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSearchBox).HintText(FText::FromString(TEXT("Filter asset path or error"))).OnTextChanged_Lambda([this](const FText& V) { Search = V.ToString(); Refresh(); })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(this, &SLiveFootAnalysisWindow::FilterText).OnClicked_Lambda([this] { StateFilter = StateFilter >= 5 ? -1 : StateFilter + 1; Refresh(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(this, &SLiveFootAnalysisWindow::SortText).OnClicked_Lambda([this] { Sort = static_cast<ELiveFootSort>((static_cast<int32>(Sort) + 1) % 5); Refresh(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked(ECheckBoxState::Checked).OnCheckStateChanged_Lambda([this](ECheckBoxState V) { bDescending = V == ECheckBoxState::Checked; Refresh(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Descending")))]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState V) { bIssuesOnly = V == ECheckBoxState::Checked; Refresh(); })[SNew(STextBlock).Text(FText::FromString(TEXT("Issues only")))]]
		]
		+ SVerticalBox::Slot().FillHeight(1).MinHeight(220.f).Padding(6)
		[
			SAssignNew(List, SListView<TSharedPtr<FLiveFootAnalysisJob>>).ListItemsSource(&Visible).SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SLiveFootAnalysisWindow::MakeRow)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FLiveFootAnalysisJob> Job, ESelectInfo::Type)
			{
				Selected = Job; SelectedTime = 0.f;
				if (Job && Job->Result)
				{
					for (const auto& Sample : Job->Result->Samples)
					{ if (Sample.Left.bSuspectedSlip || Sample.Right.bSuspectedSlip) { SelectedTime = Sample.Time; break; } }
				}
				if (Preview)
				{
					Preview->SetResult(Selected.IsValid() ? Selected->Result : nullptr);
					Preview->SetTime(SelectedTime);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("Selected time (s): ")))]
			+ SHorizontalBox::Slot().MaxWidth(160)[SNew(SNumericEntryBox<float>).MinValue(0.f).MaxValue_Lambda([this] { return Selected && Selected->Result && Selected->Result->Samples.Num() ? TOptional<float>(Selected->Result->Samples.Last().Time) : TOptional<float>(0.f); }).Value_Lambda([this] { return TOptional<float>(SelectedTime); }).OnValueChanged_Lambda([this](float V) { SelectedTime = FMath::IsFinite(V) ? FMath::Max(0.f, V) : 0.f; if (Preview) { Preview->SetTime(SelectedTime); } })]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Open animation at time"))).OnClicked(this, &SLiveFootAnalysisWindow::OpenAtTime)]
		]
		+ SVerticalBox::Slot().MaxHeight(180).Padding(6)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text(this, &SLiveFootAnalysisWindow::DetailsText)]]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("V1.3 static preview (expand)")))]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("green Contact, blue Swing, red suspected slip, gold ground target")))]
				+ SVerticalBox::Slot().AutoHeight().Padding(2)[SAssignNew(Preview, SLiveFootAnalysisPreview)]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Message); })]
	];
}

SLiveFootAnalysisWindow::~SLiveFootAnalysisWindow() { Batch.Cancel(); }
void SLiveFootAnalysisWindow::Cancel()
{
	Batch.Cancel();
	Selected.Reset();
	SelectedTime = 0.f;
	if (List) { List->ClearSelection(); }
	if (Preview) { Preview->SetResult(nullptr); Preview->SetTime(0.f); }
	Message = TEXT("Queue cancelled. Completed rows retained; unfinished rows marked Cancelled. Preview cleared.");
	Refresh();
}
void SLiveFootAnalysisWindow::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	const bool WasRunning = Batch.IsRunning(); Batch.Tick();
	if ((WasRunning && !Batch.IsRunning()) || CurrentTime - LastRefresh >= 0.2) { LastRefresh = CurrentTime; Refresh(); }
}

void SLiveFootAnalysisWindow::AnalyzeAssets(const TArray<FSoftObjectPath>& Assets)
{
	if (Batch.Start(Assets, Options, Message)) { Selected.Reset(); SelectedTime = 0.f; Message = TEXT("Queue started; sampling yields between chunks."); }
	Refresh();
}
void SLiveFootAnalysisWindow::AnalyzeSelected()
{
	TArray<FAssetData> Assets; FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
	TArray<FSoftObjectPath> Paths;
	for (const auto& Asset : Assets) { if (Asset.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName()) { Paths.Add(Asset.GetSoftObjectPath()); } }
	AnalyzeAssets(Paths);
}
void SLiveFootAnalysisWindow::AnalyzeFolder()
{
	Folder = NormalizeContentFolderPath(Folder);
	if (!Folder.StartsWith(TEXT("/")) || Folder.Contains(TEXT("."))) { Message = TEXT("Use a package folder such as /Game/Characters/Mannequins/Anims/Unarmed"); return; }
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	// Complete any remaining asynchronous registry work here. This keeps a
	// second folder analysis usable without closing and reopening the window.
	if (Registry.IsLoadingAssets())
	{
		Message = TEXT("Waiting for Content Browser asset discovery...");
		Registry.WaitForCompletion();
	}
	FARFilter Filter; Filter.PackagePaths.Add(FName(*Folder)); Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	TArray<FAssetData> Assets; Registry.GetAssets(Filter, Assets);
	TArray<FSoftObjectPath> Paths; for (const auto& Asset : Assets) { Paths.Add(Asset.GetSoftObjectPath()); }
	if (Paths.IsEmpty())
	{
		Message = FString::Printf(TEXT("No Animation Sequences found under %s. Use a Content Browser package folder and wait for asset discovery to finish."), *Folder);
		Refresh();
		return;
	}
	AnalyzeAssets(Paths);
}
void SLiveFootAnalysisWindow::BakeSelected()
{
	if (!Selected || !Selected->Result || !Selected->Result->bSuccess || Batch.IsRunning())
	{
		Message = TEXT("Select a successful, completed result before baking.");
		return;
	}
	UAnimSequence* Sequence = Cast<UAnimSequence>(Selected->Asset.TryLoad());
	if (!Sequence || FLiveFootBatchAnalysis::SourceHash(Sequence) != Selected->SourceHash)
	{
		Message = TEXT("Source changed or is unavailable; rerun analysis before baking.");
		return;
	}
	FLiveFootBakeOptions BakeOptions;
	BakeOptions.OutputFolder = OutputFolder;
	FLiveFootBakeResult BakeResult;
	if (LiveFootAnimationBake::BakeCorrectedCopy(Sequence, Selected->Options, *Selected->Result, BakeOptions, BakeResult))
	{
		Message = FString::Printf(TEXT("Saved corrected copy: %s | corrected keys %d | max correction %.2f cm | slip %.3f -> %.3f s | audit: %s"),
			*BakeResult.AssetPath, BakeResult.CorrectedKeys, BakeResult.MaxCorrection, BakeResult.BeforeSlipDuration, BakeResult.AfterSlipDuration, *BakeResult.AuditFilename);
	}
	else
	{
		Message = TEXT("Bake failed: ") + BakeResult.Error;
	}
}
void SLiveFootAnalysisWindow::Refresh()
{
	FLiveFootBatchAnalysis::FilterAndSort(Batch.GetJobs(), Search, StateFilter, bIssuesOnly, Sort, bDescending, Visible);
	if (Selected && !Visible.Contains(Selected)) { Selected.Reset(); if (List) { List->ClearSelection(); } }
	if (List)
	{
		// SListView normally defers this rebuild. Rebuild now so terminal row
		// states are visible as soon as the queue reaches 100 percent.
		List->RebuildList();
		List->Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
	}
	if (Preview)
	{
		Preview->SetResult(Selected.IsValid() ? Selected->Result : nullptr);
		Preview->SetTime(SelectedTime);
	}
}
FText SLiveFootAnalysisWindow::ProgressText() const
{
	return FText::FromString(FString::Printf(TEXT("%s | terminal %d/%d | visible %d | cache %d | results %.1f MiB / 128 | cache %.1f MiB / 32"),
		Batch.IsRunning() ? TEXT("Analyzing") : TEXT("Idle"), Batch.Completed(), Batch.GetJobs().Num(), Visible.Num(), Batch.CachedCount(),
		Batch.RetainedBytes() / 1048576.0, Batch.CachedBytes() / 1048576.0));
}
FText SLiveFootAnalysisWindow::CancelText() const
{
	return FText::FromString(Batch.IsRunning() ? TEXT("Cancel") : TEXT("Clear preview"));
}
FText SLiveFootAnalysisWindow::SortText() const
{
	const TCHAR* Names[] = {TEXT("Path"), TEXT("Slip sum"), TEXT("Max P95"), TEXT("Contact ratio"), TEXT("Unknown ratio")};
	return FText::FromString(FString(TEXT("Sort: ")) + Names[static_cast<int32>(Sort)]);
}
FText SLiveFootAnalysisWindow::ControlRigText() const
{
	return FText::FromString(FString(TEXT("Control Rig capability: ")) + ControlRigCapability.Message);
}
FText SLiveFootAnalysisWindow::FilterText() const
{ return FText::FromString(FString(TEXT("Status: ")) + (StateFilter < 0 ? TEXT("All") : FLiveFootBatchAnalysis::StateText(static_cast<ELiveFootJobState>(StateFilter)))); }
TSharedRef<ITableRow> SLiveFootAnalysisWindow::MakeRow(TSharedPtr<FLiveFootAnalysisJob> Job, const TSharedRef<STableViewBase>& Table)
{
	FString Text = FString::Printf(TEXT("[%s%s] %s"), FLiveFootBatchAnalysis::StateText(Job->State), Job->bCacheHit ? TEXT(" / cache") : TEXT(""), *Job->Asset.ToString());
	if (Job->Result && Job->Result->bSuccess)
	{
		const auto& L = Job->Result->LeftSummary; const auto& R = Job->Result->RightSummary;
		Text += FString::Printf(TEXT("\nsamples=%d | L slip %.3fs P95 %.2f | R slip %.3fs P95 %.2f | contact %.2f/%.2fs | unknown %.2f/%.2fs"),
			Job->Result->SampleCount, L.SuspectedSlipDuration, L.P95TangentSpeed, R.SuspectedSlipDuration, R.P95TangentSpeed,
			L.ContactDuration, R.ContactDuration, L.UnknownDuration, R.UnknownDuration);
	}
	if (!Job->Error.IsEmpty()) { Text += TEXT("\n") + Job->Error; }
	return SNew(STableRow<TSharedPtr<FLiveFootAnalysisJob>>, Table).Padding(4)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Text))];
}
FText SLiveFootAnalysisWindow::DetailsText() const
{
	if (!Selected) { return FText::FromString(TEXT("Select a result to inspect time/foot/phase; suspected-slip rows select their first detected sample.")); }
	if (!Selected->Result || !Selected->Result->bSuccess) { return FText::FromString(Selected->Error); }
	const auto& Result = *Selected->Result;
	const int32 Index = FMath::Clamp(FMath::RoundToInt(SelectedTime * Selected->Options.SampleRate), 0, Result.Samples.Num() - 1);
	const auto& S = Result.Samples[Index];
	return FText::FromString(FString::Printf(TEXT("%s\nSample t=%.6fs | L %s h=%.3f tangent=%.3f velocity_valid=%d slip_evaluated=%d slip=%d | R %s h=%.3f tangent=%.3f velocity_valid=%d slip_evaluated=%d slip=%d\nConfig %s | Source %s\n%s"),
		*Selected->Asset.ToString(), S.Time, AnimFootDiagnostics::PhaseText(S.Left.Phase), S.Left.Height, S.Left.TangentSpeed,
		S.Left.bValidVelocity, S.Left.bSlipEvaluated, S.Left.bSuspectedSlip, AnimFootDiagnostics::PhaseText(S.Right.Phase), S.Right.Height, S.Right.TangentSpeed,
		S.Right.bValidVelocity, S.Right.bSlipEvaluated, S.Right.bSuspectedSlip, *Selected->ConfigHash, *Selected->SourceHash, *Selected->ConfigText));
}
FReply SLiveFootAnalysisWindow::Export()
{
	FString Directory, Error;
	if (LiveFootReports::Export(Visible, FPaths::ProjectSavedDir() / TEXT("LiveFootAnalysis"), Directory, Error)) { Message = TEXT("Exported visible rows to: ") + FPaths::ConvertRelativePathToFull(Directory); }
	else { Message = TEXT("Export failed: ") + Error; }
	return FReply::Handled();
}
FReply SLiveFootAnalysisWindow::OpenAtTime()
{
	if (!Selected || !Selected->Result || !Selected->Result->bSuccess || !GEditor) { Message = TEXT("Select a successful result first."); return FReply::Handled(); }
	UAnimSequence* Sequence = Cast<UAnimSequence>(Selected->Asset.TryLoad());
	if (!Sequence || FLiveFootBatchAnalysis::SourceHash(Sequence) != Selected->SourceHash) { Message = TEXT("Source changed or is unavailable; rerun analysis before locating a time."); return FReply::Handled(); }
	auto* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Subsystem->OpenEditorForAsset(Sequence)) { Message = TEXT("Could not open Animation Editor."); return FReply::Handled(); }
	auto* Editor = Subsystem->FindEditorForAsset(Sequence, true);
	if (!Editor || Editor->GetEditorName() != FName(TEXT("AnimationEditor"))) { Message = TEXT("Asset editor does not expose the supported Animation Editor interface."); return FReply::Handled(); }
	auto* AnimationEditor = static_cast<IAnimationEditor*>(Editor);
	AnimationEditor->SetAnimationAsset(Sequence);
	auto* Component = AnimationEditor->GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent();
	if (!Component || !Component->PreviewInstance) { Message = TEXT("Preview instance is not ready; try again after the animation editor loads."); return FReply::Handled(); }
	Component->PreviewInstance->SetPlaying(false);
	Component->PreviewInstance->SetPosition(FMath::Clamp(SelectedTime, 0.f, Sequence->GetPlayLength()), false);
	Message = TEXT("Animation Editor paused at selected time. No notifies fired and no asset was saved.");
	return FReply::Handled();
}
