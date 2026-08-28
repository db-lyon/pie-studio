// Per-tick actor telemetry handlers (#954): telemetry_start, telemetry_status,
// telemetry_stop. Members of FGameplayHandlers.
//
// The recorder is armed and polled rather than blocking until it is done,
// because a bridge handler runs on the game thread: a handler that blocked
// would stop the very ticks it is trying to sample. That is the same shape the
// observer already uses (arm / status / stop), so the polling loop is one an
// agent driving this plugin has already met.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIETelemetryRecorder.h"
#include "Editor.h"
#include "Engine/World.h"

namespace
{
	using namespace UEMCPPIE;

	// The built-in actor-state fields. Anything not in here is a property path or
	// a getter, which the caller spells out explicitly.
	bool ParseFieldKind(const FString& Raw, ETelemetryChannel& Out)
	{
		const FString F = Raw.ToLower().Replace(TEXT("_"), TEXT(""));
		if (F == TEXT("location") || F == TEXT("position")) { Out = ETelemetryChannel::Location; return true; }
		if (F == TEXT("rotation"))                          { Out = ETelemetryChannel::Rotation; return true; }
		if (F == TEXT("velocity"))                          { Out = ETelemetryChannel::Velocity; return true; }
		if (F == TEXT("speed"))                             { Out = ETelemetryChannel::Speed; return true; }
		if (F == TEXT("grounded"))                          { Out = ETelemetryChannel::Grounded; return true; }
		if (F == TEXT("falling") || F == TEXT("isfalling")) { Out = ETelemetryChannel::Falling; return true; }
		if (F == TEXT("distancetoground"))                  { Out = ETelemetryChannel::DistanceToGround; return true; }
		return false;
	}

	const TCHAR* kKnownFields = TEXT("location | rotation | velocity | speed | grounded | falling | distance_to_ground");

	// An entry is either a bare string (the path/function, doubling as the channel
	// name) or an object that can rename it and point it at another object.
	bool ReadChannelEntry(const TSharedPtr<FJsonValue>& Entry, ETelemetryChannel Kind,
	                      const TCHAR* PathKey, FTelemetryChannelSpec& Out, FString& OutError)
	{
		Out.Kind = Kind;
		if (!Entry.IsValid()) { OutError = TEXT("empty entry"); return false; }

		if (Entry->Type == EJson::String)
		{
			Out.Path = Entry->AsString();
			Out.Name = Out.Path;
		}
		else if (Entry->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& O = Entry->AsObject();
			if (!O->TryGetStringField(PathKey, Out.Path))
			{
				O->TryGetStringField(TEXT("path"), Out.Path);
			}
			if (Out.Path.IsEmpty())
			{
				OutError = FString::Printf(TEXT("entry needs '%s'"), PathKey);
				return false;
			}
			if (!O->TryGetStringField(TEXT("name"), Out.Name) || Out.Name.IsEmpty())
			{
				Out.Name = Out.Path;
			}
			Out.Target = FObjectAddress::FromJson(O);
		}
		else
		{
			OutError = TEXT("entry must be a string or an object");
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonValue> BuildConfig(const TSharedPtr<FJsonObject>& Params, FTelemetryConfig& Out)
	{
		Out.Id = OptionalString(Params, TEXT("id"));
		Out.Subject = FObjectAddress::FromJson(Params);

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;

		if (Params->TryGetArrayField(TEXT("fields"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString Raw;
				if (!V.IsValid() || !V->TryGetString(Raw))
				{
					return MCPError(TEXT("'fields' entries must be strings"));
				}
				FTelemetryChannelSpec Spec;
				if (!ParseFieldKind(Raw, Spec.Kind))
				{
					return MCPError(FString::Printf(TEXT("unknown field '%s' (expected %s)"), *Raw, kKnownFields));
				}
				Spec.Name = Raw;
				Out.Channels.Add(Spec);
			}
		}
		else
		{
			// The default answers the question the verb was built for: did this move
			// leave the ground, how high, and for how long.
			static const TCHAR* const DefaultFields[] = { TEXT("location"), TEXT("velocity"), TEXT("grounded") };
			for (const TCHAR* F : DefaultFields)
			{
				FTelemetryChannelSpec Spec;
				Spec.Name = F;
				ParseFieldKind(F, Spec.Kind);
				Out.Channels.Add(Spec);
			}
		}

		if (Params->TryGetArrayField(TEXT("properties"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FTelemetryChannelSpec Spec;
				FString Err;
				if (!ReadChannelEntry(V, ETelemetryChannel::Property, TEXT("path"), Spec, Err))
				{
					return MCPError(FString::Printf(TEXT("properties: %s"), *Err));
				}
				Out.Channels.Add(Spec);
			}
		}

		if (Params->TryGetArrayField(TEXT("getters"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FTelemetryChannelSpec Spec;
				FString Err;
				if (!ReadChannelEntry(V, ETelemetryChannel::Getter, TEXT("func"), Spec, Err))
				{
					return MCPError(FString::Printf(TEXT("getters: %s"), *Err));
				}
				Out.Channels.Add(Spec);
			}
		}

		Out.DurationSeconds = OptionalNumber(Params, TEXT("duration_seconds"), 2.0);
		Out.MaxSamples = OptionalInt(Params, TEXT("max_samples"), 600);
		Out.IntervalSeconds = OptionalNumber(Params, TEXT("sample_interval_seconds"), 0.0);
		Out.EveryNTicks = OptionalInt(Params, TEXT("every_n_ticks"), 1);
		if (!OptionalBool(Params, TEXT("sample_every_tick"), true) && Out.IntervalSeconds <= 0.0 && Out.EveryNTicks <= 1)
		{
			// sample_every_tick=false with no rate given: fall back to a rate that is
			// still a series but does not sample the maximum.
			Out.EveryNTicks = 2;
		}

		const TSharedPtr<FJsonObject>* CallObj = nullptr;
		if (Params->TryGetObjectField(TEXT("call_on_start"), CallObj) && CallObj)
		{
			const TSharedPtr<FJsonObject>& C = *CallObj;
			if (!C->TryGetStringField(TEXT("func"), Out.StartCall.Func) || Out.StartCall.Func.IsEmpty())
			{
				return MCPError(TEXT("call_on_start needs 'func'"));
			}
			Out.StartCall.bSet = true;
			Out.StartCall.Target = FObjectAddress::FromJson(C);
			const TArray<TSharedPtr<FJsonValue>>* ArgArr = nullptr;
			if (C->TryGetArrayField(TEXT("args"), ArgArr) && ArgArr) Out.StartCall.Args = *ArgArr;
		}

		return nullptr;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTelemetryStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FTelemetryConfig Cfg;
	if (auto E = BuildConfig(Params, Cfg)) return E;

	FString Error, Id;
	if (!FPIETelemetryRecorder::Get().Start(Cfg, Error, Id))
	{
		return MCPError(Error);
	}

	const FTelemetryRun* Run = FPIETelemetryRecorder::Get().Find(Id);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("id"), Id);
	Result->SetStringField(TEXT("state"), TEXT("recording"));
	Result->SetNumberField(TEXT("duration_seconds"), Run ? Run->Config.DurationSeconds : Cfg.DurationSeconds);
	Result->SetNumberField(TEXT("max_samples"), Run ? Run->Config.MaxSamples : Cfg.MaxSamples);
	TArray<TSharedPtr<FJsonValue>> Names;
	for (const FTelemetryChannelSpec& Spec : Cfg.Channels)
	{
		Names.Add(MakeShared<FJsonValueString>(Spec.Name));
	}
	Result->SetArrayField(TEXT("channels"), Names);
	Result->SetStringField(TEXT("next"),
		TEXT("Recording runs on the game thread's tick, so this returned immediately. Poll telemetry_status until state=completed, or call telemetry_stop to end it early."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTelemetryStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Id = OptionalString(Params, TEXT("id"));
	const FTelemetryRun* Run = FPIETelemetryRecorder::Get().Find(Id);
	if (!Run)
	{
		if (Id.IsEmpty())
		{
			return MCPError(TEXT("No telemetry recordings to report. Start one with telemetry_start."));
		}
		return MCPError(FString::Printf(TEXT("No telemetry recording with id '%s'"), *Id));
	}

	// Once a run is finished the samples are the whole point of asking, so they
	// come back by default; while it is still recording they would be a partial
	// series the caller has to poll again anyway.
	const bool bDefaultSamples = Run->State == ETelemetryState::Completed;
	const bool bIncludeSamples = OptionalBool(Params, TEXT("include_samples"), bDefaultSamples);

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("run"), FPIETelemetryRecorder::RunToJson(*Run, bIncludeSamples));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieTelemetryStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString Id = OptionalString(Params, TEXT("id"));
	const int32 Stopped = FPIETelemetryRecorder::Get().Stop(Id);

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("stopped"), Stopped);

	if (const FTelemetryRun* Run = FPIETelemetryRecorder::Get().Find(Id))
	{
		const bool bIncludeSamples = OptionalBool(Params, TEXT("include_samples"), true);
		Result->SetObjectField(TEXT("run"), FPIETelemetryRecorder::RunToJson(*Run, bIncludeSamples));
	}
	return MCPResult(Result);
}
