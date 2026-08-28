#include "PIE/PIEObjectAddress.h"
#include "PIE/PIEActorPuppet.h"
#include "PIE/PIESequenceFormat.h"   // FindActorById
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EngineSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

#include <initializer_list>

namespace UEMCPPIE
{
	namespace
	{
		// Read a string from the first key that carries one. Address keys are spelled
		// camelCase to match the live-PIE verbs (actorLabel, subsystemClass), with the
		// snake_case spelling accepted too because the recording verbs use that.
		FString FirstString(const TSharedPtr<FJsonObject>& O, std::initializer_list<const TCHAR*> Keys)
		{
			if (!O.IsValid()) return FString();
			for (const TCHAR* K : Keys)
			{
				FString V;
				if (O->TryGetStringField(K, V) && !V.IsEmpty()) return V;
			}
			return FString();
		}

		bool SubsystemClassMatches(UObject* O, const FString& ClassName)
		{
			if (!O) return false;
			const FString N = O->GetClass()->GetName();
			return N == ClassName
				|| N == (FString(TEXT("U")) + ClassName)
				|| O->GetClass()->GetPathName() == ClassName;
		}
	}

	FString FObjectAddress::Describe() const
	{
		TArray<FString> Parts;
		if (!Ref.IsEmpty())        Parts.Add(FString::Printf(TEXT("ref=%s"), *Ref));
		if (!Actor.IsEmpty())      Parts.Add(FString::Printf(TEXT("actor=%s"), *Actor));
		if (!Subsystem.IsEmpty())  Parts.Add(FString::Printf(TEXT("subsystem=%s"), *Subsystem));
		if (!Scope.IsEmpty())      Parts.Add(FString::Printf(TEXT("scope=%s"), *Scope));
		if (!ObjectPath.IsEmpty()) Parts.Add(FString::Printf(TEXT("objectPath=%s"), *ObjectPath));
		if (!Component.IsEmpty())  Parts.Add(FString::Printf(TEXT("component=%s"), *Component));
		if (!Property.IsEmpty())   Parts.Add(FString::Printf(TEXT("viaProperty=%s"), *Property));
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" ")) : TEXT("<empty address>");
	}

	FObjectAddress FObjectAddress::FromJson(const TSharedPtr<FJsonObject>& Step)
	{
		FObjectAddress A;
		if (!Step.IsValid()) return A;

		// A nested "target" object holds the address; a bare "target" string is the
		// actor token, which is what actor_set / actor_call already accept.
		TSharedPtr<FJsonObject> Source = Step;
		const TSharedPtr<FJsonObject>* Nested = nullptr;
		FString TargetString;
		if (Step->TryGetObjectField(TEXT("target"), Nested) && Nested)
		{
			Source = *Nested;
		}
		else if (Step->TryGetStringField(TEXT("target"), TargetString) && !TargetString.IsEmpty())
		{
			A.Actor = TargetString;
		}

		const FString Actor = FirstString(Source, { TEXT("actor"), TEXT("actorLabel"), TEXT("actor_label"), TEXT("target") });
		if (!Actor.IsEmpty()) A.Actor = Actor;

		A.Component  = FirstString(Source, { TEXT("component"), TEXT("componentName"), TEXT("component_name") });
		A.Subsystem  = FirstString(Source, { TEXT("subsystem"), TEXT("subsystemClass"), TEXT("subsystem_class") });
		A.Scope      = FirstString(Source, { TEXT("scope") }).ToLower();
		A.ObjectPath = FirstString(Source, { TEXT("objectPath"), TEXT("object_path") });
		A.Ref        = FirstString(Source, { TEXT("ref"), TEXT("fromStep"), TEXT("from_step") });
		// Deliberately NOT "property": a read step and a loop condition both carry a
		// "property" of their own meaning the value to read, and one key cannot mean
		// both "the object to reach" and "the value to test on it".
		A.Property   = FirstString(Source, { TEXT("viaProperty"), TEXT("via_property"), TEXT("via") });
		return A;
	}

	UObject* FPIEObjectAddress::ResolveSubsystem(UWorld* World, const FString& ClassName, const FString& Scope)
	{
		if (ClassName.IsEmpty()) return nullptr;
		const bool bAny = Scope.IsEmpty();

		if (World)
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (bAny || Scope == TEXT("game"))
				{
					for (UGameInstanceSubsystem* S : GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>())
					{
						if (SubsystemClassMatches(S, ClassName)) return S;
					}
				}
				if (bAny || Scope == TEXT("localplayer"))
				{
					if (ULocalPlayer* LP = GI->GetFirstGamePlayer())
					{
						for (ULocalPlayerSubsystem* S : LP->GetSubsystemArrayCopy<ULocalPlayerSubsystem>())
						{
							if (SubsystemClassMatches(S, ClassName)) return S;
						}
					}
				}
			}
			if (bAny || Scope == TEXT("world"))
			{
				for (UWorldSubsystem* S : World->GetSubsystemArrayCopy<UWorldSubsystem>())
				{
					if (SubsystemClassMatches(S, ClassName)) return S;
				}
			}
		}

		if ((bAny || Scope == TEXT("engine")) && GEngine)
		{
			for (UEngineSubsystem* S : GEngine->GetEngineSubsystemArrayCopy<UEngineSubsystem>())
			{
				if (SubsystemClassMatches(S, ClassName)) return S;
			}
		}
		return nullptr;
	}

	AActor* FPIEObjectAddress::ResolveActor(UWorld* World, const FString& Token)
	{
		if (!World || Token.IsEmpty()) return nullptr;

		// The editor label is what an agent reads off the outliner, so it decides
		// outright; object name, class name and path resolve the misses. FindActorById
		// carries the name/class/path-suffix half already.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!IsValid(A)) continue;
#if WITH_EDITOR
			if (A->GetActorLabel() == Token) return A;
#endif
		}
		return FindActorById(World, Token);
	}

	UActorComponent* FPIEObjectAddress::ResolveComponent(AActor* Actor, const FString& Token)
	{
		if (!Actor || Token.IsEmpty()) return nullptr;

		UActorComponent* CaseInsensitive = nullptr;
		UActorComponent* ByClass = nullptr;
		for (UActorComponent* C : Actor->GetComponents())
		{
			if (!IsValid(C)) continue;
			const FString Name = C->GetName();
			if (Name == Token) return C;
			if (!CaseInsensitive && Name.Equals(Token, ESearchCase::IgnoreCase)) CaseInsensitive = C;
			if (!ByClass)
			{
				const UClass* Cls = C->GetClass();
				if (Cls && (Cls->GetName() == Token
					|| Cls->GetName() == (FString(TEXT("U")) + Token)
					|| Cls->GetPathName() == Token))
				{
					ByClass = C;
				}
			}
		}
		return CaseInsensitive ? CaseInsensitive : ByClass;
	}

	UObject* FPIEObjectAddress::Resolve(UWorld* World, const FObjectAddress& Addr,
	                                    const FObjectRefMap* Refs, FString& OutError)
	{
		UObject* Base = nullptr;

		if (!Addr.Ref.IsEmpty())
		{
			const TWeakObjectPtr<UObject>* Found = Refs ? Refs->Find(Addr.Ref) : nullptr;
			if (!Found)
			{
				OutError = FString::Printf(TEXT("no earlier step captured '%s'"), *Addr.Ref);
				return nullptr;
			}
			Base = Found->Get();
			if (!Base)
			{
				OutError = FString::Printf(TEXT("the object captured as '%s' is gone"), *Addr.Ref);
				return nullptr;
			}
		}
		else if (!Addr.Actor.IsEmpty())
		{
			Base = ResolveActor(World, Addr.Actor);
			if (!Base)
			{
				OutError = FString::Printf(TEXT("actor not found: %s"), *Addr.Actor);
				return nullptr;
			}
		}
		else if (!Addr.Subsystem.IsEmpty())
		{
			Base = ResolveSubsystem(World, Addr.Subsystem, Addr.Scope);
			if (!Base)
			{
				OutError = Addr.Scope.IsEmpty()
					? FString::Printf(TEXT("subsystem not found: %s"), *Addr.Subsystem)
					: FString::Printf(TEXT("subsystem not found: %s (scope %s)"), *Addr.Subsystem, *Addr.Scope);
				return nullptr;
			}
		}
		else if (!Addr.ObjectPath.IsEmpty())
		{
			Base = FindObject<UObject>(nullptr, *Addr.ObjectPath);
			if (!Base) Base = LoadObject<UObject>(nullptr, *Addr.ObjectPath);
			if (!Base)
			{
				OutError = FString::Printf(TEXT("object not found: %s"), *Addr.ObjectPath);
				return nullptr;
			}
		}
		else
		{
			OutError = TEXT("address names nothing (expected one of actor, subsystem, objectPath, ref)");
			return nullptr;
		}

		if (!Addr.Component.IsEmpty())
		{
			AActor* AsActor = Cast<AActor>(Base);
			if (!AsActor)
			{
				OutError = FString::Printf(TEXT("'component' needs an actor base, got %s"), *Base->GetClass()->GetName());
				return nullptr;
			}
			UActorComponent* Comp = ResolveComponent(AsActor, Addr.Component);
			if (!Comp)
			{
				OutError = FString::Printf(TEXT("component not found on %s: %s"), *AsActor->GetName(), *Addr.Component);
				return nullptr;
			}
			Base = Comp;
		}

		if (!Addr.Property.IsEmpty())
		{
			FString Err;
			UObject* Pointed = FPIEActorPuppet::ResolveObjectByPath(Base, Addr.Property, Err);
			if (!Pointed)
			{
				OutError = FString::Printf(TEXT("property '%s' on %s: %s"),
					*Addr.Property, *Base->GetName(), *Err);
				return nullptr;
			}
			Base = Pointed;
		}

		return Base;
	}
}
