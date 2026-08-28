// Per-tick actor telemetry (#954).
//
// level(read_actor_motion) answers "where is this actor now". Verifying that a
// jump produced a real airborne arc, that an impulse settled, that a blend
// finished, or that a volume was dwelt in needs the same read taken every tick
// and kept with its timestamps. That is a time series, so it lives here rather
// than in core.
//
// The recorder is bounded twice over (a duration cap and a sample cap, both
// clamped), and torn down on every exit: completion, an explicit stop, EndPIE,
// module shutdown, and a wall-clock watchdog for the case where the world stops
// advancing while the editor keeps ticking. The failure mode this exists to
// remove is a hand-rolled tick callback that outlives the call that registered
// it, so nothing here can be left running by returning early.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "PIE/PIEObjectAddress.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;

namespace UEMCPPIE
{
	enum class ETelemetryState : uint8
	{
		Recording,
		Completed
	};

	enum class ETelemetryChannel : uint8
	{
		Location,
		Rotation,
		Velocity,
		Speed,
		Grounded,
		Falling,
		DistanceToGround,
		Property,
		Getter
	};

	struct FTelemetryChannelSpec
	{
		FString Name;                 // key in each sample
		ETelemetryChannel Kind = ETelemetryChannel::Property;
		FString Path;                 // dotted property path, or function name
		FObjectAddress Target;        // empty = the recording's subject
	};

	struct FTelemetryStartCall
	{
		bool bSet = false;
		FObjectAddress Target;        // empty = the recording's subject
		FString Func;
		TArray<TSharedPtr<FJsonValue>> Args;
	};

	struct FTelemetryConfig
	{
		FString Id;
		FObjectAddress Subject;
		TArray<FTelemetryChannelSpec> Channels;
		FTelemetryStartCall StartCall;

		double DurationSeconds = 2.0;
		int32 MaxSamples = 600;
		int32 EveryNTicks = 1;        // used when IntervalSeconds is 0
		double IntervalSeconds = 0.0; // > 0 wins over EveryNTicks
	};

	struct FTelemetrySample
	{
		int32 Index = 0;
		uint64 Frame = 0;
		double Time = 0.0;      // world seconds since the recording attached
		double Dt = 0.0;
		TMap<FString, TSharedPtr<FJsonValue>> Values;
	};

	struct FTelemetryRun
	{
		FTelemetryConfig Config;
		ETelemetryState State = ETelemetryState::Recording;
		FString StopReason;             // duration | max_samples | stopped | pie_ended | watchdog | subject_lost
		FString StartedAt;
		TArray<FTelemetrySample> Samples;

		TWeakObjectPtr<UObject> SubjectCache;
		double StartWorldTime = 0.0;
		double StartRealTime = 0.0;
		double LastSampleTime = -1.0;
		double EndWorldTime = 0.0;
		int32 TicksSeen = 0;
		bool bStartCallFired = false;
		FString StartCallError;
	};

	class FPIETelemetryRecorder
	{
	public:
		static FPIETelemetryRecorder& Get();

		// Ceilings the caller cannot raise. A recorder that samples every tick is
		// cheap per frame and ruinous if it is left on, so the bound is structural
		// rather than advisory.
		static constexpr double kMaxDurationSeconds = 60.0;
		static constexpr int32  kMaxSamples = 20000;
		static constexpr int32  kMaxChannels = 32;
		static constexpr int32  kMaxConcurrentRuns = 4;
		static constexpr int32  kKeptCompletedRuns = 8;

		void Init();
		void Shutdown();

		// Live PIE required. Validates every channel and the start call against the
		// subject before a single tick is taken, so a typo fails here rather than
		// silently producing an empty column.
		bool Start(const FTelemetryConfig& Cfg, FString& OutError, FString& OutId);

		// Stop one run by id, or every live run when Id is empty. Returns how many
		// were stopped.
		int32 Stop(const FString& Id);

		// Find a run by id, or the most recent one when Id is empty.
		const FTelemetryRun* Find(const FString& Id) const;
		bool HasLiveRuns() const;

		// Serialise a run: state, the sample series, and the per-channel summary a
		// caller would otherwise recompute (min/max/first/last with the time each
		// extreme happened, and for boolean channels the transitions plus how long
		// was spent true and false, which is what an air-time answer is made of).
		static TSharedRef<FJsonObject> RunToJson(const FTelemetryRun& Run, bool bIncludeSamples);

	private:
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		void BindEndFrame();
		void UnbindEndFrame();
		void FinishRun(FTelemetryRun& Run, const FString& Reason, UWorld* World);
		void SweepCompleted();

		TArray<FTelemetryRun> Runs;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle EndFrameHandle;
		bool bEndFrameBound = false;
		int32 NextId = 1;
	};
}
