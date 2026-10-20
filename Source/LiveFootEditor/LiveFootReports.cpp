#include "LiveFootReports.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"

namespace
{
	FString Quote(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return TEXT("\"") + Value + TEXT("\"");
	}
	FString Number(double Value, bool bValid = true)
	{ return bValid && FMath::IsFinite(Value) ? FString::SanitizeFloat(Value, 6) : TEXT("N/A"); }
	FString Bool(bool Value) { return Value ? TEXT("1") : TEXT("0"); }
	void WriteRow(FArchive& File, const TArray<FString>& Fields)
	{
		FString Row;
		for (int32 I = 0; I < Fields.Num(); ++I) { if (I) { Row += TEXT(","); } Row += Quote(Fields[I]); }
		Row += TEXT("\r\n"); FTCHARToUTF8 Utf8(*Row); File.Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	}
	class FCsvFile
	{
	public:
		TUniquePtr<FArchive> File;
		FString Path;
		explicit FCsvFile(const FString& InPath) : Path(InPath)
		{
			File.Reset(IFileManager::Get().CreateFileWriter(*Path));
			if (File) { uint8 Bom[] = {0xEF, 0xBB, 0xBF}; File->Serialize(Bom, 3); }
		}
		bool Close() { return File && !File->IsError() && File->Close(); }
	};
	TArray<FString> Meta(const FLiveFootAnalysisJob& Job)
	{
		return {Job.Asset.ToString(), TEXT("1.2"), TEXT("2"), FEngineVersion::Current().ToString(),
			Job.ConfigHash, Job.SourceHash, TEXT("Sequence raw pose; Skeleton component space; root track retained"),
			Job.ConfigText, Bool(Job.bCacheHit)};
	}
	TArray<FString> MetaHeader()
	{ return {TEXT("asset_path"), TEXT("tool_version"), TEXT("algorithm_version"), TEXT("ue_version"),
		TEXT("config_hash"), TEXT("source_hash"), TEXT("pose_scope"), TEXT("config_snapshot"), TEXT("cache_hit")}; }
	void AddFoot(TArray<FString>& Row, const FAnimFootDebugResult& R)
	{
		Row.Append({Bool(R.bValidPosition), Bool(R.bValidVelocity), Bool(R.bValidGround), Bool(R.bSlipEvaluated),
			Number(R.SolePosition.X, R.bValidPosition), Number(R.SolePosition.Y, R.bValidPosition), Number(R.SolePosition.Z, R.bValidPosition),
			Number(R.Height, R.bValidGround), Number(R.TangentSpeed, R.bValidVelocity && R.bValidGround),
			AnimFootDiagnostics::PhaseText(R.Phase), AnimFootDiagnostics::IssueText(R.Issue),
			R.bSlipEvaluated ? Bool(R.bSuspectedSlip) : TEXT("N/A")});
	}
	bool HasDiagnosticIssue(const FLiveFootAnalysisJob& Job, const FLiveFootSequenceAnalysisResult& Result)
	{
		if (Job.State == ELiveFootJobState::Failed || !Result.bSuccess) { return true; }
		const auto HasSummaryIssue = [](const FAnimFootAnalysisSummary& Summary)
		{
			return Summary.SuspectedSlipDuration > KINDA_SMALL_NUMBER
				|| Summary.UnknownDuration > KINDA_SMALL_NUMBER
				|| (Summary.LastIssue != EAnimFootIssue::None && Summary.LastIssue != EAnimFootIssue::Warmup);
		};
		return HasSummaryIssue(Result.LeftSummary) || HasSummaryIssue(Result.RightSummary);
	}
	FString DiagnosticIssueText(const FLiveFootSequenceAnalysisResult& Result)
	{
		return FString::Printf(TEXT("Issues-only warning: suspected slip L=%.3fs R=%.3fs; unknown L=%.3fs R=%.3fs; last issue L=%s R=%s"),
			Result.LeftSummary.SuspectedSlipDuration, Result.RightSummary.SuspectedSlipDuration,
			Result.LeftSummary.UnknownDuration, Result.RightSummary.UnknownDuration,
			AnimFootDiagnostics::IssueText(Result.LeftSummary.LastIssue), AnimFootDiagnostics::IssueText(Result.RightSummary.LastIssue));
	}
}

bool LiveFootReports::Export(const TArray<TSharedPtr<FLiveFootAnalysisJob>>& Jobs, const FString& ParentDirectory,
	FString& OutDirectory, FString& OutError)
{
	OutDirectory.Reset(); OutError.Reset();
	if (Jobs.IsEmpty()) { OutError = TEXT("No results to export"); return false; }
	const FString Directory = ParentDirectory / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) { OutError = TEXT("Could not create report directory: ") + Directory; return false; }
	FCsvFile Summary(Directory / TEXT("LiveFoot_summary.csv")), Samples(Directory / TEXT("LiveFoot_samples.csv")),
		Errors(Directory / TEXT("LiveFoot_errors.csv")), Intervals(Directory / TEXT("LiveFoot_intervals.csv"));
	if (!Summary.File || !Samples.File || !Errors.File || !Intervals.File)
	{ OutError = TEXT("Could not create report files in ") + Directory; return false; }
	auto Header = MetaHeader();
	Header.Append({TEXT("status"), TEXT("samples"), TEXT("left_contact_s"), TEXT("left_slip_s"), TEXT("left_p95_cm_s"),
		TEXT("right_contact_s"), TEXT("right_slip_s"), TEXT("right_p95_cm_s"), TEXT("left_unknown_s"), TEXT("right_unknown_s"),
		TEXT("left_p95_recent_window"), TEXT("right_p95_recent_window"), TEXT("intervals_may_be_truncated"), TEXT("elapsed_s"), TEXT("error"),
		TEXT("left_mean_cm_s"), TEXT("right_mean_cm_s"), TEXT("left_max_cm_s"), TEXT("right_max_cm_s"),
		TEXT("contact_ratio"), TEXT("unknown_ratio"), TEXT("left_duration_s"), TEXT("right_duration_s")});
	WriteRow(*Summary.File, Header);
	Header = MetaHeader(); Header.Add(TEXT("time_s"));
	for (const FString Side : {FString(TEXT("left_")), FString(TEXT("right_"))})
	{
		for (const TCHAR* Name : {TEXT("valid_position"), TEXT("valid_velocity"), TEXT("valid_ground"), TEXT("slip_evaluated"),
			TEXT("x_cm"), TEXT("y_cm"), TEXT("z_cm"), TEXT("height_cm"), TEXT("tangent_cm_s"), TEXT("phase"), TEXT("issue"), TEXT("suspected_slip")}) { Header.Add(Side + Name); }
	}
	WriteRow(*Samples.File, Header);
	Header = MetaHeader(); Header.Append({TEXT("status"), TEXT("error_or_warning")}); WriteRow(*Errors.File, Header);
	Header = MetaHeader(); Header.Append({TEXT("foot"), TEXT("kind"), TEXT("phase"), TEXT("suspected_slip"), TEXT("start_s"), TEXT("end_s"), TEXT("duration_s"), TEXT("max_tangent_cm_s"), TEXT("issue")});
	WriteRow(*Intervals.File, Header);
	for (const auto& Job : Jobs)
	{
		const bool OK = Job->State == ELiveFootJobState::Succeeded && Job->Result && Job->Result->bSuccess;
		const FLiveFootSequenceAnalysisResult Empty;
		const auto& Result = OK ? *Job->Result : Empty;
		const auto& L = Result.LeftSummary; const auto& R = Result.RightSummary;
		const double Duration = L.SampleDuration + R.SampleDuration;
		auto Row = Meta(*Job);
		Row.Append({FLiveFootBatchAnalysis::StateText(Job->State), OK ? LexToString(Result.SampleCount) : TEXT("0"),
			Number(L.ContactDuration, OK), Number(L.SuspectedSlipDuration, OK), Number(L.P95TangentSpeed, OK && L.bHasTangentSpeed),
			Number(R.ContactDuration, OK), Number(R.SuspectedSlipDuration, OK), Number(R.P95TangentSpeed, OK && R.bHasTangentSpeed),
			Number(L.UnknownDuration, OK), Number(R.UnknownDuration, OK), Bool(L.bP95UsesRecentWindow), Bool(R.bP95UsesRecentWindow),
			Bool(Result.LeftIntervals.Num() >= 64 || Result.RightIntervals.Num() >= 64), Number(Job->ElapsedSeconds), Job->Error,
			Number(L.MeanTangentSpeed, OK && L.bHasTangentSpeed), Number(R.MeanTangentSpeed, OK && R.bHasTangentSpeed),
			Number(L.MaxTangentSpeed, OK && L.bHasTangentSpeed), Number(R.MaxTangentSpeed, OK && R.bHasTangentSpeed),
			Number(Duration > 0 ? (L.ContactDuration + R.ContactDuration) / Duration : 0, OK && Duration > 0),
			Number(Duration > 0 ? (L.UnknownDuration + R.UnknownDuration) / Duration : 0, OK && Duration > 0),
			Number(L.SampleDuration, OK), Number(R.SampleDuration, OK)});
		WriteRow(*Summary.File, Row);
		if (!Job->Error.IsEmpty()) { Row = Meta(*Job); Row.Append({FLiveFootBatchAnalysis::StateText(Job->State), Job->Error}); WriteRow(*Errors.File, Row); }
		else if (HasDiagnosticIssue(*Job, Result))
		{
			// A successful analysis can still have diagnostic findings. Keep these
			// rows separate from FAILED in summary.csv, but make them discoverable
			// in errors.csv for review and Issues-only workflows.
			Row = Meta(*Job); Row.Append({TEXT("WARNING"), DiagnosticIssueText(Result)}); WriteRow(*Errors.File, Row);
		}
		if (!OK) { continue; }
		if (L.bP95UsesRecentWindow || R.bP95UsesRecentWindow || Result.LeftIntervals.Num() >= 64 || Result.RightIntervals.Num() >= 64)
		{
			Row = Meta(*Job); Row.Append({TEXT("WARNING"), TEXT("P95 may use recent 256 velocity samples; intervals retain at most 64 records per foot")}); WriteRow(*Errors.File, Row);
		}
		for (const auto& Sample : Result.Samples)
		{ Row = Meta(*Job); Row.Add(Number(Sample.Time)); AddFoot(Row, Sample.Left); AddFoot(Row, Sample.Right); WriteRow(*Samples.File, Row); }
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (const auto& Interval : Side == 0 ? Result.LeftIntervals : Result.RightIntervals)
			{
				Row = Meta(*Job); Row.Append({Side == 0 ? TEXT("Left") : TEXT("Right"), Interval.Kind == EAnimFootIntervalKind::Phase ? TEXT("Phase") : TEXT("SuspectedSlip"),
					AnimFootDiagnostics::PhaseText(Interval.Phase), Bool(Interval.bSuspectedSlip), Number(Interval.StartTime), Number(Interval.EndTime),
					Number(Interval.Duration), Number(Interval.MaxTangentSpeed), AnimFootDiagnostics::IssueText(Interval.Issue)}); WriteRow(*Intervals.File, Row);
			}
		}
	}
	const bool A = Summary.Close(), B = Samples.Close(), C = Errors.Close(), D = Intervals.Close();
	if (!A || !B || !C || !D) { OutError = TEXT("Report write failed; incomplete report directory: ") + Directory; return false; }
	OutDirectory = Directory; return true;
}
