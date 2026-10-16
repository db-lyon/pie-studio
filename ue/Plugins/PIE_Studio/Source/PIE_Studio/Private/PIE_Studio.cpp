#include "PIE_StudioModule.h"
#include "Modules/ModuleManager.h"
#include "MCPHandlerRegistration.h"
#include "Handlers/GameplayHandlers.h"
#include "PIEInputInjector.h"
#include "PIE/PIERemotePlayers.h"
#include "PIE/PIEUatRunner.h"
#include "PIE/PIEWorldResolver.h"
#include "Engine/LocalPlayer.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "PIE/PIESessionLog.h"
#include "UI/SMCPPIEPanel.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogPIEStudio);
IMPLEMENT_MODULE(FPIE_StudioModule, PIE_Studio)

namespace
{
	// An action core now provides, kept for one release. Every result names the replacement.
	UEMCP::FExternalHandlerFn Deprecated(UEMCP::FExternalHandlerFn Handler, const TCHAR* Replacement)
	{
		const FString Note = FString::Printf(TEXT("Deprecated: use %s. This pie action is removed in the next release."), Replacement);
		return [Handler, Note](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonValue>
		{
			TSharedPtr<FJsonValue> Result = Handler(Params);
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Result.IsValid() && Result->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				(*Obj)->SetStringField(TEXT("deprecated"), Note);
			}
			return Result;
		};
	}
}

void FPIE_StudioModule::StartupModule()
{
	// The injector lives in PIE_StudioRuntime (so clients in their own process run it too); in the editor it resolves
	// (pie_instance, client_id) through the PIE world resolver.
	UEMCPPIE::FPIEInputInjector::SetPlayerResolver([](int32 PIEInstance, int32 ClientIndex, FString& OutError) -> ULocalPlayer*
	{
		UEMCPPIE::PIEWorldResolver::FPlayerTarget Target;
		return UEMCPPIE::PIEWorldResolver::ResolvePlayer(PIEInstance, ClientIndex, Target, OutError) ? Target.LocalPlayer : nullptr;
	});
	UEMCPPIE::PIERemotePlayers::Init();
	UEMCPPIE::FPIEInputRecorder::Get().Init();
	UEMCPPIE::FPIEInputReplayer::Get().Init();
	UEMCPPIE::FPIEObserver::Get().Init();
	UEMCPPIE::FPIESessionLog::Get().Init();
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

	// Multiplayer players
	UEMCP::RegisterExternalHandler(TEXT("clients"), &FGameplayHandlers::PieClients);
	UEMCP::RegisterExternalHandler(TEXT("console"), &FGameplayHandlers::PieConsole);
	UEMCP::RegisterExternalHandler(TEXT("remote_result"), &FGameplayHandlers::PieRemoteResult);

	// Multiplayer acceptance runs
	UEMCP::RegisterExternalHandler(TEXT("uat_run"), &FGameplayHandlers::PieUatRun);
	UEMCP::RegisterExternalHandler(TEXT("uat_status"), &FGameplayHandlers::PieUatStatus);
	UEMCP::RegisterExternalHandler(TEXT("uat_abort"), &FGameplayHandlers::PieUatAbort);

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
	UEMCP::RegisterExternalHandler(TEXT("replay_run"), &FGameplayHandlers::PieReplayRun);
	UEMCP::RegisterExternalHandler(TEXT("replay_disarm"), &FGameplayHandlers::PieReplayDisarm);
	UEMCP::RegisterExternalHandler(TEXT("replay_stop"), &FGameplayHandlers::PieReplayStop);
	UEMCP::RegisterExternalHandler(TEXT("replay_status"), &FGameplayHandlers::PieReplayStatus);
	UEMCP::RegisterExternalHandler(TEXT("replay_analyze"), &FGameplayHandlers::PieReplayAnalyze);
	UEMCP::RegisterExternalHandler(TEXT("replay_state"), &FGameplayHandlers::PieReplayState);

	// Diff / Snapshot
	UEMCP::RegisterExternalHandler(TEXT("record_diff"), &FGameplayHandlers::PieRecordDiff);
	UEMCP::RegisterExternalHandler(TEXT("snapshot"), Deprecated(&FGameplayHandlers::PieSnapshot, TEXT("editor(get_object_properties, world=pie)")));

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
	UEMCP::RegisterExternalHandler(TEXT("subsystem_state"), Deprecated(&FGameplayHandlers::GetPieSubsystemState, TEXT("editor(get_object_properties, target=subsystem)")));

	// Session log / errors + standalone capture
	UEMCP::RegisterExternalHandler(TEXT("session_errors"), &FGameplayHandlers::PieSessionErrors);
	UEMCP::RegisterExternalHandler(TEXT("session_log"), &FGameplayHandlers::PieSessionLog);
	UEMCP::RegisterExternalHandler(TEXT("capture"), Deprecated(&FGameplayHandlers::PieCapture, TEXT("editor(capture_screenshot, target=pie) or editor(capture_scene_png)")));

	// Profiling
	UEMCP::RegisterExternalHandler(TEXT("trace_start"), Deprecated(&FGameplayHandlers::PieTraceStart, TEXT("editor(start_trace)")));
	UEMCP::RegisterExternalHandler(TEXT("trace_stop"), Deprecated(&FGameplayHandlers::PieTraceStop, TEXT("editor(stop_trace)")));
	UEMCP::RegisterExternalHandler(TEXT("perf_summary"), &FGameplayHandlers::PiePerfSummary);

	// Reproduction tests
	UEMCP::RegisterExternalHandler(TEXT("test_scaffold"), &FGameplayHandlers::PieTestScaffold);
	UEMCP::RegisterExternalHandler(TEXT("test_run"), &FGameplayHandlers::PieTestRun);
	UEMCP::RegisterExternalHandler(TEXT("test_list"), &FGameplayHandlers::PieTestList);

	// Assertion layer (Roadmap v2, Phase A)
	UEMCP::RegisterExternalHandler(TEXT("assert_eval"), &FGameplayHandlers::PieAssertEval);

	// Actor puppeteering (Roadmap v2, F1)
	UEMCP::RegisterExternalHandler(TEXT("actor_spawn"), Deprecated(&FGameplayHandlers::PieActorSpawn, TEXT("level(spawn_transient_actor, world=pie)")));
	UEMCP::RegisterExternalHandler(TEXT("actor_destroy"), Deprecated(&FGameplayHandlers::PieActorDestroy, TEXT("level(destroy_transient_actor)")));
	UEMCP::RegisterExternalHandler(TEXT("actor_set"), Deprecated(&FGameplayHandlers::PieActorSet, TEXT("editor(set_object_property, world=pie)")));
	UEMCP::RegisterExternalHandler(TEXT("actor_call"), Deprecated(&FGameplayHandlers::PieActorCall, TEXT("editor(invoke_object_function, world=pie)")));

	// Declarative scenario (Roadmap v2, F2)
	UEMCP::RegisterExternalHandler(TEXT("scenario_scaffold"), &FGameplayHandlers::PieScenarioScaffold);
	UEMCP::RegisterExternalHandler(TEXT("scenario_validate"), &FGameplayHandlers::PieScenarioValidate);

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
				    || UEMCPPIE::FPIEObserver::Get().IsActive()
				    || UEMCPPIE::FPIEUatRunner::Get().IsRunning();
			});
			GEditor->ShouldDisableCPUThrottlingDelegates.Add(Suppress);
			return false;
		})
	);

	UE_LOG(LogPIEStudio, Log, TEXT("[pie-studio] Registered %d handlers"), 45);
}

void FPIE_StudioModule::ShutdownModule()
{
	SMCPPIEPanel::UnregisterToolbarButton();
	SMCPPIEPanel::UnregisterTab();

	// Unregister all external handlers
	UEMCP::UnregisterExternalHandler(TEXT("inject_input"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_start"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_update"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("inject_input_tape"));
	UEMCP::UnregisterExternalHandler(TEXT("clients"));
	UEMCP::UnregisterExternalHandler(TEXT("console"));
	UEMCP::UnregisterExternalHandler(TEXT("remote_result"));
	UEMCP::UnregisterExternalHandler(TEXT("uat_run"));
	UEMCP::UnregisterExternalHandler(TEXT("uat_status"));
	UEMCP::UnregisterExternalHandler(TEXT("uat_abort"));
	UEMCP::UnregisterExternalHandler(TEXT("record_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("record_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("record_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("record_status"));
	UEMCP::UnregisterExternalHandler(TEXT("record_list"));
	UEMCP::UnregisterExternalHandler(TEXT("record_read"));
	UEMCP::UnregisterExternalHandler(TEXT("record_delete"));
	UEMCP::UnregisterExternalHandler(TEXT("mark"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_arm"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_run"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_disarm"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_status"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_analyze"));
	UEMCP::UnregisterExternalHandler(TEXT("replay_state"));
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
	UEMCP::UnregisterExternalHandler(TEXT("session_errors"));
	UEMCP::UnregisterExternalHandler(TEXT("session_log"));
	UEMCP::UnregisterExternalHandler(TEXT("capture"));
	UEMCP::UnregisterExternalHandler(TEXT("trace_start"));
	UEMCP::UnregisterExternalHandler(TEXT("trace_stop"));
	UEMCP::UnregisterExternalHandler(TEXT("perf_summary"));
	UEMCP::UnregisterExternalHandler(TEXT("test_scaffold"));
	UEMCP::UnregisterExternalHandler(TEXT("test_run"));
	UEMCP::UnregisterExternalHandler(TEXT("test_list"));
	UEMCP::UnregisterExternalHandler(TEXT("assert_eval"));
	UEMCP::UnregisterExternalHandler(TEXT("actor_spawn"));
	UEMCP::UnregisterExternalHandler(TEXT("actor_destroy"));
	UEMCP::UnregisterExternalHandler(TEXT("actor_set"));
	UEMCP::UnregisterExternalHandler(TEXT("actor_call"));
	UEMCP::UnregisterExternalHandler(TEXT("scenario_scaffold"));
	UEMCP::UnregisterExternalHandler(TEXT("scenario_validate"));

	UEMCPPIE::FPIESessionLog::Get().Shutdown();
	UEMCPPIE::FPIEObserver::Get().Shutdown();
	UEMCPPIE::FPIEInputReplayer::Get().Shutdown();
	UEMCPPIE::FPIEInputRecorder::Get().Shutdown();
	// The injector itself belongs to PIE_StudioRuntime and shuts down with it; only the editor's resolver is withdrawn.
	UEMCPPIE::FPIEInputInjector::SetPlayerResolver(nullptr);
	UEMCPPIE::PIERemotePlayers::Shutdown();
	UEMCPPIE::FPIEUatRunner::Get().Shutdown();
}
