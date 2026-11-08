#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UPIEStudioRemoteControl;
class UWorld;

/**
 * The players of a multiplayer PIE session that the editor reaches through the game connection.
 *
 * The editor is the server (listen or dedicated). Every player whose controller is not local to it gets a
 * UPIEStudioRemoteControl at login, and is addressed by `client`: 1..N in join order. Client 0 is the editor's own
 * player on a listen server. This works the same whether clients run in the editor process or in their own.
 */
namespace UEMCPPIE::PIERemotePlayers
{
	struct FRemotePlayer
	{
		int32 Client = 0;
		APlayerController* PlayerController = nullptr;
		UPIEStudioRemoteControl* Control = nullptr;
		FString PlayerName;
		FString LogFile;
		bool bReady = false;
	};

	void Init();
	void Shutdown();

	/** The PIE world acting as server, or null when no networked PIE session is running. */
	UWorld* FindServerWorld();

	/** Every remote player, numbered from 1 in join order. */
	TArray<FRemotePlayer> List();

	/** The remote control for `Client`, which must be ready; null with an error otherwise. */
	UPIEStudioRemoteControl* Find(int32 Client, FString& OutError);

	/** Records which remote player an injection id was sent to, so updates and stops reach the same player. */
	void RememberId(const FString& Id, UPIEStudioRemoteControl* Control);
	UPIEStudioRemoteControl* FindById(const FString& Id);

	/** A new request id, unique for the session. */
	FString NewId(const TCHAR* Prefix);
}
