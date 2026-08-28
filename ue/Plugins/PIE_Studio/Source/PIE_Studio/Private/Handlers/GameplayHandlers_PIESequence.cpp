// Ordered step execution against the live PIE world (#960): run_sequence.
// A member of FGameplayHandlers.
//
// The whole list runs inside one game-thread dispatch. That is the point: a
// six-call verification kept losing to another agent ending the PIE session
// between calls, and no amount of retrying individual calls fixes a sequence
// that only means anything as a whole. A bridge handler already executes on the
// game thread, so nothing else runs between steps by construction.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIESequenceRunner.h"
#include "Editor.h"
#include "Engine/World.h"

TSharedPtr<FJsonValue> FGameplayHandlers::PieRunSequence(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!Params->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
	{
		return MCPError(TEXT("'steps' is required (an ordered array of step objects)"));
	}

	// Validate the whole list before touching the world. A sequence that is going
	// to fail on step 5 should not have applied steps 1 to 4 first.
	TArray<FString> Problems;
	const bool bValid = FPIESequenceRunner::Validate(*Steps, Problems);

	if (OptionalBool(Params, TEXT("validate_only"), false) || !bValid)
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("valid"), bValid);
		Result->SetNumberField(TEXT("step_count"), Steps->Num());
		TArray<TSharedPtr<FJsonValue>> Errs;
		for (const FString& P : Problems) Errs.Add(MakeShared<FJsonValueString>(P));
		Result->SetArrayField(TEXT("errors"), Errs);
		if (!bValid)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("sequence is not runnable: %s"), *FString::Join(Problems, TEXT("; "))));
		}
		return MCPResult(Result);
	}

	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (!World) return MCPError(TEXT("PIE not running"));

	FSequenceContext Ctx;
	Ctx.World = World;
	Ctx.Budget = FPIESequenceRunner::kMaxStepExecutions;

	const bool bAbortOnError = !OptionalString(Params, TEXT("on_error"), TEXT("abort")).Equals(TEXT("continue"), ESearchCase::IgnoreCase);

	TArray<TSharedPtr<FJsonValue>> Log;
	FString RunError;
	const bool bRan = FPIESequenceRunner::Run(Ctx, *Steps, bAbortOnError, Log, RunError);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("completed"), bRan);
	Result->SetNumberField(TEXT("steps_run"), Ctx.StepsRun);
	Result->SetArrayField(TEXT("steps"), Log);

	TSharedRef<FJsonObject> Captured = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Ctx.Captured)
	{
		Captured->SetField(KV.Key, KV.Value);
	}
	Result->SetObjectField(TEXT("captured"), Captured);

	if (!bRan)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), RunError);
	}
	return MCPResult(Result);
}
