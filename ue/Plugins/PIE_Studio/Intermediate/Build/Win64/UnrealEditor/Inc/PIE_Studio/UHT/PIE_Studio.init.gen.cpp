// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodePIE_Studio_init() {}
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
	static FPackageRegistrationInfo Z_Registration_Info_UPackage__Script_PIE_Studio;
	FORCENOINLINE UPackage* Z_Construct_UPackage__Script_PIE_Studio(ETypeConstructPhase)
	{
		if (!Z_Registration_Info_UPackage__Script_PIE_Studio.OuterSingleton)
		{
		static const UECodeGen_Private::FPackageParams PackageParams = {
			"/Script/PIE_Studio",
			nullptr,
			0,
			PKG_CompiledIn | 0x00000040,
			0x62B36BF7,
			0x73C544FB,
			METADATA_PARAMS(0, nullptr)
		};
		UECodeGen_Private::ConstructUPackage(Z_Registration_Info_UPackage__Script_PIE_Studio.OuterSingleton, PackageParams);
	}
	return Z_Registration_Info_UPackage__Script_PIE_Studio.OuterSingleton;
}
static FRegisterCompiledInInfo Z_CompiledInDeferPackage_UPackage__Script_PIE_Studio(Z_Construct_UPackage__Script_PIE_Studio, TEXT("/Script/PIE_Studio"), Z_Registration_Info_UPackage__Script_PIE_Studio, CONSTRUCT_RELOAD_VERSION_INFO(FPackageReloadVersionInfo, 0x62B36BF7, 0x73C544FB));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
