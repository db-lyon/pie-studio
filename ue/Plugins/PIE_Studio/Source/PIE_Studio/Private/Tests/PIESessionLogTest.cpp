// Automation coverage for FPIESessionLog (roadmap item 1a).
// Drives a session directly (no PIE required), emits log lines through GLog, and
// asserts they are captured in memory and written to session_errors.json.
//
// Runs under the engine's automation framework:
//   Automation RunTests PIEStudio.SessionLog
// Cannot be executed on this workstation (no ue-mcp host to compile against); it
// exists so the behaviour is verified on the user's build.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PIE/PIESessionLog.h"
#include "PIE_StudioModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPIESessionLogCaptureTest,
	"PIEStudio.SessionLog.CapturesErrorsAndWritesArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPIESessionLogCaptureTest::RunTest(const FString& /*Parameters*/)
{
	using namespace UEMCPPIE;

	FPIESessionLog& Log = FPIESessionLog::Get();
	Log.Init(); // idempotent; ensures the output device is registered on GLog

	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("MCPSessionsTest") / Token;

	Log.BeginSession(Dir);
	TestTrue(TEXT("session is active after BeginSession"), Log.IsActive());

	UE_LOG(LogPIEStudio, Error, TEXT("PIESESSIONLOGTEST_ERR_%s"), *Token);
	UE_LOG(LogPIEStudio, Warning, TEXT("PIESESSIONLOGTEST_WARN_%s"), *Token);
	UE_LOG(LogPIEStudio, Log, TEXT("PIESESSIONLOGTEST_INFO_%s"), *Token);

	// Force the redirector to route buffered lines to our device before we read.
	if (GLog)
	{
		GLog->Flush();
	}

	// In-memory summary should see at least the error and the warning.
	{
		const TSharedPtr<FJsonObject> Summary = Log.BuildErrorSummary(ELogVerbosity::Warning);
		TestTrue(TEXT("summary object built"), Summary.IsValid());
		if (Summary.IsValid())
		{
			int32 ErrCount = 0, WarnCount = 0;
			Summary->TryGetNumberField(TEXT("error_count"), ErrCount);
			Summary->TryGetNumberField(TEXT("warning_count"), WarnCount);
			TestTrue(TEXT("captured at least one error"), ErrCount >= 1);
			TestTrue(TEXT("captured at least one warning"), WarnCount >= 1);
		}
	}

	// Error-only floor must exclude the warning line from the count.
	{
		const TSharedPtr<FJsonObject> ErrOnly = Log.BuildErrorSummary(ELogVerbosity::Error);
		int32 WarnCount = -1;
		if (ErrOnly.IsValid())
		{
			ErrOnly->TryGetNumberField(TEXT("warning_count"), WarnCount);
		}
		TestEqual(TEXT("error-only floor reports zero warnings"), WarnCount, 0);
	}

	// Query the raw ring buffer for our info line.
	{
		const TArray<FPIESessionLog::FLogLine> Lines =
			Log.QueryLines(ELogVerbosity::Log, NAME_None, TEXT("PIESESSIONLOGTEST_INFO_"), 0, 50);
		TestTrue(TEXT("info line found in ring buffer"), Lines.Num() >= 1);
	}

	const FString Finalised = Log.EndSession();
	TestEqual(TEXT("EndSession returns the session dir"), Finalised, Dir);
	TestFalse(TEXT("session inactive after EndSession"), Log.IsActive());

	// Artifacts on disk.
	FString ErrorsJson;
	const bool bReadErrors = FFileHelper::LoadFileToString(ErrorsJson, *(Dir / TEXT("session_errors.json")));
	TestTrue(TEXT("session_errors.json written"), bReadErrors);
	TestTrue(TEXT("session_errors.json contains the error token"),
		ErrorsJson.Contains(FString::Printf(TEXT("PIESESSIONLOGTEST_ERR_%s"), *Token)));

	FString LogJsonl;
	const bool bReadLog = FFileHelper::LoadFileToString(LogJsonl, *(Dir / TEXT("session_log.jsonl")));
	TestTrue(TEXT("session_log.jsonl written"), bReadLog);

	// Cleanup.
	IFileManager::Get().DeleteDirectory(*Dir, false, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
