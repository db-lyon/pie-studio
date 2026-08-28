// Live-object addressing for the driving verbs (#954, #955, #960).
//
// The existing verbs address one thing each: actor_set/actor_call take an actor
// id, subsystem_state takes a subsystem class, tracked values take a dotted
// path. A telemetry recorder, a sample loop and a sequence runner all have to
// reach the same set of things through one grammar, including the two the older
// verbs could not name at all: a component subobject on an actor that was
// spawned at run time, and an object a UPROPERTY pointer was made to point at
// part way through the run.
//
// An address is a small JSON object. The base is whichever of actor /
// subsystem / objectPath / ref is present; component and property then narrow
// from that base. Resolution is pure lookup with no side effects, so a failed
// address never half-applies a step.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UActorComponent;
class UObject;
class UWorld;

namespace UEMCPPIE
{
	// Values captured by earlier steps of the same run, addressable by name.
	using FObjectRefMap = TMap<FString, TWeakObjectPtr<UObject>>;

	struct FObjectAddress
	{
		FString Actor;        // editor label, object name, class name, or object path
		FString Component;    // component subobject on that actor, by name or class
		FString Subsystem;    // subsystem class short name or path
		FString Scope;        // game | world | localplayer | engine (empty = search all)
		FString ObjectPath;   // direct object path
		FString Ref;          // name captured by an earlier step in the same run
		FString Property;     // dotted path from the base to an object-valued UPROPERTY

		bool IsEmpty() const
		{
			return Actor.IsEmpty() && Subsystem.IsEmpty() && ObjectPath.IsEmpty() && Ref.IsEmpty();
		}

		// One-line rendering for error messages, so a miss names what was asked for.
		FString Describe() const;

		// Read an address off a step object. Accepts the flat form (actor / component
		// / subsystem / ... as sibling keys of the step), a nested "target" object, or
		// "target" as a bare string meaning the actor token - the spelling actor_set
		// and actor_call already use.
		static FObjectAddress FromJson(const TSharedPtr<FJsonObject>& Step);
	};

	class FPIEObjectAddress
	{
	public:
		// Resolve a subsystem by short (or "U"-prefixed) class name or class path.
		// An empty Scope searches game instance, then local player, then world, then
		// engine, which is the order a gameplay record is most likely to live in.
		static UObject* ResolveSubsystem(UWorld* World, const FString& ClassName, const FString& Scope);

		// Resolve an actor by editor label, object name, class name, or path. Label
		// wins outright; the rest resolve the misses.
		static AActor* ResolveActor(UWorld* World, const FString& Token);

		// Resolve a component subobject by object name (exact, then case-insensitive)
		// then by class short name or path.
		static UActorComponent* ResolveComponent(AActor* Actor, const FString& Token);

		// Resolve a whole address. Refs may be null when the caller keeps no captures.
		// Returns null + OutError naming the segment that failed.
		static UObject* Resolve(UWorld* World, const FObjectAddress& Addr,
		                        const FObjectRefMap* Refs, FString& OutError);
	};
}
