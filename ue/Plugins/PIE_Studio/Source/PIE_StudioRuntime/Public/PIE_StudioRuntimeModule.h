#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

PIE_STUDIORUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogPIEStudioRuntime, Log, All);

class FPIE_StudioRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
