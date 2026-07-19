// Reproduction-test handlers: test_scaffold, test_run, test_list (item 3a).
// Turns a recording into a durable, self-verifying scenario: a test.json with
// assertions (max drift + max errors). test_run drives the recording (optional)
// and evaluates the finalised drift.json against those assertions, returning
// pass/fail + artifacts. This is the "reproduce + verify a fix" loop.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIESequenceFormat.h"
#include "PIE/PIEInputReplayer.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace UEMCPPIE;

	FString ResolveRecordingFolder(const FString& Id, const FString& RecDir)
	{
		FString Folder;
		if (!RecDir.IsEmpty() && Id.IsEmpty()) Folder = RecDir;
		else Folder = (RecDir.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("MCPRecordings")) : RecDir) / Id;
		Folder.RemoveFromEnd(TEXT("/"));
		Folder.RemoveFromEnd(TEXT("\\"));
		return Folder;
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Str;
		if (!FFileHelper::LoadFileToString(Str, *Path)) return nullptr;
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return nullptr;
		return Obj;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTestScaffold(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));
	const FString Folder = ResolveRecordingFolder(Id, RecDir);

	if (!FPaths::FileExists(Folder / TEXT("recording.csv")))
	{
		return MCPError(FString::Printf(TEXT("No recording.csv in %s; scaffold a test from a real recording"), *Folder));
	}

	const FString Name = OptionalString(Params, TEXT("name"), TEXT("repro"));

	TSharedRef<FJsonObject> Asserts = MakeShared<FJsonObject>();
	Asserts->SetNumberField(TEXT("max_position_drift_cm"), OptionalNumber(Params, TEXT("max_position_drift_cm"), 10.0));
	Asserts->SetNumberField(TEXT("max_velocity_drift_cms"), OptionalNumber(Params, TEXT("max_velocity_drift_cms"), 50.0));
	Asserts->SetNumberField(TEXT("max_rotation_drift_deg"), OptionalNumber(Params, TEXT("max_rotation_drift_deg"), 5.0));
	Asserts->SetNumberField(TEXT("max_errors"), OptionalNumber(Params, TEXT("max_errors"), 0.0));

	TSharedRef<FJsonObject> Scenario = MakeShared<FJsonObject>();
	Scenario->SetStringField(TEXT("name"), Name);
	Scenario->SetStringField(TEXT("source_recording_dir"), Folder);
	Scenario->SetStringField(TEXT("drive"), TEXT("replay"));
	Scenario->SetObjectField(TEXT("assertions"), Asserts);

	const FString TestsDir = Folder / TEXT("tests");
	IFileManager::Get().MakeDirectory(*TestsDir, true);
	const FString Path = TestsDir / (Name + TEXT(".json"));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Scenario, W);
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		return MCPError(FString::Printf(TEXT("Failed to write %s"), *Path));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("test_path"), Path);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetObjectField(TEXT("scenario"), Scenario);
	Result->SetStringField(TEXT("next"), TEXT("Run it with test_run(name, recording_dir); it replays the recording and checks drift + errors against the assertions."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTestList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));

	TArray<FString> Folders;
	if (!Id.IsEmpty() || !RecDir.IsEmpty())
	{
		Folders.Add(ResolveRecordingFolder(Id, RecDir));
	}
	else
	{
		const FString Root = FPaths::ProjectSavedDir() / TEXT("MCPRecordings");
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
		for (const FString& D : Dirs) Folders.Add(Root / D);
	}

	TArray<TSharedPtr<FJsonValue>> Tests;
	for (const FString& Folder : Folders)
	{
		const FString TestsDir = Folder / TEXT("tests");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(TestsDir / TEXT("*.json")), true, false);
		for (const FString& F : Files)
		{
			TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("name"), FPaths::GetBaseFilename(F));
			T->SetStringField(TEXT("path"), TestsDir / F);
			T->SetStringField(TEXT("recording_dir"), Folder);
			Tests.Add(MakeShared<FJsonValueObject>(T));
		}
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("tests"), Tests);
	Result->SetNumberField(TEXT("count"), Tests.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTestRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// Locate the scenario: explicit test_path, or name under a recording's tests/.
	FString TestPath = OptionalString(Params, TEXT("test_path"));
	if (TestPath.IsEmpty())
	{
		const FString Folder = ResolveRecordingFolder(OptionalString(Params, TEXT("recording_id")), OptionalString(Params, TEXT("recording_dir")));
		const FString Name = OptionalString(Params, TEXT("name"), TEXT("repro"));
		TestPath = Folder / TEXT("tests") / (Name + TEXT(".json"));
	}

	TSharedPtr<FJsonObject> Scenario = LoadJson(TestPath);
	if (!Scenario.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Test scenario not found/parseable: %s"), *TestPath));
	}

	FString Source;
	Scenario->TryGetStringField(TEXT("source_recording_dir"), Source);
	const FString DriftPath = Source / TEXT("drift.json");

	const bool bAutoRun = OptionalBool(Params, TEXT("auto_run"), true);
	const bool bDriftExists = FPaths::FileExists(DriftPath);

	// If asked to run and no fresh drift is present, drive the recording now.
	if (bAutoRun && !bDriftExists)
	{
		if (!GEditor) return MCPError(TEXT("test_run auto_run requires the editor"));
		if (GEditor->PlayWorld != nullptr) return MCPError(TEXT("A PIE session is already running; stop it before test_run"));

		FReplayerArmConfig Cfg;
		Cfg.SourceDir = Source;
		Cfg.SourceRecordingId = FPaths::GetCleanFilename(Source);
		Cfg.bAutoStopPIE = true;
		Cfg.bRecordDrift = true;
		FString Err, Msg;
		if (!FPIEInputReplayer::Get().Arm(Cfg, Err, Msg)) return MCPError(Err);
		FRequestPlaySessionParams PlayParams;
		GEditor->RequestPlaySession(PlayParams);

		auto Started = MCPSuccess();
		Started->SetBoolField(TEXT("started"), true);
		Started->SetStringField(TEXT("test_path"), TestPath);
		Started->SetStringField(TEXT("poll"), TEXT("Poll replay_status until pie_active=false, then call test_run again to evaluate."));
		return MCPResult(Started);
	}

	if (!bDriftExists)
	{
		return MCPError(FString::Printf(TEXT("No drift.json at %s; run the replay first (auto_run=true) or replay_run the recording."), *DriftPath));
	}

	// Evaluate assertions against the finalised drift report.
	FDriftReport D;
	FString Err;
	if (!LoadDrift(DriftPath, D, Err))
	{
		return MCPError(FString::Printf(TEXT("Failed to read drift report: %s"), *Err));
	}

	const TSharedPtr<FJsonObject>* A = nullptr;
	Scenario->TryGetObjectField(TEXT("assertions"), A);
	auto Thr = [&A](const TCHAR* Key, double Def) -> double
	{
		double V = Def;
		if (A) (*A)->TryGetNumberField(Key, V);
		return V;
	};
	const double MaxPos = Thr(TEXT("max_position_drift_cm"), 10.0);
	const double MaxVel = Thr(TEXT("max_velocity_drift_cms"), 50.0);
	const double MaxRot = Thr(TEXT("max_rotation_drift_deg"), 5.0);
	const int32 MaxErrors = static_cast<int32>(Thr(TEXT("max_errors"), 0.0));

	TArray<TSharedPtr<FJsonValue>> Failures;
	auto Check = [&Failures](bool bCond, const FString& Msg)
	{
		if (!bCond) Failures.Add(MakeShared<FJsonValueString>(Msg));
	};
	Check(D.MaxPositionDriftCm <= MaxPos,
		FString::Printf(TEXT("position drift %.1f cm > %.1f"), D.MaxPositionDriftCm, MaxPos));
	Check(D.MaxVelocityDriftCms <= MaxVel,
		FString::Printf(TEXT("velocity drift %.1f cm/s > %.1f"), D.MaxVelocityDriftCms, MaxVel));
	Check(D.MaxRotationDriftDeg <= MaxRot,
		FString::Printf(TEXT("rotation drift %.1f deg > %.1f"), D.MaxRotationDriftDeg, MaxRot));
	Check(D.Summary.ErrorsDuringRun <= MaxErrors,
		FString::Printf(TEXT("%d errors logged > %d allowed"), D.Summary.ErrorsDuringRun, MaxErrors));

	const bool bPassed = Failures.Num() == 0;

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("passed"), bPassed);
	Result->SetStringField(TEXT("test_path"), TestPath);
	Result->SetStringField(TEXT("drift_report_path"), DriftPath);
	Result->SetArrayField(TEXT("failures"), Failures);
	Result->SetNumberField(TEXT("max_position_drift_cm"), D.MaxPositionDriftCm);
	Result->SetNumberField(TEXT("max_velocity_drift_cms"), D.MaxVelocityDriftCms);
	Result->SetNumberField(TEXT("errors_during_run"), D.Summary.ErrorsDuringRun);

	// Surface the contact sheet for a visual check when present.
	const FString CapturesDir = Source / TEXT("captures");
	TArray<FString> Sheets;
	IFileManager::Get().FindFiles(Sheets, *(CapturesDir / TEXT("contact_*.jpg")), true, false);
	if (Sheets.Num() > 0) { Sheets.Sort(); Result->SetStringField(TEXT("contact_sheet_path"), CapturesDir / Sheets.Last()); }

	return MCPResult(Result);
}
