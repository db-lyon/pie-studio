// Actor puppeteering handlers (Roadmap v2, F1): actor_spawn, actor_destroy,
// actor_set, actor_call. Write access to the live PIE world so an agent can
// Arrange a scenario. Members of FGameplayHandlers.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIEActorPuppet.h"
#include "PIE/PIESequenceFormat.h"   // FindActorById
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	using namespace UEMCPPIE;

	// Read a numeric array param into up to three components (defaults kept).
	void ReadVec3(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, double& X, double& Y, double& Z)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Params->TryGetArrayField(Field, Arr) || !Arr) return;
		if (Arr->Num() > 0 && (*Arr)[0].IsValid()) X = (*Arr)[0]->AsNumber();
		if (Arr->Num() > 1 && (*Arr)[1].IsValid()) Y = (*Arr)[1]->AsNumber();
		if (Arr->Num() > 2 && (*Arr)[2].IsValid()) Z = (*Arr)[2]->AsNumber();
	}

	UWorld* PieWorld() { return GEditor ? GEditor->PlayWorld : nullptr; }
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieActorSpawn(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString ClassPath;
	if (auto E = RequireString(Params, TEXT("class"), ClassPath)) return E;

	UWorld* World = PieWorld();
	if (!World) return MCPError(TEXT("PIE not running"));

	double X = 0, Y = 0, Z = 0;
	ReadVec3(Params, TEXT("at"), X, Y, Z);
	double Pitch = 0, Yaw = 0, Roll = 0;
	ReadVec3(Params, TEXT("rotation"), Pitch, Yaw, Roll);
	double SX = 1, SY = 1, SZ = 1;
	ReadVec3(Params, TEXT("scale"), SX, SY, SZ);

	const FTransform Xform(FRotator(Pitch, Yaw, Roll), FVector(X, Y, Z), FVector(SX, SY, SZ));

	FString Err;
	AActor* Actor = UEMCPPIE::FPIEActorPuppet::SpawnActor(World, ClassPath, Xform, Err);
	if (!Actor) return MCPError(Err);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("id"), Actor->GetName());
	Result->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Result->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieActorDestroy(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Target;
	if (auto E = RequireString(Params, TEXT("target"), Target)) return E;

	UWorld* World = PieWorld();
	if (!World) return MCPError(TEXT("PIE not running"));

	AActor* Actor = UEMCPPIE::FindActorById(World, Target);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *Target));

	const bool bDestroyed = Actor->Destroy();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("destroyed"), bDestroyed);
	Result->SetStringField(TEXT("target"), Target);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieActorSet(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Target, Path;
	if (auto E = RequireString(Params, TEXT("target"), Target)) return E;
	if (auto E = RequireString(Params, TEXT("path"), Path)) return E;
	const TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("value"));
	if (!Value.IsValid()) return MCPError(TEXT("'value' is required"));

	UWorld* World = PieWorld();
	if (!World) return MCPError(TEXT("PIE not running"));

	AActor* Actor = UEMCPPIE::FindActorById(World, Target);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *Target));

	FString Err;
	if (!UEMCPPIE::FPIEActorPuppet::SetPropertyByPath(Actor, Path, Value, Err))
	{
		return MCPError(Err);
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("target"), Target);
	Result->SetStringField(TEXT("path"), Path);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieActorCall(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Target, Func;
	if (auto E = RequireString(Params, TEXT("target"), Target)) return E;
	if (auto E = RequireString(Params, TEXT("func"), Func)) return E;

	UWorld* World = PieWorld();
	if (!World) return MCPError(TEXT("PIE not running"));

	AActor* Actor = UEMCPPIE::FindActorById(World, Target);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *Target));

	TArray<TSharedPtr<FJsonValue>> Args;
	const TArray<TSharedPtr<FJsonValue>>* ArgArr = nullptr;
	if (Params->TryGetArrayField(TEXT("args"), ArgArr) && ArgArr) Args = *ArgArr;

	FString Err;
	if (!UEMCPPIE::FPIEActorPuppet::CallFunction(Actor, Func, Args, Err))
	{
		return MCPError(Err);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("target"), Target);
	Result->SetStringField(TEXT("func"), Func);
	Result->SetBoolField(TEXT("called"), true);
	return MCPResult(Result);
}
