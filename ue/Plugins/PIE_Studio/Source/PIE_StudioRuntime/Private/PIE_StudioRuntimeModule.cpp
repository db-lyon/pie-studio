#include "PIE_StudioRuntimeModule.h"
#include "Modules/ModuleManager.h"
#include "PIEInputInjector.h"

DEFINE_LOG_CATEGORY(LogPIEStudioRuntime);
IMPLEMENT_MODULE(FPIE_StudioRuntimeModule, PIE_StudioRuntime)

void FPIE_StudioRuntimeModule::StartupModule()
{
	UEMCPPIE::FPIEInputInjector::Init();
}

void FPIE_StudioRuntimeModule::ShutdownModule()
{
	UEMCPPIE::FPIEInputInjector::Shutdown();
}
