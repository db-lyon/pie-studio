// Automation coverage for the Arrange substrate (Roadmap v2, F1):
// SetPropertyByPath writes float/bool UProperties by reflection on a transient
// object, and reports a clear error for an unresolved path. Spawn/call need a
// live PIE world and are exercised via the scenario tests once F2 lands.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PIE/PIEActorPuppet.h"
#include "PIE/MCPObservationProfile.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEActorPuppetTest,
	"PIEStudio.Arrange.SetPropertyByPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEActorPuppetTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	UMCPObservationProfile* Obj = NewObject<UMCPObservationProfile>(GetTransientPackage());
	TestNotNull(TEXT("transient profile"), Obj);
	if (!Obj) return false;

	Obj->PositionThresholdCm = 1.0f;
	Obj->bCapturePawnState = false;

	FString Err;

	// Float write.
	const bool bFloat = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("PositionThresholdCm"), MakeShared<FJsonValueNumber>(42.5), Err);
	TestTrue(FString::Printf(TEXT("float write ok (%s)"), *Err), bFloat);
	TestTrue(TEXT("float applied"), FMath::IsNearlyEqual(Obj->PositionThresholdCm, 42.5f));

	// Bool write.
	const bool bBool = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("bCapturePawnState"), MakeShared<FJsonValueBoolean>(true), Err);
	TestTrue(FString::Printf(TEXT("bool write ok (%s)"), *Err), bBool);
	TestTrue(TEXT("bool applied"), Obj->bCapturePawnState);

	// Unresolved path fails with a message.
	Err.Reset();
	const bool bBad = FPIEActorPuppet::SetPropertyByPath(
		Obj, TEXT("NoSuchProperty"), MakeShared<FJsonValueNumber>(1), Err);
	TestFalse(TEXT("unknown path fails"), bBad);
	TestTrue(TEXT("error message set"), !Err.IsEmpty());

	return true;
}

// The read half of the same walk (#955, #960). A per-turn snapshot and a
// sequence runner both need to read what they just wrote back out, and a
// struct-shaped value has to survive as structure rather than a stringified
// blob, so the struct case is asserted rather than only the scalar one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEActorPuppetReadTest,
	"PIEStudio.Arrange.GetPropertyByPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEActorPuppetReadTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	UMCPObservationProfile* Obj = NewObject<UMCPObservationProfile>(GetTransientPackage());
	TestNotNull(TEXT("transient profile"), Obj);
	if (!Obj) return false;

	Obj->PositionThresholdCm = 7.5f;
	Obj->bCapturePawnState = true;
	FMCPTrackedValueEntry Entry;
	Entry.Path = TEXT("Boss.Health");
	Entry.DriftThreshold = 3.f;
	Obj->TrackedValues.Add(Entry);

	FString Err;
	TSharedPtr<FJsonValue> Value;

	// Scalar read round-trips the number.
	TestTrue(TEXT("float read ok"),
		FPIEActorPuppet::GetPropertyByPath(Obj, TEXT("PositionThresholdCm"), Value, Err));
	TestTrue(TEXT("float value"), Value.IsValid() && FMath::IsNearlyEqual(Value->AsNumber(), 7.5, 0.001));

	// Bool read.
	Value.Reset();
	TestTrue(TEXT("bool read ok"),
		FPIEActorPuppet::GetPropertyByPath(Obj, TEXT("bCapturePawnState"), Value, Err));
	TestTrue(TEXT("bool value"), Value.IsValid() && Value->AsBool());

	// Struct-shaped read keeps its fields: the array comes back as JSON objects,
	// with the entry's string field carried through.
	Value.Reset();
	TestTrue(TEXT("struct array read ok"),
		FPIEActorPuppet::GetPropertyByPath(Obj, TEXT("TrackedValues"), Value, Err));
	bool bFoundPath = false;
	if (Value.IsValid() && Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		TestEqual(TEXT("one entry"), Arr.Num(), 1);
		if (Arr.Num() == 1 && Arr[0].IsValid() && Arr[0]->Type == EJson::Object)
		{
			// FJsonObjectConverter standardises property-name casing, so match on
			// the value rather than pinning an exact key spelling.
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Arr[0]->AsObject()->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String
					&& KV.Value->AsString() == TEXT("Boss.Health"))
				{
					bFoundPath = true;
				}
			}
		}
	}
	TestTrue(TEXT("struct field survived as structure"), bFoundPath);

	// Unresolved path fails with a message rather than an empty value.
	Err.Reset();
	Value.Reset();
	TestFalse(TEXT("unknown path fails"),
		FPIEActorPuppet::GetPropertyByPath(Obj, TEXT("NoSuchProperty"), Value, Err));
	TestTrue(TEXT("error message set"), !Err.IsEmpty());

	// ResolveObjectByPath: empty path is the root, a non-object leaf is an error.
	Err.Reset();
	TestTrue(TEXT("empty path is root"),
		FPIEActorPuppet::ResolveObjectByPath(Obj, FString(), Err) == static_cast<UObject*>(Obj));
	Err.Reset();
	TestNull(TEXT("scalar leaf is not an object reference"),
		FPIEActorPuppet::ResolveObjectByPath(Obj, TEXT("PositionThresholdCm"), Err));
	TestTrue(TEXT("object-resolve error message set"), !Err.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
