// Automation coverage for the sequence runner's structural checks (#960) and
// the condition grammar it shares with sample_loop (#955). Execution needs a
// live PIE world; validation is pure JSON and is where the guarantees that
// matter live - above all that a loop cannot be written without a bounded
// iteration cap, since an unbounded predicate would hang the one dispatch the
// whole design rests on.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PIE/PIESequenceRunner.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// The fixtures are written with single quotes and converted here. JSON nested
	// three levels deep is unreadable once every quote is a backslash escape, and
	// the readability is the point of a fixture.
	FString SequenceJson(const FString& Compact)
	{
		return Compact.Replace(TEXT("'"), TEXT("\""));
	}

	TArray<TSharedPtr<FJsonValue>> SequenceSteps(const FString& Compact)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SequenceJson(Compact));
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	TSharedPtr<FJsonObject> SequenceObject(const FString& Compact)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SequenceJson(Compact));
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	bool AnyContains(const TArray<FString>& Errors, const TCHAR* Needle)
	{
		for (const FString& E : Errors)
		{
			if (E.Contains(Needle)) return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIESequenceValidateTest,
	"PIEStudio.Sequence.Validate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIESequenceValidateTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	// The shape #960 describes: mint state, bind it to a component on an actor an
	// earlier step spawned, kick a flow off, step it until a flag clears, then
	// read two objects.
	{
		const TArray<TSharedPtr<FJsonValue>> Steps = SequenceSteps(TEXT(
			"["
			"{'kind':'spawn','class':'/Game/BP_Fighter.BP_Fighter_C','at':[0,0,100],'capture':'fighter'},"
			"{'kind':'set','target':{'ref':'fighter','component':'CombatComponent'},'path':'Record','value':'seeded'},"
			"{'kind':'call','target':{'subsystem':'BattleSubsystem','scope':'game'},'func':'StartBattle','capture':'battle'},"
			"{'kind':'loop','max_iterations':50,"
			 "'while':{'subsystem':'BattleSubsystem','property':'bBattleActive','op':'is_true'},"
			 "'steps':[{'kind':'call','target':{'subsystem':'BattleSubsystem'},'func':'StepTurn'}]},"
			"{'kind':'read','target':{'ref':'fighter','component':'CombatComponent'},'property':'Damage','capture':'damage'},"
			"{'kind':'read','target':{'subsystem':'BattleSubsystem'},'property':'Result','capture':'result'}"
			"]"));
		TestEqual(TEXT("six steps parsed"), Steps.Num(), 6);

		TArray<FString> Errors;
		const bool bValid = FPIESequenceRunner::Validate(Steps, Errors);
		TestTrue(FString::Printf(TEXT("valid sequence (%s)"), *FString::Join(Errors, TEXT("; "))), bValid);
	}

	// A loop without a cap is rejected. This is the one rule the runner cannot
	// bend: the whole sequence runs in a single game-thread dispatch.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT(
			"[{'kind':'loop','while':{'subsystem':'S','property':'bGoing'},"
			"'steps':[{'kind':'call','target':'A','func':'Tick'}]}]")), Errors);
		TestTrue(TEXT("uncapped loop rejected"), AnyContains(Errors, TEXT("max_iterations")));
	}

	// And the cap has to be inside the ceiling.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT(
			"[{'kind':'loop','max_iterations':100000,"
			"'steps':[{'kind':'call','target':'A','func':'Tick'}]}]")), Errors);
		TestTrue(TEXT("oversized cap rejected"), AnyContains(Errors, TEXT("max_iterations")));
	}

	// while and until are the same knob read two ways; naming both is ambiguous.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT(
			"[{'kind':'loop','max_iterations':10,"
			"'while':{'subsystem':'S','property':'bGoing'},"
			"'until':{'subsystem':'S','property':'bDone'},"
			"'steps':[{'kind':'call','target':'A','func':'Tick'}]}]")), Errors);
		TestTrue(TEXT("while+until rejected"), AnyContains(Errors, TEXT("pick one")));
	}

	// A loop body is validated too, so a typo two levels down is caught before
	// anything is applied.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT(
			"[{'kind':'loop','max_iterations':5,'steps':[{'kind':'nonsense'}]}]")), Errors);
		TestTrue(TEXT("loop body validated"), AnyContains(Errors, TEXT("loop body")));
	}

	// Missing required fields are named per step and per field.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT(
			"["
			"{'kind':'set','path':'Health','value':10},"
			"{'kind':'call','target':'Hero'},"
			"{'kind':'read','target':'Hero'},"
			"{'kind':'teleport','target':'Hero'}"
			"]")), Errors);
		TestTrue(TEXT("set without a target"), AnyContains(Errors, TEXT("set needs a target")));
		TestTrue(TEXT("call without a func"), AnyContains(Errors, TEXT("call needs 'func'")));
		TestTrue(TEXT("read without a property"), AnyContains(Errors, TEXT("read needs 'property'")));
		TestTrue(TEXT("unknown kind named"), AnyContains(Errors, TEXT("teleport")));
	}

	// An empty list is not a runnable sequence.
	{
		TArray<FString> Errors;
		FPIESequenceRunner::Validate(SequenceSteps(TEXT("[]")), Errors);
		TestTrue(TEXT("empty list rejected"), AnyContains(Errors, TEXT("no steps")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEStepConditionTest,
	"PIEStudio.Sequence.Condition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEStepConditionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	// The default op is a truth test, which is what "step until the flag clears"
	// needs without spelling out a value.
	{
		FStepCondition C;
		FString Err;
		TestTrue(TEXT("bare property parses"),
			FStepCondition::Parse(SequenceObject(TEXT("{'subsystem':'S','property':'bBattleActive'}")), false, C, Err));
		TestTrue(TEXT("op defaults to a truth test"), C.Op == EStepCondOp::IsTrue);
		TestFalse(TEXT("not negated"), C.bNegated);
		TestEqual(TEXT("subsystem carried"), C.Target.Subsystem, FString(TEXT("S")));
	}

	// until is while-not, and says so when described.
	{
		FStepCondition C;
		FString Err;
		TestTrue(TEXT("until parses"),
			FStepCondition::Parse(SequenceObject(TEXT("{'subsystem':'S','property':'bBattleActive'}")), true, C, Err));
		TestTrue(TEXT("negated"), C.bNegated);
		TestTrue(TEXT("describe says until"), C.Describe().Contains(TEXT("until")));
	}

	// A comparison operator without a value is a broken predicate, not a
	// predicate that quietly compares against zero.
	{
		FStepCondition C;
		FString Err;
		TestFalse(TEXT("gt without value rejected"),
			FStepCondition::Parse(SequenceObject(TEXT("{'actor':'Hero','property':'Health','op':'gt'}")), false, C, Err));
		TestTrue(TEXT("error mentions value"), Err.Contains(TEXT("value")));
	}

	// An unknown operator is named rather than silently defaulted.
	{
		FStepCondition C;
		FString Err;
		TestFalse(TEXT("unknown op rejected"),
			FStepCondition::Parse(SequenceObject(TEXT("{'actor':'Hero','property':'Health','op':'approximately'}")), false, C, Err));
		TestTrue(TEXT("error names the op"), Err.Contains(TEXT("approximately")));
	}

	// A condition with no property has nothing to test.
	{
		FStepCondition C;
		FString Err;
		TestFalse(TEXT("missing property rejected"),
			FStepCondition::Parse(SequenceObject(TEXT("{'actor':'Hero'}")), false, C, Err));
		TestTrue(TEXT("error mentions property"), Err.Contains(TEXT("property")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
