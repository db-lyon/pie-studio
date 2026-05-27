#include "PIE_TransportModule.h"
#include "Modules/ModuleManager.h"
#include "MCPHandlerRegistration.h"
#include "Handlers/GameplayHandlers.h"
#include "PIE/PIEInputInjector.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "UI/SMCPPIEPanel.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogPIETransport);
IMPLEMENT_MODULE(FPIE_TransportModule, PIE_Transport)

void FPIE_TransportModule::StartupModule()
{
	UEMCPPIE::FPIEInputInjector::Init();
	UEMCPPIE::FPIEInputRecorder::Get().Init();
	UEMCPPIE::FPIEInputReplayer::Get().Init();
	UEMCPPIE::FPIEObserver::Get().Init();
	SMCPPIEPanel::RegisterTab();
	SMCPPIEPanel::RegisterToolbarButton();

	FEditorDelegates::EndPIE.AddLambda([](bool)
	{
		UEMCPPIE::FPIEInputInjector::OnPIEEnded();
	});

	// Register all PIE handlers on the bridge via the external API.
	// Bare names here; the plugin actionPrefix "pie" is prepended by ue-mcp
	// to form the MCP tool name (e.g. record_arm -> pie_record_arm).

	// Input injection
	UEMCP::RegisterExternalHandler(TEXT("inject_input"), &FGameplayHandlers::InjectInput);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_start"), &FGameplayHandlers::InjectInputStart);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_update"), &FGameplayHandlers::InjectInputUpdate);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_stop"), &FGameplayHandlers::InjectInputStop);
	UEMCP::RegisterExternalHandler(TEXT("inject_input_tape"), &FGameplayHandlers::InjectInputTape);

	// Recording
	UEMCP::RegisterExternalHandler(TEXT("record_arm"), &FGameplayHandlers::PieRecordArm);
	UEMCP::RegisterExternalHandler(TEXT("record_disarm"), &FGameplayHandlers::PieRecordDisarm);
	UEMCP::RegisterExternalHandler(TEXT("record_stop"), &FGameplayHandlers::PieRecordStop);
	UEMCP::RegisterExternalHandler(TEXT("record_status"), &FGameplayHandlers::PieRecordStatus);
	UEMCP::RegisterExternalHandler(TEXT("record_list"), &FGameplayHandlers::PieRecordList);
	UEMCP::RegisterExternalHandler(TEXT("record_read"), &FGameplayHandlers::PieRecordRead);
	UEMCP::RegisterExternalHandler(TEXT("record_delete"), &FGameplayHandlers::PieRecordDelete);
	UEMCP::RegisterExternalHandler(TEXT("mark"), &FGameplayHandlers::PieMark);

	// Replay
	UEMCP::RegisterExternalHandler(TEXT("replay_arm"), &FGameplayHandlers::PieReplayArm);
	UEMCP::RegisterExternalHandler(TEXT("replay_disarm"), &FGameplayHandlers::PieReplayDisarm);
	UEMCP::RegisterExternalHandler(TEXT("replay_stop"), &FGameplayHandlers::PieReplayStop);
	UEMCP::RegisterExternalHandler(TEXT("replay_status"), &FGameplayHandlers::PieReplayStatus);

	// Diff / Snapshot
	UEMCP::RegisterExternalHandler(TEXT("record_diff"), &FGameplayHandlers::PieRecordDiff);
	UEMCP::RegisterExternalHandler(TEXT("snapshot"), &FGameplayHandlers::PieSnapshot);

	// Observation profiles
	UEMCP::RegisterExternalHandler(TEXT("profile_create"), &FGameplayHandlers::PieProfileCreate);
	UEMCP::RegisterExternalHandler(TEXT("profile_read"), &FGameplayHandlers::PieProfileRead);
	UEMCP::RegisterExternalHandler(TEXT("profile_update"), &FGameplayHandlers::PieProfileUpdate);
	UEMCP::RegisterExternalHandler(TEXT("profile_delete"), &FGameplayHandlers::PieProfileDelete);
	UEMCP::RegisterExternalHandler(TEXT("profile_list"), &FGameplayHandlers::PieProfileList);

	// Observer
	UEMCP::RegisterExternalHandler(TEXT("observe_arm"), &FGameplayHandlers::PieObserveArm);
	UEMCP::RegisterExternalHandler(TEXT("observe_disarm"), &FGameplayHandlers::PieObserveDisarm);
	UEMCP::RegisterExternalHandler(TEXT("observe_stop"), &FGameplayHandlers::PieObserveStop);
	UEMCP::RegisterExternalHandler(TEXT("observe_status"), &FGameplayHandlers::PieObserveStatus);
	UEMCP::RegisterExternalHandler(TEXT("observe_list"), &FGameplayHandlers::PieObserveList);
	UEMCP::RegisterExternalHandler(TEXT("observe_read"), &FGameplayHandlers::PieObserveRead);

	// PIE inspection
	UEMCP::RegisterExternalHandler(TEXT("anim_state"), &FGameplayHandlers::GetPieAnimState);
	UEMCP::RegisterExternalHandler(TEXT("anim_properties"), &FGameplayHandlers::GetPieAnimProperties);
	UEMCP::RegisterExternalHandler(TEXT("subsystem_state"), &FGameplayHandlers::GetPieSubsystemState);

	// CPU throttle suppression while recording/replaying/observing
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor) return true;
			bool bHasWorld = false;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World()) { bHasWorld = true; break; }
			}
			if (!bHasWorld) return true;

			UEditorEngine::FShouldDisableCPUThrottling Suppress;
			Suppress.BindLambda([]() -> bool
			{
				return UEMCPPIE::FPIEInputRecorder::Get().IsActive()
				    || UEMCPPIE::FPIEInputReplayer::Get().IsActive()
				    || UEMCPPIE::FPIEObserver::Get().IsActive();
			});
			GEditor->ShouldDisableCPUThrottlingDelegates.Add(Suppress);
			return false;
		})
	);

	UE_LOG(LogPIETransport, Log, TEXT("[pie-transport] Registered %d handlers"), 33);
}

void FPIE_TransportModule::ShutdownModule()
{
	SMCPPIEPanel::UnregisterToolbarButton();
	SMCPPIEPanel::UnregisterTab();

	// Unregister all external handlers
	UEMCP::UnregisterExternalHandler(TEXT("inject_input"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_start"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_update"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_tape"));
	UEMCP::UnregisterExternalHandler(TEXT("record_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("record_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("record_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("record_status"));
	UEMCP::UnregisterExternalHandler(TEXT("record_list"));
	UEMCP::UnregisterExternalHandler(TEXT("record_read"));
	UEMCP::UnregisterExternalHandler(TEXT("record_delete"));
	UEMCP::UnregisterExternalHandler(TEXT("mark"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_status"));
	UEMCP::UnregisterExternalHandler(TEXT("record_diff"));
	UEMCP::UnregisterExternalHandler(TEXT("snapshot"));
	UEMCP::UnregisterExternalHandler(TEXT("profile_create"));
	UEMCP::UnregisterExternalHandler(TEXT("profile_read"));
	UEMCP::UnregisterExternalHandler(TEXT("profile_update"));
	UEMCP::UnregisterExternalHandler(TEXT("profile_delete"));
	UEMCP::UnregisterExternalHandler(TEXT("profile_list"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_status"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_list"));
	UEMCP::UnregisterExternalHandler(TEXT("observe_read"));
	UEMCP::UnregisterExternalHandler(TEXT("anim_state"));
	UEMCP::UnregisterExternalHandler(TEXT("anim_properties"));
	UEMCP::UnregisterExternalHandler(TEXT("subsystem_state"));

	UEMCPPIE::FPIEObserver::Get().Shutdown();
	UEMCPPIE::FPIEInputReplayer::Get().Shutdown();
	UEMCPPIE::FPIEInputRecorder::Get().Shutdown();
	UEMCPPIE::FPIEInputInjector::Shutdown();
}
