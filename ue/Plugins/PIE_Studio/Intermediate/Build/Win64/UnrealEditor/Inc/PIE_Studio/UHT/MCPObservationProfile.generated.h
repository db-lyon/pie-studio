// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "PIE/MCPObservationProfile.h"

#ifdef PIE_STUDIO_MCPObservationProfile_generated_h
#error "MCPObservationProfile.generated.h already included, missing '#pragma once' in MCPObservationProfile.h"
#endif
#define PIE_STUDIO_MCPObservationProfile_generated_h

#include "UObject/ObjectMacros.h"
#include "UObject/ReflectedTypeAccessors.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

// ********** Begin ScriptStruct FMCPTrackedValueEntry *********************************************
struct Z_Construct_UScriptStruct_FMCPTrackedValueEntry_Statics;
PIE_STUDIO_API UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedValueEntry(ETypeConstructPhase);

#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_10_GENERATED_BODY \
	friend struct ::Z_Construct_UScriptStruct_FMCPTrackedValueEntry_Statics; \
	UE_NODEBUG static UScriptStruct* StaticStruct() { return Z_Construct_UScriptStruct_FMCPTrackedValueEntry(ETypeConstructPhase::Inner); }


struct FMCPTrackedValueEntry;
// ********** End ScriptStruct FMCPTrackedValueEntry ***********************************************

// ********** Begin ScriptStruct FMCPTrackedActorEntry *********************************************
struct Z_Construct_UScriptStruct_FMCPTrackedActorEntry_Statics;
PIE_STUDIO_API UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedActorEntry(ETypeConstructPhase);

#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_24_GENERATED_BODY \
	friend struct ::Z_Construct_UScriptStruct_FMCPTrackedActorEntry_Statics; \
	UE_NODEBUG static UScriptStruct* StaticStruct() { return Z_Construct_UScriptStruct_FMCPTrackedActorEntry(ETypeConstructPhase::Inner); }


struct FMCPTrackedActorEntry;
// ********** End ScriptStruct FMCPTrackedActorEntry ***********************************************

// ********** Begin Class UMCPObservationProfile ***************************************************
struct Z_Construct_UClass_UMCPObservationProfile_Statics;
PIE_STUDIO_API UClass* Z_Construct_UClass_UMCPObservationProfile(ETypeConstructPhase);

#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_34_INCLASS_NO_PURE_DECLS \
private: \
	friend struct ::Z_Construct_UClass_UMCPObservationProfile_Statics; \
	friend PIE_STUDIO_API UClass* ::Z_Construct_UClass_UMCPObservationProfile(ETypeConstructPhase); \
public: \
	DECLARE_CLASS2(UMCPObservationProfile, UDataAsset, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/PIE_Studio"), Z_Construct_UClass_UMCPObservationProfile) \
	DECLARE_SERIALIZER(UMCPObservationProfile)


#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_34_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API UMCPObservationProfile(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
	/** Deleted move- and copy-constructors, should never be used */ \
	UMCPObservationProfile(UMCPObservationProfile&&) = delete; \
	UMCPObservationProfile(const UMCPObservationProfile&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UMCPObservationProfile); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UMCPObservationProfile); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UMCPObservationProfile) \
	NO_API virtual ~UMCPObservationProfile();


#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_31_PROLOG
#define FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_34_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_34_INCLASS_NO_PURE_DECLS \
	FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h_34_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class UMCPObservationProfile;

// ********** End Class UMCPObservationProfile *****************************************************

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h

PRAGMA_ENABLE_DEPRECATION_WARNINGS
