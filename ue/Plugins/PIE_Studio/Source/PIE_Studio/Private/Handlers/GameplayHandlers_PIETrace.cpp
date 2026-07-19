// Unreal Insights trace handlers: trace_start, trace_stop (roadmap item 4b).
// Members of FGameplayHandlers. Wraps FTraceAuxiliary to capture a .utrace the
// user opens in Unreal Insights, and returns its path.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "PIE/PIESequenceFormat.h"

namespace
{
	using UEMCPPIE::SplitCSVLine;

	double Percentile(const TArray<double>& SortedAsc, double P)
	{
		if (SortedAsc.Num() == 0) return 0.0;
		const int32 Idx = FMath::Clamp(FMath::RoundToInt(P * (SortedAsc.Num() - 1)), 0, SortedAsc.Num() - 1);
		return SortedAsc[Idx];
	}
}

namespace
{
	// Path of the trace we most recently started, so trace_stop can report it.
	static FString GLastTracePath;
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTraceStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Channels = OptionalString(Params, TEXT("channels"), TEXT("cpu,frame,bookmark,stats,counters,gpu"));

	FString Dir = OptionalString(Params, TEXT("output_dir"));
	if (Dir.IsEmpty())
	{
		Dir = FPaths::ProjectSavedDir() / TEXT("MCPTraces");
	}
	IFileManager::Get().MakeDirectory(*Dir, true);

	FString Name = OptionalString(Params, TEXT("name"));
	if (Name.IsEmpty())
	{
		Name = FString::Printf(TEXT("pie_%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}
	if (!Name.EndsWith(TEXT(".utrace")))
	{
		Name += TEXT(".utrace");
	}
	const FString Path = FPaths::ConvertRelativePathToFull(Dir / Name);

	FTraceAuxiliary::FOptions Opts;
	Opts.bTruncateFile = true;
	const bool bStarted = FTraceAuxiliary::Start(
		FTraceAuxiliary::EConnectionType::File, *Path, *Channels, &Opts);

	if (!bStarted)
	{
		return MCPError(TEXT("Failed to start trace (a trace may already be active, or tracing is compiled out). Call trace_stop and retry."));
	}

	GLastTracePath = Path;

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("started"), true);
	Result->SetStringField(TEXT("trace_path"), Path);
	Result->SetStringField(TEXT("channels"), Channels);
	Result->SetStringField(TEXT("open_with"), TEXT("Unreal Insights (UnrealInsights.exe) — open the .utrace to inspect CPU/GPU/frame timing"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTraceStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();

	const bool bStopped = FTraceAuxiliary::Stop();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), bStopped);
	if (!GLastTracePath.IsEmpty())
	{
		Result->SetStringField(TEXT("trace_path"), GLastTracePath);
		Result->SetBoolField(TEXT("file_exists"), FPaths::FileExists(GLastTracePath));
	}
	return MCPResult(Result);
}

// Item 4a: aggregate the per-frame perf columns of a recording.csv into a lead:
// frametime avg/p50/p99/max, avg GPU/render/game ms, peak memory, worst hitches.
TSharedPtr<FJsonValue> FGameplayHandlers::PiePerfSummary(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));

	FString Folder;
	if (!RecDir.IsEmpty() && Id.IsEmpty())
	{
		Folder = RecDir;
	}
	else
	{
		const FString Root = RecDir.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("MCPRecordings")) : RecDir;
		Folder = Root / Id;
	}
	Folder.RemoveFromEnd(TEXT("/"));
	Folder.RemoveFromEnd(TEXT("\\"));

	const FString CsvPath = Folder / TEXT("recording.csv");
	FString Csv;
	if (!FFileHelper::LoadFileToString(Csv, *CsvPath))
	{
		return MCPError(FString::Printf(TEXT("recording.csv not found at %s"), *CsvPath));
	}

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines);

	// Locate the header (first non-comment line).
	int32 HeaderIdx = INDEX_NONE;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		if (!Lines[i].StartsWith(TEXT("#"))) { HeaderIdx = i; break; }
	}
	if (HeaderIdx == INDEX_NONE)
	{
		return MCPError(TEXT("recording.csv has no header"));
	}

	const TArray<FString> Cols = SplitCSVLine(Lines[HeaderIdx]);
	auto ColIndex = [&Cols](const TCHAR* Name) -> int32
	{
		for (int32 i = 0; i < Cols.Num(); ++i) { if (Cols[i] == Name) return i; }
		return INDEX_NONE;
	};
	const int32 CDt = ColIndex(TEXT("dt"));
	const int32 CFrame = ColIndex(TEXT("frame"));
	const int32 CTime = ColIndex(TEXT("time"));
	const int32 CGame = ColIndex(TEXT("game_ms"));
	const int32 CRender = ColIndex(TEXT("render_ms"));
	const int32 CGpu = ColIndex(TEXT("gpu_ms"));
	const int32 CMem = ColIndex(TEXT("mem_mb"));

	if (CDt == INDEX_NONE)
	{
		return MCPError(TEXT("recording.csv has no 'dt' column; cannot compute frametime"));
	}

	struct FFrameRow { double FrameMs = 0; uint64 Frame = 0; double Time = 0; };
	TArray<FFrameRow> Rows;
	TArray<double> FrameMs, GameMs, RenderMs, GpuMs;
	double PeakMemMB = 0.0;

	for (int32 i = HeaderIdx + 1; i < Lines.Num(); ++i)
	{
		if (Lines[i].IsEmpty() || Lines[i].StartsWith(TEXT("#"))) continue;
		const TArray<FString> F = SplitCSVLine(Lines[i]);
		if (CDt >= F.Num()) continue;

		FFrameRow R;
		R.FrameMs = FCString::Atod(*F[CDt]) * 1000.0;
		if (CFrame != INDEX_NONE && CFrame < F.Num()) R.Frame = static_cast<uint64>(FCString::Atoi64(*F[CFrame]));
		if (CTime != INDEX_NONE && CTime < F.Num()) R.Time = FCString::Atod(*F[CTime]);
		Rows.Add(R);
		FrameMs.Add(R.FrameMs);

		if (CGame != INDEX_NONE && CGame < F.Num()) GameMs.Add(FCString::Atod(*F[CGame]));
		if (CRender != INDEX_NONE && CRender < F.Num()) RenderMs.Add(FCString::Atod(*F[CRender]));
		if (CGpu != INDEX_NONE && CGpu < F.Num()) GpuMs.Add(FCString::Atod(*F[CGpu]));
		if (CMem != INDEX_NONE && CMem < F.Num()) PeakMemMB = FMath::Max(PeakMemMB, FCString::Atod(*F[CMem]));
	}

	if (FrameMs.Num() == 0)
	{
		return MCPError(TEXT("recording.csv has no data rows"));
	}

	auto Avg = [](const TArray<double>& A) -> double
	{
		if (A.Num() == 0) return 0.0;
		double S = 0; for (double V : A) S += V; return S / A.Num();
	};

	TArray<double> SortedFt = FrameMs;
	SortedFt.Sort();

	// Worst hitches: top 5 frames by frametime.
	TArray<FFrameRow> WorstRows = Rows;
	WorstRows.Sort([](const FFrameRow& A, const FFrameRow& B) { return A.FrameMs > B.FrameMs; });

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("recording"), CsvPath);
	Result->SetNumberField(TEXT("frames"), Rows.Num());

	TSharedRef<FJsonObject> Ft = MakeShared<FJsonObject>();
	Ft->SetNumberField(TEXT("avg_ms"), Avg(FrameMs));
	Ft->SetNumberField(TEXT("p50_ms"), Percentile(SortedFt, 0.50));
	Ft->SetNumberField(TEXT("p99_ms"), Percentile(SortedFt, 0.99));
	Ft->SetNumberField(TEXT("max_ms"), SortedFt.Last());
	Ft->SetNumberField(TEXT("avg_fps"), Avg(FrameMs) > 0.0 ? 1000.0 / Avg(FrameMs) : 0.0);
	Result->SetObjectField(TEXT("frametime"), Ft);

	TSharedRef<FJsonObject> Threads = MakeShared<FJsonObject>();
	Threads->SetNumberField(TEXT("avg_game_ms"), Avg(GameMs));
	Threads->SetNumberField(TEXT("avg_render_ms"), Avg(RenderMs));
	Threads->SetNumberField(TEXT("avg_gpu_ms"), Avg(GpuMs));
	Result->SetObjectField(TEXT("threads"), Threads);
	Result->SetNumberField(TEXT("peak_mem_mb"), PeakMemMB);

	TArray<TSharedPtr<FJsonValue>> Hitches;
	const int32 NHitch = FMath::Min(5, WorstRows.Num());
	for (int32 i = 0; i < NHitch; ++i)
	{
		TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
		H->SetNumberField(TEXT("frame"), static_cast<double>(WorstRows[i].Frame));
		H->SetNumberField(TEXT("time"), WorstRows[i].Time);
		H->SetNumberField(TEXT("frame_ms"), WorstRows[i].FrameMs);
		Hitches.Add(MakeShared<FJsonValueObject>(H));
	}
	Result->SetArrayField(TEXT("worst_hitches"), Hitches);

	return MCPResult(Result);
}
