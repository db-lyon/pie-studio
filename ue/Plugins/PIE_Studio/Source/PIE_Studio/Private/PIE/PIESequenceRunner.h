// Ordered step execution inside one game-thread dispatch (#960), plus the
// condition grammar the bounded loop and the sample loop (#955) share.
//
// Two things defeated a call-per-step verification. First, concurrency: another
// agent driving the same editor ended the PIE session between calls, so a
// six-call sequence never survived to completion even though every individual
// call succeeded. Second, a conditional loop: "step until the battle flag
// clears" has no iteration count to write down in advance, so it cannot be a
// fixed call list at all.
//
// A run is therefore one dispatch. A bridge handler already executes on the
// game thread, so nothing else runs between steps by construction - there is no
// window for another client's request to land. The cost is that no step may
// wait for a tick: the world does not advance inside a run, so a step drives
// state through calls and reads, and anything needing elapsed time belongs in
// the telemetry recorder instead.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "PIE/PIEObjectAddress.h"

class UWorld;

namespace UEMCPPIE
{
	enum class EStepCondOp : uint8
	{
		Eq, Ne, Lt, Lte, Gt, Gte, IsTrue, IsFalse
	};

	struct FStepCondition
	{
		bool bSet = false;
		bool bNegated = false;          // "until" is "while not"
		FObjectAddress Target;
		FString Property;
		EStepCondOp Op = EStepCondOp::IsTrue;
		TSharedPtr<FJsonValue> Value;
		double Tolerance = 0.0;

		// Parse a condition object: {actor|subsystem|objectPath|ref, component?,
		// property, op?, value?, tolerance?}. Returns false + OutError on a bad op
		// or a missing property.
		static bool Parse(const TSharedPtr<FJsonObject>& O, bool bNegate, FStepCondition& Out, FString& OutError);
		FString Describe() const;
	};

	struct FSequenceContext
	{
		UWorld* World = nullptr;
		FObjectRefMap Refs;                                  // objects captured by name
		TMap<FString, TSharedPtr<FJsonValue>> Captured;      // values captured by name
		int32 StepsRun = 0;
		int32 Budget = 0;                                    // remaining step executions
	};

	class FPIESequenceRunner
	{
	public:
		static constexpr int32 kMaxSteps = 64;              // steps in one list
		static constexpr int32 kMaxLoopIterations = 1000;   // ceiling on a loop's own cap
		static constexpr int32 kMaxStepExecutions = 5000;   // total, so nested loops stay bounded

		// Structural validation with no side effects: known kinds, required fields
		// per kind, a mandatory and in-range max_iterations on every loop, and a
		// parseable condition. One message per problem.
		static bool Validate(const TArray<TSharedPtr<FJsonValue>>& Steps, TArray<FString>& OutErrors);

		// Execute an ordered step list. Appends one entry per step to OutLog whether
		// it succeeded or not. Returns false when a step failed and bAbortOnError.
		static bool Run(FSequenceContext& Ctx, const TArray<TSharedPtr<FJsonValue>>& Steps,
		                bool bAbortOnError, TArray<TSharedPtr<FJsonValue>>& OutLog, FString& OutError);

		// Evaluate a condition against the live world. bOutMet is meaningful only
		// when the call returns true.
		static bool EvaluateCondition(FSequenceContext& Ctx, const FStepCondition& Cond,
		                              bool& bOutMet, FString& OutError);

		// Resolve an address against the run's captures. Shared with sample_loop.
		static UObject* ResolveTarget(FSequenceContext& Ctx, const FObjectAddress& Addr, FString& OutError);

		// Record a named value, and the object behind it when there is one, so a
		// later step can address it. Shared with sample_loop.
		static void Capture(FSequenceContext& Ctx, const FString& Name,
		                    const TSharedPtr<FJsonValue>& Value, UObject* Object);
	};
}
