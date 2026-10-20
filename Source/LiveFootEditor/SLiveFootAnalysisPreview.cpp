#include "SLiveFootAnalysisPreview.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	FLinearColor PhaseColor(const FLiveFootSequenceSample& Sample, bool bLeft)
	{
		const FAnimFootDebugResult& Foot = bLeft ? Sample.Left : Sample.Right;
		if (Foot.bSuspectedSlip) { return FLinearColor(0.95f, 0.12f, 0.08f, 1.f); }
		switch (Foot.Phase)
		{
		case EAnimFootPhase::Contact: return FLinearColor(0.15f, 0.85f, 0.25f, 1.f);
		case EAnimFootPhase::Swing: return FLinearColor(0.20f, 0.55f, 1.f, 1.f);
		default: return FLinearColor(0.65f, 0.65f, 0.65f, 1.f);
		}
	}

	void DrawLine(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
		const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness = 1.f)
	{
		TArray<FVector2f> Points;
		Points.Add(FVector2f(A)); Points.Add(FVector2f(B));
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	FVector2D MapPoint(const FGeometry& Geometry, float Time, float Height, float Duration, float MinHeight, float HeightRange)
	{
		const float Left = 34.f;
		const float Right = 10.f;
		const float Top = 16.f;
		const float Bottom = 24.f;
		const float X = Left + (Duration > KINDA_SMALL_NUMBER ? Time / Duration : 0.f) * FMath::Max(1.f, Geometry.GetLocalSize().X - Left - Right);
		const float Y = Top + (1.f - (Height - MinHeight) / HeightRange) * FMath::Max(1.f, Geometry.GetLocalSize().Y - Top - Bottom);
		return FVector2D(X, Y);
	}
}

void SLiveFootAnalysisPreview::Construct(const FArguments& Arguments)
{
}

void SLiveFootAnalysisPreview::SetResult(TSharedPtr<const FLiveFootSequenceAnalysisResult> InResult)
{
	Result = MoveTemp(InResult);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SLiveFootAnalysisPreview::SetTime(float InTime)
{
	Time = FMath::Max(0.f, FMath::IsFinite(InTime) ? InTime : 0.f);
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SLiveFootAnalysisPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(640.f, 220.f);
}

int32 SLiveFootAnalysisPreview::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.025f, 0.03f, 0.04f, 1.f));
	if (!Result.IsValid() || Result->Samples.Num() == 0) { return LayerId + 1; }

	float Duration = FMath::Max(0.001f, Result->Samples.Last().Time);
	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	for (const FLiveFootSequenceSample& Sample : Result->Samples)
	{
		for (const FAnimFootDebugResult* Foot : {&Sample.Left, &Sample.Right})
		{
			if (Foot->bValidPosition && FMath::IsFinite(Foot->SolePosition.Z)) { MinHeight = FMath::Min(MinHeight, Foot->SolePosition.Z); MaxHeight = FMath::Max(MaxHeight, Foot->SolePosition.Z); }
			if (Foot->bValidGround && FMath::IsFinite(Foot->GroundPoint.Z)) { MinHeight = FMath::Min(MinHeight, Foot->GroundPoint.Z); MaxHeight = FMath::Max(MaxHeight, Foot->GroundPoint.Z); }
		}
	}
	if (!FMath::IsFinite(MinHeight) || !FMath::IsFinite(MaxHeight)) { return LayerId + 1; }
	const float Margin = FMath::Max(2.f, (MaxHeight - MinHeight) * 0.12f);
	MinHeight -= Margin; MaxHeight += Margin;
	const float HeightRange = FMath::Max(1.f, MaxHeight - MinHeight);

	// The graph is a Z-height view, so use an actual sampled ground point for
	// the target line. A fixed world Z=0 would be misleading when the analysis
	// is configured for another plane height.
	float GroundHeight = 0.f;
	bool bHasGround = false;
	for (const FLiveFootSequenceSample& Sample : Result->Samples)
	{
		for (const FAnimFootDebugResult* Foot : {&Sample.Left, &Sample.Right})
		{
			if (Foot->bValidGround && FMath::IsFinite(Foot->GroundPoint.Z))
			{
				GroundHeight = Foot->GroundPoint.Z;
				bHasGround = true;
				break;
			}
		}
		if (bHasGround) { break; }
	}
	const FVector2D GroundA = MapPoint(Geometry, 0.f, GroundHeight, Duration, MinHeight, HeightRange);
	const FVector2D GroundB = MapPoint(Geometry, Duration, GroundHeight, Duration, MinHeight, HeightRange);
	if (bHasGround)
	{
		DrawLine(OutDrawElements, LayerId + 1, Geometry, GroundA, GroundB, FLinearColor(0.85f, 0.75f, 0.15f, 0.8f), 1.5f);
	}

	const float ClampedTime = FMath::Clamp(Time, 0.f, Duration);
	const FVector2D TimeTop = MapPoint(Geometry, ClampedTime, MaxHeight, Duration, MinHeight, HeightRange);
	const FVector2D TimeBottom = MapPoint(Geometry, ClampedTime, MinHeight, Duration, MinHeight, HeightRange);
	DrawLine(OutDrawElements, LayerId + 2, Geometry, TimeTop, TimeBottom, FLinearColor(1.f, 1.f, 1.f, 0.45f), 1.f);

	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Index = 1; Index < Result->Samples.Num(); ++Index)
		{
			const FLiveFootSequenceSample& Previous = Result->Samples[Index - 1];
			const FLiveFootSequenceSample& Current = Result->Samples[Index];
			const FAnimFootDebugResult& PreviousFoot = Side == 0 ? Previous.Left : Previous.Right;
			const FAnimFootDebugResult& CurrentFoot = Side == 0 ? Current.Left : Current.Right;
			if (!PreviousFoot.bValidPosition || !CurrentFoot.bValidPosition) { continue; }
			DrawLine(OutDrawElements, LayerId + 3, Geometry,
				MapPoint(Geometry, Previous.Time, PreviousFoot.SolePosition.Z, Duration, MinHeight, HeightRange),
				MapPoint(Geometry, Current.Time, CurrentFoot.SolePosition.Z, Duration, MinHeight, HeightRange),
				PhaseColor(Current, Side == 0), Side == 0 ? 2.f : 1.5f);
			if (CurrentFoot.bValidGround)
			{
				DrawLine(OutDrawElements, LayerId + 3, Geometry,
					MapPoint(Geometry, Current.Time, CurrentFoot.SolePosition.Z, Duration, MinHeight, HeightRange),
					MapPoint(Geometry, Current.Time, CurrentFoot.GroundPoint.Z, Duration, MinHeight, HeightRange),
					FLinearColor(1.f, 0.65f, 0.1f, 0.55f), 1.f);
			}
		}
	}

	int32 CurrentIndex = 0;
	for (int32 Index = 1; Index < Result->Samples.Num(); ++Index)
	{
		if (Result->Samples[Index].Time <= Time) { CurrentIndex = Index; } else { break; }
	}
	const FLiveFootSequenceSample& Current = Result->Samples[CurrentIndex];
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FAnimFootDebugResult& Foot = Side == 0 ? Current.Left : Current.Right;
		if (!Foot.bValidPosition) { continue; }
		const FVector2D Point = MapPoint(Geometry, Current.Time, Foot.SolePosition.Z, Duration, MinHeight, HeightRange);
		const FVector2D Target = MapPoint(Geometry, Current.Time, Foot.GroundPoint.Z, Duration, MinHeight, HeightRange);
		const FVector2D Size(8.f, 8.f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
			Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(FVector2D(Point.X - Size.X * 0.5f, Point.Y - Size.Y * 0.5f))),
			WhiteBrush, ESlateDrawEffect::None, Side == 0 ? FLinearColor::White : FLinearColor(0.65f, 0.85f, 1.f, 1.f));
		if (Foot.bValidGround)
		{
			DrawLine(OutDrawElements, LayerId + 4, Geometry, Target - FVector2D(4.f, 0.f), Target + FVector2D(4.f, 0.f), FLinearColor(1.f, 0.8f, 0.1f, 1.f), 2.f);
		}
	}
	return LayerId + 5;
}
