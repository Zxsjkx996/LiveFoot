#pragma once

#include "CoreMinimal.h"
#include "LiveFootSequenceAnalysis.h"
#include "Widgets/SLeafWidget.h"

/** Read-only 2D preview of sampled sole height and the configured ground target. */
class SLiveFootAnalysisPreview final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SLiveFootAnalysisPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Arguments);
	void SetResult(TSharedPtr<const FLiveFootSequenceAnalysisResult> InResult);
	void SetTime(float InTime);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& WidgetStyle,
		bool bParentEnabled) const override;

private:
	TSharedPtr<const FLiveFootSequenceAnalysisResult> Result;
	float Time = 0.f;
};
