// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "PIE/MCPObservationProfile.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_UOBJECT");
void EmptyLinkFunctionForGeneratedCodeMCPObservationProfile() {}

// ********** Begin Cross Module References ********************************************************
ENGINE_API UClass* Z_Construct_UClass_UDataAsset(ETypeConstructPhase);
// ********** End Cross Module References **********************************************************

// ********** Begin Same Module References *********************************************************
UPackage* Z_Construct_UPackage__Script_PIE_Studio(ETypeConstructPhase);
PIE_STUDIO_API UClass* Z_Construct_UClass_UMCPObservationProfile(ETypeConstructPhase);
PIE_STUDIO_API UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedActorEntry(ETypeConstructPhase);
PIE_STUDIO_API UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedValueEntry(ETypeConstructPhase);
PIE_STUDIO_API UClass* Z_Construct_UClass_UMCPObservationProfile(ETypeConstructPhase);
// ********** End Same Module References ***********************************************************
#define UHT_STRUCT_BASE(INIT) UE::CodeGen::ConstInit::TCompiledInObjectPtr<const FStructBaseChain>(UE::Private::AsStructBaseChain(INIT))

// ********** Begin ScriptStruct FMCPTrackedValueEntry *********************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_Construct_UScriptStruct_FMCPTrackedValueEntry_Statics
struct UHT_STATICS
{
	static inline consteval int32 GetStructSize() { return DataSizeOf<FMCPTrackedValueEntry>(); }
	static inline consteval int16 GetStructAlignment() { return alignof(FMCPTrackedValueEntry); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Type_MetaData[] = {
		{ "BlueprintType", "true" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Path_MetaData[] = {
		{ "Category", "Tracking" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Property path to observe (e.g. CharacterMovement.Velocity.X)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_DriftThreshold_MetaData[] = {
		{ "Category", "Tracking" },
		{ "ClampMin", "0" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Override drift threshold for this value. 0 = use profile default." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FMCPTrackedValueEntry constinit property declarations *************
	static const UECodeGen_Private::FStrPropertyParams NewProp_Path;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_DriftThreshold;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FMCPTrackedValueEntry constinit property declarations ***************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FMCPTrackedValueEntry>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct UHT_STATICS

// ********** Begin ScriptStruct FMCPTrackedValueEntry Property Definitions ************************
const UECodeGen_Private::FStrPropertyParams UHT_STATICS::NewProp_Path = { "Path", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Str, nullptr, nullptr, 1, STRUCT_OFFSET(FMCPTrackedValueEntry, Path), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Path_MetaData), NewProp_Path_MetaData) };
const UECodeGen_Private::FFloatPropertyParams UHT_STATICS::NewProp_DriftThreshold = { "DriftThreshold", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, nullptr, nullptr, 1, STRUCT_OFFSET(FMCPTrackedValueEntry, DriftThreshold), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_DriftThreshold_MetaData), NewProp_DriftThreshold_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const UHT_STATICS::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_Path,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_DriftThreshold,
};
static_assert(UE_ARRAY_COUNT(UHT_STATICS::PropPointers) < 2048);
// ********** End ScriptStruct FMCPTrackedValueEntry Property Definitions **************************
const UECodeGen_Private::FStructParams UHT_STATICS::StructParams = {
	(FTypeConstructFunc*)Z_Construct_UPackage__Script_PIE_Studio,
	nullptr,
	&NewStructOps,
	"MCPTrackedValueEntry",
	UHT_STATICS::PropPointers,
	UE_ARRAY_COUNT(UHT_STATICS::PropPointers),
	DataSizeOf<FMCPTrackedValueEntry>(),
	alignof(FMCPTrackedValueEntry),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000001),
	METADATA_PARAMS(UE_ARRAY_COUNT(UHT_STATICS::Type_MetaData), UHT_STATICS::Type_MetaData)
};
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry;
UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedValueEntry(ETypeConstructPhase Phase)
{
	if (Phase == ETypeConstructPhase::Outer)
	{
		if (!Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.OuterSingleton)
		{
			Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FMCPTrackedValueEntry, (UObject*)Z_Construct_UPackage__Script_PIE_Studio(ETypeConstructPhase::Outer), TEXT("MCPTrackedValueEntry"));
		}
		return Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.OuterSingleton;
	}
	if (!Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.InnerSingleton, UHT_STATICS::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry.InnerSingleton);
}
#undef UHT_STATICS
// ********** End ScriptStruct FMCPTrackedValueEntry ***********************************************

// ********** Begin ScriptStruct FMCPTrackedActorEntry *********************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_Construct_UScriptStruct_FMCPTrackedActorEntry_Statics
struct UHT_STATICS
{
	static inline consteval int32 GetStructSize() { return DataSizeOf<FMCPTrackedActorEntry>(); }
	static inline consteval int16 GetStructAlignment() { return alignof(FMCPTrackedActorEntry); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Type_MetaData[] = {
		{ "BlueprintType", "true" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ActorId_MetaData[] = {
		{ "Category", "Tracking" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Actor label or unique ID to track in the PIE world." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FMCPTrackedActorEntry constinit property declarations *************
	static const UECodeGen_Private::FStrPropertyParams NewProp_ActorId;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FMCPTrackedActorEntry constinit property declarations ***************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FMCPTrackedActorEntry>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct UHT_STATICS

// ********** Begin ScriptStruct FMCPTrackedActorEntry Property Definitions ************************
const UECodeGen_Private::FStrPropertyParams UHT_STATICS::NewProp_ActorId = { "ActorId", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Str, nullptr, nullptr, 1, STRUCT_OFFSET(FMCPTrackedActorEntry, ActorId), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ActorId_MetaData), NewProp_ActorId_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const UHT_STATICS::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_ActorId,
};
static_assert(UE_ARRAY_COUNT(UHT_STATICS::PropPointers) < 2048);
// ********** End ScriptStruct FMCPTrackedActorEntry Property Definitions **************************
const UECodeGen_Private::FStructParams UHT_STATICS::StructParams = {
	(FTypeConstructFunc*)Z_Construct_UPackage__Script_PIE_Studio,
	nullptr,
	&NewStructOps,
	"MCPTrackedActorEntry",
	UHT_STATICS::PropPointers,
	UE_ARRAY_COUNT(UHT_STATICS::PropPointers),
	DataSizeOf<FMCPTrackedActorEntry>(),
	alignof(FMCPTrackedActorEntry),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000001),
	METADATA_PARAMS(UE_ARRAY_COUNT(UHT_STATICS::Type_MetaData), UHT_STATICS::Type_MetaData)
};
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry;
UScriptStruct* Z_Construct_UScriptStruct_FMCPTrackedActorEntry(ETypeConstructPhase Phase)
{
	if (Phase == ETypeConstructPhase::Outer)
	{
		if (!Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.OuterSingleton)
		{
			Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FMCPTrackedActorEntry, (UObject*)Z_Construct_UPackage__Script_PIE_Studio(ETypeConstructPhase::Outer), TEXT("MCPTrackedActorEntry"));
		}
		return Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.OuterSingleton;
	}
	if (!Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.InnerSingleton, UHT_STATICS::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry.InnerSingleton);
}
#undef UHT_STATICS
// ********** End ScriptStruct FMCPTrackedActorEntry ***********************************************

// ********** Begin Class UMCPObservationProfile ***************************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_Construct_UClass_UMCPObservationProfile_Statics
struct UHT_STATICS
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Type_MetaData[] = {
		{ "BlueprintType", "true" },
		{ "IncludePath", "PIE/MCPObservationProfile.h" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TrackedValues_MetaData[] = {
		{ "Category", "Values" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
		{ "TitleProperty", "Path" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Gameplay properties to observe during replay. Each path is sampled every frame and compared against the original recording." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TrackedActors_MetaData[] = {
		{ "Category", "Actors" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
		{ "TitleProperty", "ActorId" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Actors to track by ID. Their position, rotation, and velocity are sampled each frame." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bCapturePawnState_MetaData[] = {
		{ "Category", "Sampling" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Capture pawn transform, velocity, and movement state each frame." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bCaptureMontage_MetaData[] = {
		{ "Category", "Sampling" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Capture active anim montage name and position each frame." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PositionThresholdCm_MetaData[] = {
		{ "Category", "Drift Thresholds" },
		{ "ClampMin", "0" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Pawn position must drift more than this (cm) to count as divergence. Filters out physics jitter." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RotationThresholdDeg_MetaData[] = {
		{ "Category", "Drift Thresholds" },
		{ "ClampMin", "0" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Pawn rotation must drift more than this (degrees) to count as divergence." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_VelocityThresholdCms_MetaData[] = {
		{ "Category", "Drift Thresholds" },
		{ "ClampMin", "0" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Pawn velocity must differ by more than this (cm/s) to count as divergence." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TrackedValueDefaultThreshold_MetaData[] = {
		{ "Category", "Drift Thresholds" },
		{ "ClampMin", "0" },
		{ "ModuleRelativePath", "Private/PIE/MCPObservationProfile.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Default threshold for tracked values. 0 = any change counts as drift. Per-value thresholds override this." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UMCPObservationProfile constinit property declarations *******************
	static const UECodeGen_Private::FStructPropertyParams NewProp_TrackedValues_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_TrackedValues;
	static const UECodeGen_Private::FStructPropertyParams NewProp_TrackedActors_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_TrackedActors;
	static void NewProp_bCapturePawnState_SetBit(void* Obj)
	{
		((UMCPObservationProfile*)Obj)->bCapturePawnState = 1;
	}
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bCapturePawnState;
	static void NewProp_bCaptureMontage_SetBit(void* Obj)
	{
		((UMCPObservationProfile*)Obj)->bCaptureMontage = 1;
	}
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bCaptureMontage;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_PositionThresholdCm;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_RotationThresholdDeg;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_VelocityThresholdCms;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_TrackedValueDefaultThreshold;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UMCPObservationProfile constinit property declarations *********************
	static FTypeConstructFunc* DependentSingletons[];
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMCPObservationProfile>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct UHT_STATICS

// ********** Begin Class UMCPObservationProfile Property Definitions ******************************
const UECodeGen_Private::FStructPropertyParams UHT_STATICS::NewProp_TrackedValues_Inner = { "TrackedValues", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FMCPTrackedValueEntry, METADATA_PARAMS(0, nullptr) }; // f9928cf999f69ae29ebb52dde686e60263445f59
const UECodeGen_Private::FArrayPropertyParams UHT_STATICS::NewProp_TrackedValues = { "TrackedValues", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Array, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, TrackedValues), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TrackedValues_MetaData), NewProp_TrackedValues_MetaData) }; // f9928cf999f69ae29ebb52dde686e60263445f59
const UECodeGen_Private::FStructPropertyParams UHT_STATICS::NewProp_TrackedActors_Inner = { "TrackedActors", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FMCPTrackedActorEntry, METADATA_PARAMS(0, nullptr) }; // 4dd4a57af49d55ea5039b95abbc265c4e3553f41
const UECodeGen_Private::FArrayPropertyParams UHT_STATICS::NewProp_TrackedActors = { "TrackedActors", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Array, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, TrackedActors), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TrackedActors_MetaData), NewProp_TrackedActors_MetaData) }; // 4dd4a57af49d55ea5039b95abbc265c4e3553f41
const UECodeGen_Private::FBoolPropertyParams UHT_STATICS::NewProp_bCapturePawnState = { "bCapturePawnState", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, nullptr, nullptr, 1, sizeof(bool), sizeof(UMCPObservationProfile), &UHT_STATICS::NewProp_bCapturePawnState_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bCapturePawnState_MetaData), NewProp_bCapturePawnState_MetaData) };
const UECodeGen_Private::FBoolPropertyParams UHT_STATICS::NewProp_bCaptureMontage = { "bCaptureMontage", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, nullptr, nullptr, 1, sizeof(bool), sizeof(UMCPObservationProfile), &UHT_STATICS::NewProp_bCaptureMontage_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bCaptureMontage_MetaData), NewProp_bCaptureMontage_MetaData) };
const UECodeGen_Private::FFloatPropertyParams UHT_STATICS::NewProp_PositionThresholdCm = { "PositionThresholdCm", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, PositionThresholdCm), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PositionThresholdCm_MetaData), NewProp_PositionThresholdCm_MetaData) };
const UECodeGen_Private::FFloatPropertyParams UHT_STATICS::NewProp_RotationThresholdDeg = { "RotationThresholdDeg", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, RotationThresholdDeg), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RotationThresholdDeg_MetaData), NewProp_RotationThresholdDeg_MetaData) };
const UECodeGen_Private::FFloatPropertyParams UHT_STATICS::NewProp_VelocityThresholdCms = { "VelocityThresholdCms", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, VelocityThresholdCms), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_VelocityThresholdCms_MetaData), NewProp_VelocityThresholdCms_MetaData) };
const UECodeGen_Private::FFloatPropertyParams UHT_STATICS::NewProp_TrackedValueDefaultThreshold = { "TrackedValueDefaultThreshold", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, nullptr, nullptr, 1, STRUCT_OFFSET(UMCPObservationProfile, TrackedValueDefaultThreshold), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TrackedValueDefaultThreshold_MetaData), NewProp_TrackedValueDefaultThreshold_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const UHT_STATICS::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_TrackedValues_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_TrackedValues,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_TrackedActors_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_TrackedActors,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_bCapturePawnState,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_bCaptureMontage,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_PositionThresholdCm,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_RotationThresholdDeg,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_VelocityThresholdCms,
	(const UECodeGen_Private::FPropertyParamsBase*)&UHT_STATICS::NewProp_TrackedValueDefaultThreshold,
};
static_assert(UE_ARRAY_COUNT(UHT_STATICS::PropPointers) < 2048);
// ********** End Class UMCPObservationProfile Property Definitions ********************************
FTypeConstructFunc* UHT_STATICS::DependentSingletons[] = {
	(FTypeConstructFunc*)Z_Construct_UClass_UDataAsset,
	(FTypeConstructFunc*)Z_Construct_UPackage__Script_PIE_Studio,
};
static_assert(UE_ARRAY_COUNT(UHT_STATICS::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams UHT_STATICS::ClassParams = {
	&Z_Construct_UClass_UMCPObservationProfile,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	UHT_STATICS::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(UHT_STATICS::PropPointers),
	0,
	0x000000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(UHT_STATICS::Type_MetaData), UHT_STATICS::Type_MetaData)
};
FClassRegistrationInfo Z_Registration_Info_UClass_UMCPObservationProfile;
UClass* Z_Construct_UClass_UMCPObservationProfile(ETypeConstructPhase Phase)
{
	if (Phase == ETypeConstructPhase::Inner)
	{
		using TClass = UMCPObservationProfile;
		if (!Z_Registration_Info_UClass_UMCPObservationProfile.InnerSingleton)
		{
			GetPrivateStaticClassBody(
				TClass::StaticPackage(),
				TEXT("MCPObservationProfile"),
				Z_Registration_Info_UClass_UMCPObservationProfile.InnerSingleton,
				nullptr,
				DataSizeOf<TClass>(),
				alignof(TClass),
				TClass::StaticClassFlags,
				TClass::StaticClassCastFlags(),
				TClass::StaticConfigName(),
				(UClass::ClassConstructorType)InternalConstructor<TClass>,
				(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
				UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
				&TClass::Super::StaticClass,
				&TClass::WithinClass::StaticClass
			);
		}
		return Z_Registration_Info_UClass_UMCPObservationProfile.InnerSingleton;
	}
	if (!Z_Registration_Info_UClass_UMCPObservationProfile.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UMCPObservationProfile.OuterSingleton, UHT_STATICS::ClassParams);
	}
	return Z_Registration_Info_UClass_UMCPObservationProfile.OuterSingleton;
}
#undef UHT_STATICS
UMCPObservationProfile::UMCPObservationProfile(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UMCPObservationProfile);
UMCPObservationProfile::~UMCPObservationProfile() {}
// ********** End Class UMCPObservationProfile *****************************************************

// ********** Begin Registration *******************************************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_CompiledInDeferFile_FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h__Script_PIE_Studio_Statics
struct UHT_STATICS
{
	static constexpr FStructRegisterCompiledInInfo ScriptStructInfo[] = {
		{ Z_Construct_UScriptStruct_FMCPTrackedValueEntry, Z_Construct_UScriptStruct_FMCPTrackedValueEntry_Statics::NewStructOps, TEXT("MCPTrackedValueEntry"),&Z_Registration_Info_UScriptStruct_FMCPTrackedValueEntry, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FMCPTrackedValueEntry), 4187131129U) },
		{ Z_Construct_UScriptStruct_FMCPTrackedActorEntry, Z_Construct_UScriptStruct_FMCPTrackedActorEntry_Statics::NewStructOps, TEXT("MCPTrackedActorEntry"),&Z_Registration_Info_UScriptStruct_FMCPTrackedActorEntry, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FMCPTrackedActorEntry), 1305781626U) },
	};
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UMCPObservationProfile, TEXT("UMCPObservationProfile"), &Z_Registration_Info_UClass_UMCPObservationProfile, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UMCPObservationProfile), 2336653534U) },
	};
}; // UHT_STATICS 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_david_Projects_UE_ue_mcp_tests_ue_mcp_Plugins_PIE_Studio_Source_PIE_Studio_Private_PIE_MCPObservationProfile_h__Script_PIE_Studio_8db491d042dccdec7d6cfa8a41b1d7474aeb7c1f{
	TEXT("/Script/PIE_Studio"),
	UHT_STATICS::ClassInfo, UE_ARRAY_COUNT(UHT_STATICS::ClassInfo),
	UHT_STATICS::ScriptStructInfo, UE_ARRAY_COUNT(UHT_STATICS::ScriptStructInfo),
	nullptr, 0,
	nullptr, 0,
};
#undef UHT_STATICS
// ********** End Registration *********************************************************************
#undef UHT_STRUCT_BASE

PRAGMA_ENABLE_DEPRECATION_WARNINGS
