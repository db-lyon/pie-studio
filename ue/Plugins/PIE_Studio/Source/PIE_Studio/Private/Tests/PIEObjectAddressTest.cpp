// Automation coverage for the addressing grammar the driving verbs share
// (#954, #955, #960). Parsing is pure JSON, so it is testable without PIE;
// resolution needs a live world and is exercised by the live runs.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PIE/PIEObjectAddress.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> AddressJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIEObjectAddressParseTest,
	"PIEStudio.Address.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIEObjectAddressParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	// A bare "target" string is the actor token, the spelling actor_set already uses.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(TEXT("{\"target\":\"BP_Hero_C_0\"}")));
		TestEqual(TEXT("bare target is the actor"), A.Actor, FString(TEXT("BP_Hero_C_0")));
		TestTrue(TEXT("bare target is addressable"), !A.IsEmpty());
	}

	// A nested target object carries the full address, including a component
	// subobject on an actor - the case the older verbs could not name.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(
			TEXT("{\"target\":{\"actorLabel\":\"Hero\",\"component\":\"CombatComponent\",\"viaProperty\":\"CurrentTarget\"}}")));
		TestEqual(TEXT("actorLabel alias"), A.Actor, FString(TEXT("Hero")));
		TestEqual(TEXT("component"), A.Component, FString(TEXT("CombatComponent")));
		TestEqual(TEXT("viaProperty"), A.Property, FString(TEXT("CurrentTarget")));
	}

	// The flat form is equivalent, and snake_case spellings are accepted because
	// the recording verbs use that convention.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(
			TEXT("{\"actor_label\":\"Hero\",\"component_name\":\"Movement\",\"via_property\":\"Owner\"}")));
		TestEqual(TEXT("snake actor"), A.Actor, FString(TEXT("Hero")));
		TestEqual(TEXT("snake component"), A.Component, FString(TEXT("Movement")));
		TestEqual(TEXT("snake viaProperty"), A.Property, FString(TEXT("Owner")));
	}

	// Subsystem addressing, with the scope normalised to lower case.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(
			TEXT("{\"subsystemClass\":\"BattleSubsystem\",\"scope\":\"Game\"}")));
		TestEqual(TEXT("subsystem"), A.Subsystem, FString(TEXT("BattleSubsystem")));
		TestEqual(TEXT("scope lowercased"), A.Scope, FString(TEXT("game")));
		TestTrue(TEXT("subsystem address is addressable"), !A.IsEmpty());
	}

	// A reference to a value an earlier step captured.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(TEXT("{\"target\":{\"ref\":\"spawned\"}}")));
		TestEqual(TEXT("ref"), A.Ref, FString(TEXT("spawned")));
		TestTrue(TEXT("ref is addressable"), !A.IsEmpty());
	}

	// An address that names no base is empty, and says so rather than resolving
	// to whatever happened to be first in the world.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(TEXT("{\"func\":\"Jump\"}")));
		TestTrue(TEXT("no base is empty"), A.IsEmpty());

		FString Err;
		TestNull(TEXT("empty address does not resolve"),
			FPIEObjectAddress::Resolve(nullptr, A, nullptr, Err));
		TestTrue(TEXT("empty address explains itself"), !Err.IsEmpty());
	}

	// An unknown ref is an error naming the ref, not a silent null target.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(TEXT("{\"target\":{\"ref\":\"missing\"}}")));
		FString Err;
		TestNull(TEXT("unknown ref does not resolve"),
			FPIEObjectAddress::Resolve(nullptr, A, nullptr, Err));
		TestTrue(TEXT("error names the ref"), Err.Contains(TEXT("missing")));
	}

	// Describe is what a failed step reports back, so it has to carry the fields.
	{
		FObjectAddress A = FObjectAddress::FromJson(AddressJson(
			TEXT("{\"target\":{\"actor\":\"Hero\",\"component\":\"Combat\"}}")));
		const FString D = A.Describe();
		TestTrue(TEXT("describe names the actor"), D.Contains(TEXT("Hero")));
		TestTrue(TEXT("describe names the component"), D.Contains(TEXT("Combat")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
