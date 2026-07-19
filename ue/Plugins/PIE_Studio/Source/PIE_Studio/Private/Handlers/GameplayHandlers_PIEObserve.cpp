// Observation profile CRUD + observer lifecycle handlers.
// Members of FGameplayHandlers.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "PIE/PIEObserver.h"
#include "PIE/MCPObservationProfile.h"
#include "PIE/PIESequenceFormat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace
{
	using namespace UEMCPPIE;

	FString ObserverStateToString(EObserverState S)
	{
		switch (S)
		{
		case EObserverState::Idle:           return TEXT("idle");
		case EObserverState::Armed:          return TEXT("armed");
		case EObserverState::WaitingForPawn: return TEXT("waiting_for_pawn");
		case EObserverState::Observing:      return TEXT("observing");
		case EObserverState::Completed:      return TEXT("completed");
		}
		return TEXT("idle");
	}

	void WriteObserverStatusFields(TSharedPtr<FJsonObject> R, const FObserverStatus& S)
	{
		R->SetStringField(TEXT("state"), ObserverStateToString(S.State));
		R->SetStringField(TEXT("run_id"), S.RunId);
		R->SetStringField(TEXT("profile"), S.ProfilePath);
		R->SetNumberField(TEXT("frames_sampled"), S.FramesSampled);
		R->SetNumberField(TEXT("elapsed_seconds"), S.ElapsedSeconds);
	}

	void ProfileToJson(const UMCPObservationProfile* P, TSharedPtr<FJsonObject> R)
	{
		TArray<TSharedPtr<FJsonValue>> Vals;
		for (const FMCPTrackedValueEntry& E : P->TrackedValues)
		{
			TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetStringField(TEXT("path"), E.Path);
			V->SetNumberField(TEXT("drift_threshold"), E.DriftThreshold);
			Vals.Add(MakeShared<FJsonValueObject>(V));
		}
		R->SetArrayField(TEXT("tracked_values"), Vals);

		TArray<TSharedPtr<FJsonValue>> Acts;
		for (const FMCPTrackedActorEntry& E : P->TrackedActors)
		{
			Acts.Add(MakeShared<FJsonValueString>(E.ActorId));
		}
		R->SetArrayField(TEXT("tracked_actors"), Acts);

		R->SetBoolField(TEXT("capture_pawn_state"), P->bCapturePawnState);
		R->SetBoolField(TEXT("capture_montage"), P->bCaptureMontage);
		R->SetNumberField(TEXT("position_threshold_cm"), P->PositionThresholdCm);
		R->SetNumberField(TEXT("rotation_threshold_deg"), P->RotationThresholdDeg);
		R->SetNumberField(TEXT("velocity_threshold_cms"), P->VelocityThresholdCms);
		R->SetNumberField(TEXT("tracked_value_default_threshold"), P->TrackedValueDefaultThreshold);
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieProfileCreate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("package_path"), TEXT("/Game/Observation"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Create = MCPCreateAssetIdempotentNewObject<UMCPObservationProfile>(
		Name, PackagePath, OnConflict, TEXT("ObservationProfile"));
	if (Create.EarlyReturn) return Create.EarlyReturn;

	UMCPObservationProfile* Profile = Create.Asset;

	const TArray<TSharedPtr<FJsonValue>>* TVArr = nullptr;
	if (Params->TryGetArrayField(TEXT("tracked_values"), TVArr) && TVArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TVArr)
		{
			if (!V.IsValid()) continue;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Str;
			if (V->TryGetObject(Obj) && Obj)
			{
				FMCPTrackedValueEntry E;
				(*Obj)->TryGetStringField(TEXT("path"), E.Path);
				double Thr = 0;
				if ((*Obj)->TryGetNumberField(TEXT("drift_threshold"), Thr))
					E.DriftThreshold = static_cast<float>(Thr);
				if (!E.Path.IsEmpty()) Profile->TrackedValues.Add(E);
			}
			else if (V->TryGetString(Str))
			{
				FMCPTrackedValueEntry E;
				E.Path = Str;
				Profile->TrackedValues.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TAArr = nullptr;
	if (Params->TryGetArrayField(TEXT("tracked_actors"), TAArr) && TAArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TAArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				FMCPTrackedActorEntry E;
				E.ActorId = S;
				Profile->TrackedActors.Add(E);
			}
		}
	}

	bool BV;
	if (Params->TryGetBoolField(TEXT("capture_pawn_state"), BV)) Profile->bCapturePawnState = BV;
	if (Params->TryGetBoolField(TEXT("capture_montage"), BV))    Profile->bCaptureMontage = BV;

	double D;
	if (Params->TryGetNumberField(TEXT("position_threshold_cm"), D))
		Profile->PositionThresholdCm = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("rotation_threshold_deg"), D))
		Profile->RotationThresholdDeg = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("velocity_threshold_cms"), D))
		Profile->VelocityThresholdCms = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("tracked_value_default_threshold"), D))
		Profile->TrackedValueDefaultThreshold = static_cast<float>(D);

	SaveAssetPackage(Profile);

	const FString AssetPath = PackagePath / Name;
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	ProfileToJson(Profile, Result);
	MCPSetDeleteAssetRollback(Result, AssetPath);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieProfileRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPObservationProfile* Profile = LoadObject<UMCPObservationProfile>(nullptr, *Path);
	if (!Profile) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Path);
	ProfileToJson(Profile, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieProfileUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPObservationProfile* Profile = LoadObject<UMCPObservationProfile>(nullptr, *Path);
	if (!Profile) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Path));

	const TArray<TSharedPtr<FJsonValue>>* TVArr = nullptr;
	if (Params->TryGetArrayField(TEXT("tracked_values"), TVArr) && TVArr)
	{
		Profile->TrackedValues.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TVArr)
		{
			if (!V.IsValid()) continue;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Str;
			if (V->TryGetObject(Obj) && Obj)
			{
				FMCPTrackedValueEntry E;
				(*Obj)->TryGetStringField(TEXT("path"), E.Path);
				double Thr = 0;
				if ((*Obj)->TryGetNumberField(TEXT("drift_threshold"), Thr))
					E.DriftThreshold = static_cast<float>(Thr);
				if (!E.Path.IsEmpty()) Profile->TrackedValues.Add(E);
			}
			else if (V->TryGetString(Str))
			{
				FMCPTrackedValueEntry E;
				E.Path = Str;
				Profile->TrackedValues.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TAArr = nullptr;
	if (Params->TryGetArrayField(TEXT("tracked_actors"), TAArr) && TAArr)
	{
		Profile->TrackedActors.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TAArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				FMCPTrackedActorEntry E;
				E.ActorId = S;
				Profile->TrackedActors.Add(E);
			}
		}
	}

	bool BV;
	if (Params->TryGetBoolField(TEXT("capture_pawn_state"), BV)) Profile->bCapturePawnState = BV;
	if (Params->TryGetBoolField(TEXT("capture_montage"), BV))    Profile->bCaptureMontage = BV;

	double D;
	if (Params->TryGetNumberField(TEXT("position_threshold_cm"), D))
		Profile->PositionThresholdCm = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("rotation_threshold_deg"), D))
		Profile->RotationThresholdDeg = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("velocity_threshold_cms"), D))
		Profile->VelocityThresholdCms = static_cast<float>(D);
	if (Params->TryGetNumberField(TEXT("tracked_value_default_threshold"), D))
		Profile->TrackedValueDefaultThreshold = static_cast<float>(D);

	SaveAssetPackage(Profile);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), Path);
	ProfileToJson(Profile, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieProfileDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	if (!OptionalBool(Params, TEXT("confirm"), false))
	{
		return MCPError(TEXT("confirm=true required to delete a profile"));
	}

	if (!UEditorAssetLibrary::DoesAssetExist(Path))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("already_deleted"), true);
		return MCPResult(Result);
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(Path);
	if (!bDeleted) return MCPError(FString::Printf(TEXT("Failed to delete: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("path"), Path);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieProfileList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPObservationProfile::StaticClass()->GetClassPathName(), Assets, true);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		const FString AP = A.GetObjectPathString();
		if (!AP.StartsWith(Directory)) continue;
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AP);
		Item->SetStringField(TEXT("name"), A.AssetName.ToString());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("profiles"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString ProfilePath;
	if (auto Err = RequireString(Params, TEXT("profile"), ProfilePath)) return Err;

	FObserverArmConfig Cfg;
	Cfg.ProfilePath = ProfilePath;
	Cfg.OutputDir = OptionalString(Params, TEXT("output_dir"));
	Cfg.SampleHz = OptionalInt(Params, TEXT("sample_hz"), 60);
	Cfg.ClientId = OptionalInt(Params, TEXT("client_id"), 0);

	if (Params->HasField(TEXT("pin_fps")))
	{
		int32 Hz = 60;
		Params->TryGetNumberField(TEXT("pin_fps"), Hz);
		Cfg.PinFPS = Hz;
	}

	FString Err, Msg;
	if (!FPIEObserver::Get().Arm(Cfg, Err, Msg))
	{
		return MCPError(Err);
	}

	const FObserverStatus S = FPIEObserver::Get().GetStatus();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("armed"), true);
	Result->SetStringField(TEXT("message"), Msg);
	WriteObserverStatusFields(Result, S);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_observe_disarm"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	if (!FPIEObserver::Get().Disarm(Err)) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FObserverFinishResult F = FPIEObserver::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty()) return MCPError(F.Error);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetStringField(TEXT("run_id"), F.RunId);
	Result->SetStringField(TEXT("output_dir"), F.OutputDir);
	Result->SetNumberField(TEXT("frames_sampled"), F.FramesSampled);
	if (!F.CSVPath.IsEmpty()) Result->SetStringField(TEXT("csv_path"), F.CSVPath);
	if (!F.TrackedActorsPath.IsEmpty()) Result->SetStringField(TEXT("tracked_actors_path"), F.TrackedActorsPath);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FObserverStatus S = FPIEObserver::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteObserverStatusFields(Result, S);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Root = OptionalString(Params, TEXT("output_dir"),
		FPaths::ProjectSavedDir() / TEXT("MCPObservations"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 200);

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);

	Dirs.Sort([](const FString& A, const FString& B) { return A > B; });
	if (Dirs.Num() > Limit) Dirs.SetNum(Limit);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FString& D : Dirs)
	{
		const FString ManifestPath = Root / D / TEXT("manifest.json");
		if (!FPaths::FileExists(ManifestPath)) continue;

		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *ManifestPath)) continue;

		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("run_id"), D);
		FString Profile;
		if (Obj->TryGetStringField(TEXT("profile"), Profile))
			Item->SetStringField(TEXT("profile"), Profile);
		int32 Frames = 0;
		if (Obj->TryGetNumberField(TEXT("frames_sampled"), Frames))
			Item->SetNumberField(TEXT("frames_sampled"), Frames);
		FString StartedAt;
		if (Obj->TryGetStringField(TEXT("started_at"), StartedAt))
			Item->SetStringField(TEXT("started_at"), StartedAt);

		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("observations"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieObserveRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString RunId;
	if (auto Err = RequireString(Params, TEXT("run_id"), RunId)) return Err;

	const FString Root = OptionalString(Params, TEXT("output_dir"),
		FPaths::ProjectSavedDir() / TEXT("MCPObservations"));
	const FString File = OptionalString(Params, TEXT("file"), TEXT("manifest"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 1000);
	const int32 Offset = OptionalInt(Params, TEXT("offset"), 0);

	const FString RunDir = Root / RunId;

	if (File == TEXT("manifest"))
	{
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *(RunDir / TEXT("manifest.json"))))
			return MCPError(FString::Printf(TEXT("manifest.json not found in %s"), *RunDir));

		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
			return MCPError(TEXT("Failed to parse manifest.json"));

		return MCPResult(Obj.ToSharedRef());
	}

	if (File == TEXT("csv"))
	{
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *(RunDir / TEXT("observation.csv"))))
			return MCPError(TEXT("observation.csv not found"));

		TArray<FString> Lines;
		CSV.ParseIntoArrayLines(Lines);
		const int32 Start = FMath::Max(0, Offset);
		const int32 End = FMath::Min(Lines.Num(), Start + Limit);

		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = Start; i < End; ++i)
		{
			Rows.Add(MakeShared<FJsonValueString>(Lines[i]));
		}
		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("rows"), Rows);
		Result->SetNumberField(TEXT("total_lines"), Lines.Num());
		Result->SetNumberField(TEXT("offset"), Start);
		Result->SetNumberField(TEXT("count"), Rows.Num());
		return MCPResult(Result);
	}

	if (File == TEXT("tracked"))
	{
		FString JSONL;
		if (!FFileHelper::LoadFileToString(JSONL, *(RunDir / TEXT("tracked.jsonl"))))
			return MCPError(TEXT("tracked.jsonl not found"));

		TArray<FString> Lines;
		JSONL.ParseIntoArrayLines(Lines);
		const int32 Start = FMath::Max(0, Offset);
		const int32 End = FMath::Min(Lines.Num(), Start + Limit);

		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> Row;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Lines[i]);
			if (FJsonSerializer::Deserialize(R, Row) && Row.IsValid())
			{
				Rows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			}
		}
		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("rows"), Rows);
		Result->SetNumberField(TEXT("total_lines"), Lines.Num());
		Result->SetNumberField(TEXT("offset"), Start);
		Result->SetNumberField(TEXT("count"), Rows.Num());
		return MCPResult(Result);
	}

	if (File == TEXT("series"))
	{
		// Item 2b: turn observation.csv into per-channel time series with
		// min/max/first/last and the first frame that crosses a threshold, so an
		// agent can ask "how did channel X change" and get a curve, not one value.
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *(RunDir / TEXT("observation.csv"))))
			return MCPError(TEXT("observation.csv not found"));

		TArray<FString> Lines;
		CSV.ParseIntoArrayLines(Lines);
		int32 HeaderIdx = INDEX_NONE;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (!Lines[i].StartsWith(TEXT("#"))) { HeaderIdx = i; break; }
		}
		if (HeaderIdx == INDEX_NONE) return MCPError(TEXT("observation.csv has no header"));

		const TArray<FString> Cols = UEMCPPIE::SplitCSVLine(Lines[HeaderIdx]);
		auto ColIndex = [&Cols](const FString& Name) -> int32
		{
			for (int32 i = 0; i < Cols.Num(); ++i) { if (Cols[i] == Name) return i; }
			return INDEX_NONE;
		};
		const int32 CFrame = ColIndex(TEXT("frame"));
		const int32 CTime = ColIndex(TEXT("time"));

		// Requested channels, or every tracked-value / speed column by default.
		TArray<FString> Channels;
		const FString ChParam = OptionalString(Params, TEXT("channels"));
		if (!ChParam.IsEmpty())
		{
			ChParam.ParseIntoArray(Channels, TEXT(","));
			for (FString& C : Channels) { C.TrimStartAndEndInline(); }
		}
		else
		{
			for (const FString& C : Cols)
			{
				if (C.StartsWith(TEXT("t:")) || C == TEXT("speed2d") ||
					C == TEXT("gpu_ms") || C == TEXT("game_ms"))
				{
					Channels.Add(C);
				}
			}
		}

		const bool bHasThreshold = Params->HasField(TEXT("threshold"));
		const double Threshold = OptionalNumber(Params, TEXT("threshold"), 0.0);
		const bool bIncludeValues = OptionalBool(Params, TEXT("include_values"), false);
		const int32 MaxPoints = OptionalInt(Params, TEXT("max_points"), 500);

		struct FChan { int32 Col; TArray<double> Vals; TArray<uint64> Frames; };
		TMap<FString, FChan> Series;
		for (const FString& Ch : Channels)
		{
			const int32 Ci = ColIndex(Ch);
			if (Ci != INDEX_NONE) { FChan& S = Series.Add(Ch); S.Col = Ci; }
		}

		for (int32 i = HeaderIdx + 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty() || Lines[i].StartsWith(TEXT("#"))) continue;
			const TArray<FString> F = UEMCPPIE::SplitCSVLine(Lines[i]);
			const uint64 Fr = (CFrame != INDEX_NONE && CFrame < F.Num())
				? static_cast<uint64>(FCString::Atoi64(*F[CFrame])) : 0;
			for (TPair<FString, FChan>& KV : Series)
			{
				if (KV.Value.Col < F.Num())
				{
					KV.Value.Vals.Add(FCString::Atod(*F[KV.Value.Col]));
					KV.Value.Frames.Add(Fr);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TPair<FString, FChan>& KV : Series)
		{
			const FChan& S = KV.Value;
			if (S.Vals.Num() == 0) continue;
			double MinV = S.Vals[0], MaxV = S.Vals[0];
			int64 FirstOverFrame = -1;
			for (int32 i = 0; i < S.Vals.Num(); ++i)
			{
				MinV = FMath::Min(MinV, S.Vals[i]);
				MaxV = FMath::Max(MaxV, S.Vals[i]);
				if (bHasThreshold && FirstOverFrame < 0 && FMath::Abs(S.Vals[i]) > Threshold)
				{
					FirstOverFrame = static_cast<int64>(S.Frames[i]);
				}
			}
			TSharedRef<FJsonObject> CO = MakeShared<FJsonObject>();
			CO->SetStringField(TEXT("channel"), KV.Key);
			CO->SetNumberField(TEXT("count"), S.Vals.Num());
			CO->SetNumberField(TEXT("min"), MinV);
			CO->SetNumberField(TEXT("max"), MaxV);
			CO->SetNumberField(TEXT("first"), S.Vals[0]);
			CO->SetNumberField(TEXT("last"), S.Vals.Last());
			if (bHasThreshold)
			{
				CO->SetNumberField(TEXT("threshold"), Threshold);
				if (FirstOverFrame >= 0) CO->SetNumberField(TEXT("first_frame_over"), static_cast<double>(FirstOverFrame));
			}
			if (bIncludeValues)
			{
				// Even downsample to MaxPoints so the payload stays bounded.
				TArray<TSharedPtr<FJsonValue>> Vs;
				const int32 N = S.Vals.Num();
				const int32 Step = FMath::Max(1, N / FMath::Max(1, MaxPoints));
				for (int32 i = 0; i < N; i += Step) { Vs.Add(MakeShared<FJsonValueNumber>(S.Vals[i])); }
				CO->SetArrayField(TEXT("values"), Vs);
			}
			Out.Add(MakeShared<FJsonValueObject>(CO));
		}

		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("channels"), Out);
		Result->SetNumberField(TEXT("channel_count"), Out.Num());
		return MCPResult(Result);
	}

	return MCPError(FString::Printf(TEXT("Unknown file type: %s (use manifest, csv, tracked, or series)"), *File));
}
