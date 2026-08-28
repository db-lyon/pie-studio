// Arrange substrate (Roadmap v2, F1). Write access to the live world so an agent
// can construct a scenario, not just observe one: set any UProperty by dotted
// path (mirror of the read walk in PIEFrameSampler.cpp), spawn/destroy actors,
// and invoke callable UFUNCTIONs. SetPropertyByPath is pure reflection and needs
// no PIE, so it is unit testable on any UObject.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class AActor;
class UObject;
class UWorld;
class FProperty;

namespace UEMCPPIE
{
	class FPIEActorPuppet
	{
	public:
		// Resolve "Component.Struct.Property" from Root to its leaf FProperty and the
		// container that holds it. OutOwner is the nearest owning UObject, which
		// ImportText/ExportText need for object-reference coercion. Returns false +
		// OutError on an unresolved segment. Shared by the read and write halves so a
		// path that reads also writes, and one fix covers both. No PIE required.
		static bool ResolvePath(UObject* Root, const FString& Path,
		                        FProperty*& OutProperty, void*& OutContainer,
		                        UObject*& OutOwner, FString& OutError);

		// Write a UProperty reached by "Component.Struct.Property" from Root, coercing
		// the JSON value via FProperty::ImportText_Direct. Returns false + OutError on
		// an unresolved path or a coercion failure. No PIE required.
		static bool SetPropertyByPath(UObject* Root, const FString& Path,
		                              const TSharedPtr<FJsonValue>& Value, FString& OutError);

		// Read the same path back as JSON, structs included (a struct leaf comes back
		// as a JSON object of its fields, which is what a per-turn snapshot of a
		// gameplay record wants). Returns false + OutError on an unresolved path.
		// No PIE required.
		static bool GetPropertyByPath(UObject* Root, const FString& Path,
		                              TSharedPtr<FJsonValue>& OutValue, FString& OutError);

		// Follow the same path to an object-valued leaf and return the object it points
		// at. An empty path returns Root. This is how an address reaches a UPROPERTY
		// actor pointer that was only set part way through a run. No PIE required.
		static UObject* ResolveObjectByPath(UObject* Root, const FString& Path, FString& OutError);

		// Spawn ClassPath (native class or "/Game/.../BP_X.BP_X_C") at Xform. Returns
		// the actor (its GetName() is the id FindActorById resolves), null + OutError otherwise.
		static AActor* SpawnActor(UWorld* World, const FString& ClassPath,
		                          const FTransform& Xform, FString& OutError);

		// Invoke a callable UFUNCTION by name on Target, importing Args positionally
		// into its parameters. Returns false + OutError if the function is missing or
		// an argument fails to coerce.
		static bool CallFunction(UObject* Target, const FString& FuncName,
		                         const TArray<TSharedPtr<FJsonValue>>& Args, FString& OutError);

		// As CallFunction, and additionally hand back the return value as JSON when the
		// function has one (OutReturn is left unset for a void function). Struct returns
		// come back as JSON objects, so a loop can sample what a call answered rather
		// than only that it ran.
		//
		// OutReturnObject, when supplied, additionally receives the returned UObject for
		// an object-valued return. JSON renders such a return as a path string, and a
		// path string is not something a later step can be pointed at, so the object
		// itself has to survive the call for "use what step 3 produced" to work.
		static bool CallFunctionWithResult(UObject* Target, const FString& FuncName,
		                                   const TArray<TSharedPtr<FJsonValue>>& Args,
		                                   TSharedPtr<FJsonValue>& OutReturn, FString& OutError,
		                                   UObject** OutReturnObject = nullptr);
	};
}
