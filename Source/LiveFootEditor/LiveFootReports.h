#pragma once
#include "CoreMinimal.h"
#include "LiveFootBatchAnalysis.h"

namespace LiveFootReports
{
	bool Export(const TArray<TSharedPtr<FLiveFootAnalysisJob>>& Jobs, const FString& ParentDirectory,
		FString& OutDirectory, FString& OutError);
}
