#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCPObservationProfile.generated.h"

USTRUCT(BlueprintType)
struct FMCPTrackedValueEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking",
		meta=(ToolTip="Property path to observe (e.g. CharacterMovement.Velocity.X)"))
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking",
		meta=(ClampMin="0", ToolTip="Override drift threshold for this value. 0 = use profile default."))
	float DriftThreshold = 0.f;
};

USTRUCT(BlueprintType)
struct FMCPTrackedActorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tracking",
		meta=(ToolTip="Actor label or unique ID to track in the PIE world."))
	FString ActorId;
};

UCLASS(BlueprintType)
class UMCPObservationProfile : public UDataAsset
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Values",
		meta=(TitleProperty="Path", ToolTip="Gameplay properties to observe during replay. Each path is sampled every frame and compared against the original recording."))
	TArray<FMCPTrackedValueEntry> TrackedValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Actors",
		meta=(TitleProperty="ActorId", ToolTip="Actors to track by ID. Their position, rotation, and velocity are sampled each frame."))
	TArray<FMCPTrackedActorEntry> TrackedActors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling",
		meta=(ToolTip="Capture pawn transform, velocity, and movement state each frame."))
	bool bCapturePawnState = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sampling",
		meta=(ToolTip="Capture active anim montage name and position each frame."))
	bool bCaptureMontage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drift Thresholds",
		meta=(ToolTip="Pawn position must drift more than this (cm) to count as divergence. Filters out physics jitter.", ClampMin="0"))
	float PositionThresholdCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drift Thresholds",
		meta=(ToolTip="Pawn rotation must drift more than this (degrees) to count as divergence.", ClampMin="0"))
	float RotationThresholdDeg = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drift Thresholds",
		meta=(ToolTip="Pawn velocity must differ by more than this (cm/s) to count as divergence.", ClampMin="0"))
	float VelocityThresholdCms = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Drift Thresholds",
		meta=(ToolTip="Default threshold for tracked values. 0 = any change counts as drift. Per-value thresholds override this.", ClampMin="0"))
	float TrackedValueDefaultThreshold = 0.f;
};
