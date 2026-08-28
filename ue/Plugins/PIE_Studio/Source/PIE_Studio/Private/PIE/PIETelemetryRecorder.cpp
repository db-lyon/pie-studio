#include "PIE/PIETelemetryRecorder.h"
#include "PIE/PIEActorPuppet.h"
#include "PIE/PIESequenceFormat.h"   // ISOTimestampNow
#include "PIE_StudioModule.h"
#include "CollisionQueryParams.h"
#include "Editor.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UnrealType.h"

namespace UEMCPPIE
{
	namespace
	{
		// A recording whose world has stopped advancing (paused PIE, a modal dialog)
		// would never reach its duration cap. The watchdog is what makes the teardown
		// promise unconditional instead of dependent on the world ticking.
		double WatchdogSeconds(const FTelemetryConfig& Cfg)
		{
			return FMath::Min(FPIETelemetryRecorder::kMaxDurationSeconds * 2.0,
				Cfg.DurationSeconds * 4.0 + 30.0);
		}

		TSharedPtr<FJsonValue> VectorJson(const FVector& V)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), V.X);
			O->SetNumberField(TEXT("y"), V.Y);
			O->SetNumberField(TEXT("z"), V.Z);
			return MakeShared<FJsonValueObject>(O);
		}

		TSharedPtr<FJsonValue> RotatorJson(const FRotator& R)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("pitch"), R.Pitch);
			O->SetNumberField(TEXT("yaw"), R.Yaw);
			O->SetNumberField(TEXT("roll"), R.Roll);
			return MakeShared<FJsonValueObject>(O);
		}

		UPawnMovementComponent* MovementOf(UObject* Subject)
		{
			if (APawn* P = Cast<APawn>(Subject)) return P->GetMovementComponent();
			return nullptr;
		}

		// Downward trace from the actor's origin. Null when nothing was hit, so an
		// unsupported subject reads as "not measured" instead of a plausible zero.
		TSharedPtr<FJsonValue> DistanceToGround(UWorld* World, UObject* Subject)
		{
			AActor* Actor = Cast<AActor>(Subject);
			if (!World || !Actor) return MakeShared<FJsonValueNull>();

			const FVector Start = Actor->GetActorLocation();
			const FVector End = Start - FVector(0.0, 0.0, 100000.0);
			FCollisionQueryParams Params(TEXT("PIEStudioTelemetryGround"), /*bTraceComplex=*/false, Actor);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				return MakeShared<FJsonValueNumber>(Start.Z - Hit.ImpactPoint.Z);
			}
			return MakeShared<FJsonValueNull>();
		}

		bool IsNumber(const TSharedPtr<FJsonValue>& V)
		{
			return V.IsValid() && V->Type == EJson::Number;
		}

		// Per-channel summary. Numbers get min/max/first/last with the time each
		// extreme happened; booleans get their transitions and the seconds spent in
		// each state; vectors are summarised component-wise, because a rise height is
		// a question about one component of a location channel.
		void SummariseNumeric(const TArray<TPair<double, double>>& Series, TSharedRef<FJsonObject> Out)
		{
			if (Series.Num() == 0) return;
			double MinV = Series[0].Value, MaxV = Series[0].Value;
			double MinT = Series[0].Key, MaxT = Series[0].Key;
			for (const TPair<double, double>& P : Series)
			{
				if (P.Value < MinV) { MinV = P.Value; MinT = P.Key; }
				if (P.Value > MaxV) { MaxV = P.Value; MaxT = P.Key; }
			}
			Out->SetNumberField(TEXT("first"), Series[0].Value);
			Out->SetNumberField(TEXT("last"), Series.Last().Value);
			Out->SetNumberField(TEXT("min"), MinV);
			Out->SetNumberField(TEXT("min_time"), MinT);
			Out->SetNumberField(TEXT("max"), MaxV);
			Out->SetNumberField(TEXT("max_time"), MaxT);
			Out->SetNumberField(TEXT("delta"), Series.Last().Value - Series[0].Value);
			Out->SetNumberField(TEXT("samples"), Series.Num());
		}
	}

	FPIETelemetryRecorder& FPIETelemetryRecorder::Get()
	{
		static FPIETelemetryRecorder Instance;
		return Instance;
	}

	void FPIETelemetryRecorder::Init()
	{
		if (EndPIEHandle.IsValid()) return;
		EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool bSim)
		{
			this->OnEndPIE(bSim);
		});
	}

	void FPIETelemetryRecorder::Shutdown()
	{
		if (EndPIEHandle.IsValid()) FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
		UnbindEndFrame();
		Runs.Reset();
	}

	void FPIETelemetryRecorder::BindEndFrame()
	{
		if (bEndFrameBound) return;
		EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]() { this->OnEndFrame(); });
		bEndFrameBound = true;
	}

	void FPIETelemetryRecorder::UnbindEndFrame()
	{
		if (!bEndFrameBound) return;
		if (EndFrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
		bEndFrameBound = false;
	}

	bool FPIETelemetryRecorder::HasLiveRuns() const
	{
		for (const FTelemetryRun& R : Runs)
		{
			if (R.State == ETelemetryState::Recording) return true;
		}
		return false;
	}

	bool FPIETelemetryRecorder::Start(const FTelemetryConfig& Cfg, FString& OutError, FString& OutId)
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World) { OutError = TEXT("PIE not running"); return false; }

		int32 Live = 0;
		for (const FTelemetryRun& R : Runs)
		{
			if (R.State == ETelemetryState::Recording) ++Live;
		}
		if (Live >= kMaxConcurrentRuns)
		{
			OutError = FString::Printf(TEXT("%d telemetry recordings are already running; stop one first"), Live);
			return false;
		}

		if (Cfg.Channels.Num() == 0) { OutError = TEXT("no channels requested"); return false; }
		if (Cfg.Channels.Num() > kMaxChannels)
		{
			OutError = FString::Printf(TEXT("too many channels (%d, max %d)"), Cfg.Channels.Num(), kMaxChannels);
			return false;
		}

		FTelemetryRun Run;
		Run.Config = Cfg;
		Run.Config.DurationSeconds = FMath::Clamp(Cfg.DurationSeconds, 0.05, kMaxDurationSeconds);
		Run.Config.MaxSamples = FMath::Clamp(Cfg.MaxSamples, 1, kMaxSamples);
		Run.Config.EveryNTicks = FMath::Max(1, Cfg.EveryNTicks);
		Run.Config.IntervalSeconds = FMath::Max(0.0, Cfg.IntervalSeconds);

		// Resolve the subject and validate every channel now. A channel that cannot
		// resolve is a typo, and a typo that only shows up as a missing column after
		// the run is the failure this whole verb exists to avoid.
		UObject* Subject = nullptr;
		if (!Run.Config.Subject.IsEmpty())
		{
			Subject = FPIEObjectAddress::Resolve(World, Run.Config.Subject, nullptr, OutError);
			if (!Subject) return false;
		}

		auto ResolveChannelTarget = [&](const FTelemetryChannelSpec& Spec, FString& Err) -> UObject*
		{
			if (Spec.Target.IsEmpty())
			{
				if (!Subject) { Err = TEXT("no subject actor given, and the channel names no target of its own"); return nullptr; }
				return Subject;
			}
			return FPIEObjectAddress::Resolve(World, Spec.Target, nullptr, Err);
		};

		for (const FTelemetryChannelSpec& Spec : Run.Config.Channels)
		{
			FString Err;
			UObject* Target = ResolveChannelTarget(Spec, Err);
			if (!Target)
			{
				OutError = FString::Printf(TEXT("channel '%s': %s"), *Spec.Name, *Err);
				return false;
			}

			if (Spec.Kind == ETelemetryChannel::Property)
			{
				TSharedPtr<FJsonValue> Probe;
				if (!FPIEActorPuppet::GetPropertyByPath(Target, Spec.Path, Probe, Err))
				{
					OutError = FString::Printf(TEXT("channel '%s': %s"), *Spec.Name, *Err);
					return false;
				}
			}
			else if (Spec.Kind == ETelemetryChannel::Getter)
			{
				UFunction* Fn = Target->FindFunction(FName(*Spec.Path));
				if (!Fn)
				{
					OutError = FString::Printf(TEXT("channel '%s': function '%s' not found on %s"),
						*Spec.Name, *Spec.Path, *Target->GetClass()->GetName());
					return false;
				}
				// A getter is called once per sample, so it must not need arguments.
				for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					if (It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm)) continue;
					OutError = FString::Printf(
						TEXT("channel '%s': getter '%s' takes arguments; only no-argument functions can be sampled per tick"),
						*Spec.Name, *Spec.Path);
					return false;
				}
			}
			else if (!Cast<AActor>(Target))
			{
				OutError = FString::Printf(TEXT("channel '%s' reads actor state, but the target is %s"),
					*Spec.Name, *Target->GetClass()->GetName());
				return false;
			}
		}

		if (Run.Config.StartCall.bSet)
		{
			FString Err;
			UObject* Target = Run.Config.StartCall.Target.IsEmpty()
				? Subject
				: FPIEObjectAddress::Resolve(World, Run.Config.StartCall.Target, nullptr, Err);
			if (!Target)
			{
				OutError = FString::Printf(TEXT("call_on_start: %s"),
					Err.IsEmpty() ? TEXT("no subject actor given") : *Err);
				return false;
			}
			if (!Target->FindFunction(FName(*Run.Config.StartCall.Func)))
			{
				OutError = FString::Printf(TEXT("call_on_start: function '%s' not found on %s"),
					*Run.Config.StartCall.Func, *Target->GetClass()->GetName());
				return false;
			}
		}

		Run.Config.Id = Cfg.Id.IsEmpty()
			? FString::Printf(TEXT("tel_%d_%s"), NextId, *FDateTime::Now().ToString(TEXT("%H%M%S")))
			: Cfg.Id;
		++NextId;

		if (Find(Run.Config.Id) != nullptr)
		{
			OutError = FString::Printf(TEXT("telemetry id '%s' is already in use"), *Run.Config.Id);
			return false;
		}

		Run.SubjectCache = Subject;
		Run.StartWorldTime = World->GetTimeSeconds();
		Run.StartRealTime = FPlatformTime::Seconds();
		Run.StartedAt = ISOTimestampNow();
		Run.State = ETelemetryState::Recording;

		SweepCompleted();
		Runs.Add(MoveTemp(Run));
		BindEndFrame();

		OutId = Runs.Last().Config.Id;
		UE_LOG(LogPIEStudio, Log, TEXT("[PIE-TEL] Recording %s: %d channels, <= %.2fs / %d samples"),
			*OutId, Cfg.Channels.Num(), Runs.Last().Config.DurationSeconds, Runs.Last().Config.MaxSamples);
		return true;
	}

	void FPIETelemetryRecorder::OnEndFrame()
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;

		for (FTelemetryRun& Run : Runs)
		{
			if (Run.State != ETelemetryState::Recording) continue;

			if (!World)
			{
				FinishRun(Run, TEXT("pie_ended"), nullptr);
				continue;
			}

			const double RealElapsed = FPlatformTime::Seconds() - Run.StartRealTime;
			if (RealElapsed > WatchdogSeconds(Run.Config))
			{
				FinishRun(Run, TEXT("watchdog"), World);
				continue;
			}

			++Run.TicksSeen;
			const double Now = World->GetTimeSeconds() - Run.StartWorldTime;

			bool bDue = false;
			if (Run.Config.IntervalSeconds > 0.0)
			{
				bDue = (Run.LastSampleTime < 0.0) || (Now - Run.LastSampleTime >= Run.Config.IntervalSeconds);
			}
			else
			{
				bDue = ((Run.TicksSeen - 1) % Run.Config.EveryNTicks) == 0;
			}

			if (bDue)
			{
				UObject* Subject = Run.SubjectCache.Get();
				if (!Subject && !Run.Config.Subject.IsEmpty())
				{
					FString Err;
					Subject = FPIEObjectAddress::Resolve(World, Run.Config.Subject, nullptr, Err);
					if (!Subject)
					{
						FinishRun(Run, TEXT("subject_lost"), World);
						continue;
					}
					Run.SubjectCache = Subject;
				}

				FTelemetrySample Sample;
				Sample.Index = Run.Samples.Num();
				Sample.Frame = static_cast<uint64>(GFrameCounter);
				Sample.Time = Now;
				Sample.Dt = World->GetDeltaSeconds();

				for (const FTelemetryChannelSpec& Spec : Run.Config.Channels)
				{
					UObject* Target = Subject;
					if (!Spec.Target.IsEmpty())
					{
						FString Err;
						Target = FPIEObjectAddress::Resolve(World, Spec.Target, nullptr, Err);
					}
					if (!Target)
					{
						Sample.Values.Add(Spec.Name, MakeShared<FJsonValueNull>());
						continue;
					}

					AActor* Actor = Cast<AActor>(Target);
					TSharedPtr<FJsonValue> Value;
					switch (Spec.Kind)
					{
					case ETelemetryChannel::Location:
						if (Actor) Value = VectorJson(Actor->GetActorLocation());
						break;
					case ETelemetryChannel::Rotation:
						if (Actor) Value = RotatorJson(Actor->GetActorRotation());
						break;
					case ETelemetryChannel::Velocity:
						if (Actor) Value = VectorJson(Actor->GetVelocity());
						break;
					case ETelemetryChannel::Speed:
						if (Actor) Value = MakeShared<FJsonValueNumber>(Actor->GetVelocity().Size());
						break;
					case ETelemetryChannel::Grounded:
					{
						if (UPawnMovementComponent* M = MovementOf(Target))
						{
							Value = MakeShared<FJsonValueBoolean>(M->IsMovingOnGround());
						}
						break;
					}
					case ETelemetryChannel::Falling:
					{
						if (UPawnMovementComponent* M = MovementOf(Target))
						{
							Value = MakeShared<FJsonValueBoolean>(M->IsFalling());
						}
						break;
					}
					case ETelemetryChannel::DistanceToGround:
						Value = DistanceToGround(World, Target);
						break;
					case ETelemetryChannel::Property:
					{
						FString Err;
						if (!FPIEActorPuppet::GetPropertyByPath(Target, Spec.Path, Value, Err))
						{
							Value.Reset();
						}
						break;
					}
					case ETelemetryChannel::Getter:
					{
						FString Err;
						TSharedPtr<FJsonValue> Returned;
						const TArray<TSharedPtr<FJsonValue>> NoArgs;
						if (FPIEActorPuppet::CallFunctionWithResult(Target, Spec.Path, NoArgs, Returned, Err))
						{
							Value = Returned;
						}
						break;
					}
					}

					// A channel that could not be read this tick is null, never a
					// plausible zero: a flat line that reads like real data is worse
					// than a gap that admits it is one.
					if (!Value.IsValid())
					{
						Value = MakeShared<FJsonValueNull>();
					}
					Sample.Values.Add(Spec.Name, Value);
				}

				Run.LastSampleTime = Now;
				Run.Samples.Add(MoveTemp(Sample));

				// The start call fires after sample 0, so the series always carries a
				// baseline taken before the thing under test was kicked off.
				if (Run.Config.StartCall.bSet && !Run.bStartCallFired)
				{
					Run.bStartCallFired = true;
					FString Err;
					UObject* Target = Run.Config.StartCall.Target.IsEmpty()
						? Subject
						: FPIEObjectAddress::Resolve(World, Run.Config.StartCall.Target, nullptr, Err);
					if (!Target)
					{
						Run.StartCallError = Err;
					}
					else if (!FPIEActorPuppet::CallFunction(Target, Run.Config.StartCall.Func, Run.Config.StartCall.Args, Err))
					{
						Run.StartCallError = Err;
					}
				}

				if (Run.Samples.Num() >= Run.Config.MaxSamples)
				{
					FinishRun(Run, TEXT("max_samples"), World);
					continue;
				}
			}

			if (Now >= Run.Config.DurationSeconds)
			{
				FinishRun(Run, TEXT("duration"), World);
			}
		}

		if (!HasLiveRuns())
		{
			UnbindEndFrame();
		}
	}

	void FPIETelemetryRecorder::OnEndPIE(bool /*bIsSimulating*/)
	{
		for (FTelemetryRun& Run : Runs)
		{
			if (Run.State == ETelemetryState::Recording)
			{
				FinishRun(Run, TEXT("pie_ended"), GEditor ? GEditor->PlayWorld : nullptr);
			}
		}
		UnbindEndFrame();
	}

	void FPIETelemetryRecorder::FinishRun(FTelemetryRun& Run, const FString& Reason, UWorld* World)
	{
		if (Run.State == ETelemetryState::Completed) return;
		Run.State = ETelemetryState::Completed;
		Run.StopReason = Reason;
		Run.EndWorldTime = World ? (World->GetTimeSeconds() - Run.StartWorldTime)
		                         : (Run.Samples.Num() > 0 ? Run.Samples.Last().Time : 0.0);
		Run.SubjectCache.Reset();
		UE_LOG(LogPIEStudio, Log, TEXT("[PIE-TEL] %s finished (%s): %d samples over %.3fs"),
			*Run.Config.Id, *Reason, Run.Samples.Num(), Run.EndWorldTime);
	}

	int32 FPIETelemetryRecorder::Stop(const FString& Id)
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		int32 Stopped = 0;
		for (FTelemetryRun& Run : Runs)
		{
			if (Run.State != ETelemetryState::Recording) continue;
			if (!Id.IsEmpty() && Run.Config.Id != Id) continue;
			FinishRun(Run, TEXT("stopped"), World);
			++Stopped;
		}
		if (!HasLiveRuns()) UnbindEndFrame();
		return Stopped;
	}

	const FTelemetryRun* FPIETelemetryRecorder::Find(const FString& Id) const
	{
		if (Id.IsEmpty())
		{
			return Runs.Num() > 0 ? &Runs.Last() : nullptr;
		}
		for (const FTelemetryRun& Run : Runs)
		{
			if (Run.Config.Id == Id) return &Run;
		}
		return nullptr;
	}

	void FPIETelemetryRecorder::SweepCompleted()
	{
		int32 Completed = 0;
		for (const FTelemetryRun& Run : Runs)
		{
			if (Run.State == ETelemetryState::Completed) ++Completed;
		}
		// Finished runs are kept so a poll after completion can still read the
		// series, but only the most recent few: the samples are held in memory.
		for (int32 i = 0; i < Runs.Num() && Completed > kKeptCompletedRuns; )
		{
			if (Runs[i].State == ETelemetryState::Completed)
			{
				Runs.RemoveAt(i);
				--Completed;
			}
			else
			{
				++i;
			}
		}
	}

	TSharedRef<FJsonObject> FPIETelemetryRecorder::RunToJson(const FTelemetryRun& Run, bool bIncludeSamples)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("id"), Run.Config.Id);
		Out->SetStringField(TEXT("state"), Run.State == ETelemetryState::Recording ? TEXT("recording") : TEXT("completed"));
		Out->SetStringField(TEXT("started_at"), Run.StartedAt);
		Out->SetNumberField(TEXT("sample_count"), Run.Samples.Num());
		Out->SetNumberField(TEXT("duration_seconds"), Run.Config.DurationSeconds);
		Out->SetNumberField(TEXT("max_samples"), Run.Config.MaxSamples);
		Out->SetNumberField(TEXT("elapsed_seconds"),
			Run.State == ETelemetryState::Completed
				? Run.EndWorldTime
				: (Run.Samples.Num() > 0 ? Run.Samples.Last().Time : 0.0));
		if (!Run.StopReason.IsEmpty()) Out->SetStringField(TEXT("stop_reason"), Run.StopReason);
		if (!Run.Config.Subject.IsEmpty()) Out->SetStringField(TEXT("subject"), Run.Config.Subject.Describe());
		if (Run.Config.StartCall.bSet)
		{
			Out->SetBoolField(TEXT("call_on_start_fired"), Run.bStartCallFired);
			if (!Run.StartCallError.IsEmpty()) Out->SetStringField(TEXT("call_on_start_error"), Run.StartCallError);
		}

		TArray<TSharedPtr<FJsonValue>> ChannelNames;
		for (const FTelemetryChannelSpec& Spec : Run.Config.Channels)
		{
			ChannelNames.Add(MakeShared<FJsonValueString>(Spec.Name));
		}
		Out->SetArrayField(TEXT("channels"), ChannelNames);

		if (bIncludeSamples)
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			Rows.Reserve(Run.Samples.Num());
			for (const FTelemetrySample& S : Run.Samples)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("index"), S.Index);
				Row->SetNumberField(TEXT("frame"), static_cast<double>(S.Frame));
				Row->SetNumberField(TEXT("time"), S.Time);
				Row->SetNumberField(TEXT("dt"), S.Dt);
				for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : S.Values)
				{
					Row->SetField(KV.Key, KV.Value);
				}
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}
			Out->SetArrayField(TEXT("samples"), Rows);
		}

		// Summary. Built from the same rows the caller gets, so the two can be
		// checked against each other rather than taken on trust.
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		for (const FTelemetryChannelSpec& Spec : Run.Config.Channels)
		{
			TSharedRef<FJsonObject> Chan = MakeShared<FJsonObject>();

			TArray<TPair<double, double>> Scalar;
			TMap<FString, TArray<TPair<double, double>>> Components;
			TArray<TPair<double, bool>> Bools;

			for (const FTelemetrySample& S : Run.Samples)
			{
				const TSharedPtr<FJsonValue>* Found = S.Values.Find(Spec.Name);
				if (!Found || !Found->IsValid()) continue;
				const TSharedPtr<FJsonValue>& V = *Found;
				if (V->Type == EJson::Number)
				{
					Scalar.Add(TPair<double, double>(S.Time, V->AsNumber()));
				}
				else if (V->Type == EJson::Boolean)
				{
					Bools.Add(TPair<double, bool>(S.Time, V->AsBool()));
				}
				else if (V->Type == EJson::Object)
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : V->AsObject()->Values)
					{
						if (!IsNumber(KV.Value)) continue;
						Components.FindOrAdd(KV.Key).Add(TPair<double, double>(S.Time, KV.Value->AsNumber()));
					}
				}
			}

			if (Scalar.Num() > 0)
			{
				SummariseNumeric(Scalar, Chan);
			}
			else if (Components.Num() > 0)
			{
				for (const TPair<FString, TArray<TPair<double, double>>>& KV : Components)
				{
					TSharedRef<FJsonObject> Comp = MakeShared<FJsonObject>();
					SummariseNumeric(KV.Value, Comp);
					Chan->SetObjectField(KV.Key, Comp);
				}
			}
			else if (Bools.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Transitions;
				double TrueSeconds = 0.0, FalseSeconds = 0.0;
				for (int32 i = 0; i < Bools.Num(); ++i)
				{
					if (i > 0)
					{
						const double Span = Bools[i].Key - Bools[i - 1].Key;
						(Bools[i - 1].Value ? TrueSeconds : FalseSeconds) += Span;
						if (Bools[i].Value != Bools[i - 1].Value)
						{
							TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
							T->SetNumberField(TEXT("time"), Bools[i].Key);
							T->SetBoolField(TEXT("to"), Bools[i].Value);
							Transitions.Add(MakeShared<FJsonValueObject>(T));
						}
					}
				}
				Chan->SetBoolField(TEXT("first"), Bools[0].Value);
				Chan->SetBoolField(TEXT("last"), Bools.Last().Value);
				Chan->SetNumberField(TEXT("true_seconds"), TrueSeconds);
				Chan->SetNumberField(TEXT("false_seconds"), FalseSeconds);
				Chan->SetArrayField(TEXT("transitions"), Transitions);
				Chan->SetNumberField(TEXT("samples"), Bools.Num());
			}
			else
			{
				Chan->SetNumberField(TEXT("samples"), 0);
			}

			Summary->SetObjectField(Spec.Name, Chan);
		}
		Out->SetObjectField(TEXT("summary"), Summary);

		return Out;
	}
}
