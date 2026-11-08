// Multiplayer PIE players: listing them, running console commands in a player's process, and reading what a
// relayed request did. Remote players are reached through PIERemotePlayers (see that header).

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIERemotePlayers.h"
#include "PIEStudioRemoteControl.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

TSharedPtr<FJsonValue> FGameplayHandlers::PieClients(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	UWorld* Server = UEMCPPIE::PIERemotePlayers::FindServerWorld();

	TArray<TSharedPtr<FJsonValue>> Players;
	for (const UEMCPPIE::PIERemotePlayers::FRemotePlayer& P : UEMCPPIE::PIERemotePlayers::List())
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("client"), P.Client);
		Entry->SetStringField(TEXT("player"), P.PlayerName);
		Entry->SetStringField(TEXT("controller"), GetNameSafe(P.PlayerController));
		Entry->SetBoolField(TEXT("ready"), P.bReady);
		if (!P.LogFile.IsEmpty()) Entry->SetStringField(TEXT("log_file"), P.LogFile);
		Players.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("networked"), Server != nullptr);
	Result->SetStringField(TEXT("server_net_mode"), !Server ? TEXT("none")
		: Server->GetNetMode() == NM_ListenServer ? TEXT("listen_server") : TEXT("dedicated_server"));
	Result->SetBoolField(TEXT("host_player"), Server && Server->GetNetMode() == NM_ListenServer);
	Result->SetArrayField(TEXT("clients"), Players);
	Result->SetStringField(TEXT("addressing"),
		TEXT("client 0 = the editor's own player (listen server); clients 1..N = remote players in join order"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieConsole(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Command;
	if (auto E = RequireString(Params, TEXT("command"), Command)) return E;
	const int32 Client = OptionalInt(Params, TEXT("client"), 0);

	if (Client > 0)
	{
		FString Err;
		UPIEStudioRemoteControl* Remote = UEMCPPIE::PIERemotePlayers::Find(Client, Err);
		if (!Remote) return MCPError(Err);
		const FString Id = UEMCPPIE::PIERemotePlayers::NewId(TEXT("console"));
		Remote->SendConsole(Id, Command);
		UEMCPPIE::PIERemotePlayers::RememberId(Id, Remote);
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("request_id"), Id);
		Result->SetNumberField(TEXT("client"), Client);
		Result->SetStringField(TEXT("command"), Command);
		Result->SetBoolField(TEXT("relayed"), true);
		return MCPResult(Result);
	}

	// The editor's own player: its play world, through its controller where there is one so player-scoped commands
	// (Net PktLoss, cheats) reach the right connection.
	UWorld* World = UEMCPPIE::PIERemotePlayers::FindServerWorld();
	if (!World && GEditor)
	{
		World = GEditor->PlayWorld;
	}
	if (!World)
	{
		return MCPError(TEXT("console: no PIE session is running"));
	}
	if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->IsLocalController())
	{
		PC->ConsoleCommand(Command, /*bWriteToLog*/ true);
	}
	else
	{
		GEngine->Exec(World, *Command);
	}
	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("client"), 0);
	Result->SetStringField(TEXT("command"), Command);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRemoteResult(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireStringAlt(Params, TEXT("injection_id"), TEXT("request_id"), Id)) return E;

	UPIEStudioRemoteControl* Remote = UEMCPPIE::PIERemotePlayers::FindById(Id);
	if (!Remote)
	{
		return MCPError(FString::Printf(TEXT("remote_result: '%s' is not a request relayed in this PIE session"), *Id));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("request_id"), Id);
	if (const FPIEStudioRemoteResult* R = Remote->FindResult(Id))
	{
		Result->SetBoolField(TEXT("reported"), true);
		Result->SetBoolField(TEXT("ok"), R->bOk);
		Result->SetBoolField(TEXT("finished"), R->bFinished);
		if (!R->Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), R->Error);
		}
	}
	else
	{
		Result->SetBoolField(TEXT("reported"), false);
	}
	return MCPResult(Result);
}
