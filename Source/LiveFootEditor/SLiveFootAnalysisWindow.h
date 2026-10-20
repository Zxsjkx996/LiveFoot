#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "LiveFootBatchAnalysis.h"
#include "LiveFootControlRigPreview.h"

class SLiveFootAnalysisPreview;
class SLiveFootAnalysisWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLiveFootAnalysisWindow) {} SLATE_END_ARGS()
	void Construct(const FArguments& Arguments);
	virtual ~SLiveFootAnalysisWindow() override;
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
	void Cancel();
	void AnalyzeAssets(const TArray<FSoftObjectPath>& Assets);
private:
	void AnalyzeSelected();
	void AnalyzeFolder();
	void BakeSelected();
	void Refresh();
	FReply Export();
	FReply OpenAtTime();
	FText ProgressText() const;
	FText CancelText() const;
	FText DetailsText() const;
	FText ControlRigText() const;
	FText SortText() const;
	FText FilterText() const;
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FLiveFootAnalysisJob> Job, const TSharedRef<STableViewBase>& Table);
	FLiveFootBatchAnalysis Batch;
	FLiveFootSequenceAnalysisOptions Options;
	TArray<TSharedPtr<FLiveFootAnalysisJob>> Visible;
	TSharedPtr<SListView<TSharedPtr<FLiveFootAnalysisJob>>> List;
	TSharedPtr<SLiveFootAnalysisPreview> Preview;
	TSharedPtr<FLiveFootAnalysisJob> Selected;
	FLiveFootControlRigCapability ControlRigCapability;
	FString Folder = TEXT("/Game/Characters/Mannequins/Anims/Unarmed"), OutputFolder = TEXT("/Game/LiveFootGenerated"), Search, Message;
	ELiveFootSort Sort = ELiveFootSort::Path;
	int32 StateFilter = -1;
	bool bIssuesOnly = false, bDescending = true;
	float SelectedTime = 0.f;
	double LastRefresh = 0.0;
};
