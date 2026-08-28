// Bounded activate-and-sample loop (#955): sample_loop. A member of
// FGameplayHandlers.
//
// invoke_object_function makes one call. Verifying a combat mechanic meant
// looping an ability activation while reading struct fields off two live
// gameplay records between every turn, which is a loop plus a per-turn read of
// struct-returning state - neither of which a single call can express.
//
// The loop runs inside one game-thread dispatch, so nothing ticks between a
// call and the read that follows it: turn N's snapshot is the state that call
// produced, not the state some later frame settled into.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIEActorPuppet.h"
#include "PIE/PIEObjectAddress.h"
#include "PIE/PIESequenceRunner.h"
#include "Editor.h"
#include "Engine/World.h"

namespace
{
	using namespace UEMCPPIE;

	constexpr int32 kMaxSampleEntries = 32;
	constexpr int32 kMaxInvokesPerTurn = 8;

	// One thing to read each turn: an address plus a dotted property path, named
	// so the per-turn snapshot is keyed by something the caller chose.
	struct FSampleEntry
	{
		FString Name;
		FObjectAddress Target;
		FString Property;
	};

	// One thing to invoke each turn.
	struct FInvokeEntry
	{
		FObjectAddress Target;
		FString Func;
		TArray<TSharedPtr<FJsonValue>> Args;
	};

	bool ReadInvoke(const TSharedPtr<FJsonValue>& V, FInvokeEntry& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O)
		{
			OutError = TEXT("entry must be an object");
			return false;
		}
		Out.Target = FObjectAddress::FromJson(*O);
		if (Out.Target.IsEmpty())
		{
			OutError = TEXT("entry needs a target (actor, subsystem, objectPath or ref)");
			return false;
		}
		if (!(*O)->TryGetStringField(TEXT("func"), Out.Func) || Out.Func.IsEmpty())
		{
			OutError = TEXT("entry needs 'func'");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* ArgArr = nullptr;
		if ((*O)->TryGetArrayField(TEXT("args"), ArgArr) && ArgArr) Out.Args = *ArgArr;
		return true;
	}

	bool ReadSample(const TSharedPtr<FJsonValue>& V, FSampleEntry& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O)
		{
			OutError = TEXT("entry must be an object");
			return false;
		}
		Out.Target = FObjectAddress::FromJson(*O);
		if (Out.Target.IsEmpty())
		{
			OutError = TEXT("entry needs a target (actor, subsystem, objectPath or ref)");
			return false;
		}
		if (!(*O)->TryGetStringField(TEXT("property"), Out.Property) || Out.Property.IsEmpty())
		{
			OutError = TEXT("entry needs 'property'");
			return false;
		}
		if (!(*O)->TryGetStringField(TEXT("name"), Out.Name) || Out.Name.IsEmpty())
		{
			Out.Name = Out.Property;
		}
		return true;
	}

	// Read every sample entry once. Struct-valued properties come back as JSON
	// objects, which is what makes a snapshot of a gameplay record comparable turn
	// to turn instead of a stringified blob.
	TSharedRef<FJsonObject> TakeSnapshot(FSequenceContext& Ctx, const TArray<FSampleEntry>& Entries)
	{
		TSharedRef<FJsonObject> Snap = MakeShared<FJsonObject>();
		for (const FSampleEntry& E : Entries)
		{
			FString Err;
			UObject* Target = FPIESequenceRunner::ResolveTarget(Ctx, E.Target, Err);
			if (!Target)
			{
				TSharedRef<FJsonObject> Failed = MakeShared<FJsonObject>();
				Failed->SetStringField(TEXT("error"), Err);
				Snap->SetObjectField(E.Name, Failed);
				continue;
			}
			TSharedPtr<FJsonValue> Value;
			if (FPIEActorPuppet::GetPropertyByPath(Target, E.Property, Value, Err) && Value.IsValid())
			{
				Snap->SetField(E.Name, Value);
			}
			else
			{
				TSharedRef<FJsonObject> Failed = MakeShared<FJsonObject>();
				Failed->SetStringField(TEXT("error"), Err);
				Snap->SetObjectField(E.Name, Failed);
			}
		}
		return Snap;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieSampleLoop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	int32 Iterations = 0;
	if (!Params->TryGetNumberField(TEXT("iterations"), Iterations))
	{
		return MCPError(TEXT("'iterations' is required"));
	}
	if (Iterations < 1 || Iterations > FPIESequenceRunner::kMaxLoopIterations)
	{
		return MCPError(FString::Printf(TEXT("'iterations' must be 1..%d"), FPIESequenceRunner::kMaxLoopIterations));
	}

	// Invocations: one object, or an array of them when a turn drives more than
	// one record.
	TArray<FInvokeEntry> Invokes;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		const TSharedPtr<FJsonObject>* One = nullptr;
		if (Params->TryGetArrayField(TEXT("invokes"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FInvokeEntry E;
				FString Err;
				if (!ReadInvoke(V, E, Err)) return MCPError(FString::Printf(TEXT("invokes: %s"), *Err));
				Invokes.Add(MoveTemp(E));
			}
		}
		else if (Params->TryGetObjectField(TEXT("invoke"), One) && One)
		{
			FInvokeEntry E;
			FString Err;
			if (!ReadInvoke(MakeShared<FJsonValueObject>(*One), E, Err))
			{
				return MCPError(FString::Printf(TEXT("invoke: %s"), *Err));
			}
			Invokes.Add(MoveTemp(E));
		}
		else
		{
			return MCPError(TEXT("'invoke' (an object) or 'invokes' (an array) is required"));
		}
	}
	if (Invokes.Num() > kMaxInvokesPerTurn)
	{
		return MCPError(FString::Printf(TEXT("too many invocations per turn (%d, max %d)"), Invokes.Num(), kMaxInvokesPerTurn));
	}

	TArray<FSampleEntry> Samples;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Params->TryGetArrayField(TEXT("sample"), Arr) || !Arr || Arr->Num() == 0)
		{
			return MCPError(TEXT("'sample' is required (an array of {target, property, name?} to snapshot each turn)"));
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FSampleEntry E;
			FString Err;
			if (!ReadSample(V, E, Err)) return MCPError(FString::Printf(TEXT("sample: %s"), *Err));
			Samples.Add(MoveTemp(E));
		}
	}
	if (Samples.Num() > kMaxSampleEntries)
	{
		return MCPError(FString::Printf(TEXT("too many sample entries (%d, max %d)"), Samples.Num(), kMaxSampleEntries));
	}

	FStepCondition StopWhen;
	{
		const TSharedPtr<FJsonObject>* CondObj = nullptr;
		if (Params->TryGetObjectField(TEXT("stop_when"), CondObj) && CondObj)
		{
			FString Err;
			if (!FStepCondition::Parse(*CondObj, /*bNegate=*/false, StopWhen, Err))
			{
				return MCPError(FString::Printf(TEXT("stop_when: %s"), *Err));
			}
		}
	}

	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));

	FSequenceContext Ctx;
	Ctx.World = World;
	Ctx.Budget = FPIESequenceRunner::kMaxStepExecutions;

	const bool bAbortOnError = !OptionalString(Params, TEXT("on_error"), TEXT("abort")).Equals(TEXT("continue"), ESearchCase::IgnoreCase);

	auto Result = MCPSuccess();

	if (OptionalBool(Params, TEXT("sample_before"), true))
	{
		Result->SetObjectField(TEXT("initial"), TakeSnapshot(Ctx, Samples));
	}

	TArray<TSharedPtr<FJsonValue>> Turns;
	FString StoppedBecause = TEXT("iterations");
	FString FailureError;

	for (int32 Turn = 0; Turn < Iterations; ++Turn)
	{
		if (!GEditor || GEditor->PlayWorld != World)
		{
			StoppedBecause = TEXT("pie_ended");
			FailureError = TEXT("the PIE session ended part way through the loop");
			break;
		}

		if (StopWhen.bSet)
		{
			bool bMet = false;
			FString Err;
			if (!FPIESequenceRunner::EvaluateCondition(Ctx, StopWhen, bMet, Err))
			{
				StoppedBecause = TEXT("stop_when_error");
				FailureError = Err;
				break;
			}
			if (bMet)
			{
				StoppedBecause = TEXT("stop_when");
				break;
			}
		}

		TSharedRef<FJsonObject> TurnObj = MakeShared<FJsonObject>();
		TurnObj->SetNumberField(TEXT("iteration"), Turn);

		TArray<TSharedPtr<FJsonValue>> Called;
		bool bTurnOk = true;
		for (const FInvokeEntry& E : Invokes)
		{
			TSharedRef<FJsonObject> CallObj = MakeShared<FJsonObject>();
			CallObj->SetStringField(TEXT("func"), E.Func);

			FString Err;
			UObject* Target = FPIESequenceRunner::ResolveTarget(Ctx, E.Target, Err);
			if (!Target)
			{
				CallObj->SetBoolField(TEXT("ok"), false);
				CallObj->SetStringField(TEXT("error"), Err);
				Called.Add(MakeShared<FJsonValueObject>(CallObj));
				bTurnOk = false;
				continue;
			}

			CallObj->SetStringField(TEXT("target"), Target->GetName());
			TSharedPtr<FJsonValue> Returned;
			const bool bOk = FPIEActorPuppet::CallFunctionWithResult(Target, E.Func, E.Args, Returned, Err);
			CallObj->SetBoolField(TEXT("ok"), bOk);
			if (bOk)
			{
				// The activation's own answer is often the verdict (did the ability
				// actually activate), so it is recorded next to the snapshot rather
				// than discarded.
				if (Returned.IsValid()) CallObj->SetField(TEXT("result"), Returned);
			}
			else
			{
				CallObj->SetStringField(TEXT("error"), Err);
				bTurnOk = false;
			}
			Called.Add(MakeShared<FJsonValueObject>(CallObj));
		}

		TurnObj->SetArrayField(TEXT("invoked"), Called);
		TurnObj->SetObjectField(TEXT("samples"), TakeSnapshot(Ctx, Samples));
		TurnObj->SetBoolField(TEXT("ok"), bTurnOk);
		Turns.Add(MakeShared<FJsonValueObject>(TurnObj));

		if (!bTurnOk && bAbortOnError)
		{
			StoppedBecause = TEXT("invoke_error");
			FailureError = FString::Printf(TEXT("iteration %d: an invocation failed"), Turn);
			break;
		}
	}

	Result->SetArrayField(TEXT("turns"), Turns);
	Result->SetNumberField(TEXT("iterations_run"), Turns.Num());
	Result->SetNumberField(TEXT("iterations_requested"), Iterations);
	Result->SetStringField(TEXT("stopped_because"), StoppedBecause);
	if (!FailureError.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FailureError);
	}
	return MCPResult(Result);
}
