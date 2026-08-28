// Automation coverage for the per-tick telemetry summary (#954). The recorder
// itself needs a live PIE world, but the summary is what an agent actually
// reads, and it is a pure function of the sampled rows - so the jump-shaped
// question ("how high, how long off the ground") is asserted here rather than
// left to a live run to discover.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PIE/PIETelemetryRecorder.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	TSharedPtr<FJsonValue> TelemetryVec(double X, double Y, double Z)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return MakeShared<FJsonValueObject>(O);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIETelemetrySummaryTest,
	"PIEStudio.Telemetry.Summary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIETelemetrySummaryTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	// A jump: rises to 127.5cm and comes back down, off the ground for the middle
	// three seconds. One sample per second keeps the arithmetic checkable by eye.
	FTelemetryRun Run;
	Run.Config.Id = TEXT("tel_test");
	Run.State = ETelemetryState::Completed;
	Run.StopReason = TEXT("duration");
	Run.EndWorldTime = 4.0;

	FTelemetryChannelSpec Loc;
	Loc.Name = TEXT("location");
	Loc.Kind = ETelemetryChannel::Location;
	Run.Config.Channels.Add(Loc);

	FTelemetryChannelSpec Ground;
	Ground.Name = TEXT("grounded");
	Ground.Kind = ETelemetryChannel::Grounded;
	Run.Config.Channels.Add(Ground);

	const double Heights[] = { 0.0, 60.0, 127.5, 60.0, 0.0 };
	const bool Grounded[] = { true, false, false, false, true };
	for (int32 i = 0; i < 5; ++i)
	{
		FTelemetrySample S;
		S.Index = i;
		S.Time = static_cast<double>(i);
		S.Dt = 1.0;
		S.Values.Add(TEXT("location"), TelemetryVec(0.0, 0.0, Heights[i]));
		S.Values.Add(TEXT("grounded"), MakeShared<FJsonValueBoolean>(Grounded[i]));
		Run.Samples.Add(MoveTemp(S));
	}

	TSharedRef<FJsonObject> Json = FPIETelemetryRecorder::RunToJson(Run, /*bIncludeSamples=*/true);

	TestEqual(TEXT("id"), Json->GetStringField(TEXT("id")), FString(TEXT("tel_test")));
	TestEqual(TEXT("state"), Json->GetStringField(TEXT("state")), FString(TEXT("completed")));
	TestEqual(TEXT("stop reason"), Json->GetStringField(TEXT("stop_reason")), FString(TEXT("duration")));
	TestEqual(TEXT("sample count"), static_cast<int32>(Json->GetNumberField(TEXT("sample_count"))), 5);

	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	TestTrue(TEXT("samples included"), Json->TryGetArrayField(TEXT("samples"), Rows) && Rows);
	if (Rows) TestEqual(TEXT("row count"), Rows->Num(), 5);

	const TSharedPtr<FJsonObject>* Summary = nullptr;
	if (!TestTrue(TEXT("summary present"), Json->TryGetObjectField(TEXT("summary"), Summary) && Summary))
	{
		return false;
	}

	// A vector channel is summarised per component, so the rise height is
	// max - first on z rather than a number the caller has to derive by hand.
	const TSharedPtr<FJsonObject>* LocSummary = nullptr;
	if (TestTrue(TEXT("location summarised"), (*Summary)->TryGetObjectField(TEXT("location"), LocSummary) && LocSummary))
	{
		const TSharedPtr<FJsonObject>* Z = nullptr;
		if (TestTrue(TEXT("z component summarised"), (*LocSummary)->TryGetObjectField(TEXT("z"), Z) && Z))
		{
			TestEqual(TEXT("peak height"), (*Z)->GetNumberField(TEXT("max")), 127.5);
			TestEqual(TEXT("peak time"), (*Z)->GetNumberField(TEXT("max_time")), 2.0);
			TestEqual(TEXT("start height"), (*Z)->GetNumberField(TEXT("first")), 0.0);
			TestEqual(TEXT("landed back"), (*Z)->GetNumberField(TEXT("last")), 0.0);
			TestEqual(TEXT("net displacement"), (*Z)->GetNumberField(TEXT("delta")), 0.0);
		}
	}

	// A boolean channel carries the air time directly, plus the two transitions
	// that bracket it, which is what "left the ground and landed" is read off.
	const TSharedPtr<FJsonObject>* GroundSummary = nullptr;
	if (TestTrue(TEXT("grounded summarised"), (*Summary)->TryGetObjectField(TEXT("grounded"), GroundSummary) && GroundSummary))
	{
		TestEqual(TEXT("airborne seconds"), (*GroundSummary)->GetNumberField(TEXT("false_seconds")), 3.0);
		TestEqual(TEXT("grounded seconds"), (*GroundSummary)->GetNumberField(TEXT("true_seconds")), 1.0);
		TestTrue(TEXT("started grounded"), (*GroundSummary)->GetBoolField(TEXT("first")));
		TestTrue(TEXT("landed grounded"), (*GroundSummary)->GetBoolField(TEXT("last")));

		const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
		if (TestTrue(TEXT("transitions present"), (*GroundSummary)->TryGetArrayField(TEXT("transitions"), Transitions) && Transitions))
		{
			TestEqual(TEXT("takeoff and landing"), Transitions->Num(), 2);
			if (Transitions->Num() == 2)
			{
				TestEqual(TEXT("takeoff time"), (*Transitions)[0]->AsObject()->GetNumberField(TEXT("time")), 1.0);
				TestFalse(TEXT("takeoff leaves the ground"), (*Transitions)[0]->AsObject()->GetBoolField(TEXT("to")));
				TestEqual(TEXT("landing time"), (*Transitions)[1]->AsObject()->GetNumberField(TEXT("time")), 4.0);
				TestTrue(TEXT("landing regains the ground"), (*Transitions)[1]->AsObject()->GetBoolField(TEXT("to")));
			}
		}
	}

	// Asking for the summary without the rows leaves the rows out, so a poll
	// during a long recording stays cheap.
	TSharedRef<FJsonObject> Lean = FPIETelemetryRecorder::RunToJson(Run, /*bIncludeSamples=*/false);
	TestFalse(TEXT("rows omitted on request"), Lean->HasField(TEXT("samples")));
	TestTrue(TEXT("summary still present"), Lean->HasField(TEXT("summary")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
