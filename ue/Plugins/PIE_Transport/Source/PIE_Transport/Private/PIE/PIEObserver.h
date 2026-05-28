#pragma once

#include "CoreMinimal.h"
#include "PIEFrameSampler.h"
#include "PIESequenceFormat.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UMCPObservationProfile;
class UWorld;
class AActor;

namespace UEMCPPIE
{
	enum class EObserverState : uint8
	{
		Idle,
		Armed,
		WaitingForPawn,
		Observing,
		Completed
	};

	struct FObserverArmConfig
	{
		FString ProfilePath;
		FString OutputDir;
		int32 SampleHz = 60;
		int32 PinFPS = -1;
		int32 ClientId = 0;
	};

	struct FObserverStatus
	{
		EObserverState State = EObserverState::Idle;
		FString RunId;
		FString ProfilePath;
		int32 FramesSampled = 0;
		double ElapsedSeconds = 0.0;
	};

	struct FObserverFinishResult
	{
		bool bSuccess = false;
		FString Error;
		FString RunId;
		FString OutputDir;
		FString CSVPath;
		FString TrackedActorsPath;
		int32 FramesSampled = 0;
		double DurationSeconds = 0.0;
	};

	struct FObservationSession
	{
		FObserverArmConfig Config;
		EObserverState State = EObserverState::Armed;
		FString RunId;
		FString OutputDir;
		FString ProfilePath;

		FPIEFrameSampler Sampler;
		FCSVHeader CSVHdr;
		FString CSVHeaderStr;
		FString CSVBody;

		TArray<FString> TrackedValuePaths;
		TArray<FString> TrackedActorIds;
		TArray<FTrackedActorRow> ActorRows;
		TMap<FString, TWeakObjectPtr<AActor>> ActorCache;

		double AttachTime = 0.0;
		int32 FramesSampled = 0;
		FString StartedAt;

		bool bCapturePawnState = true;
		bool bCaptureMontage = true;
		float ThrPosCm = 5.f;
		float ThrRotDeg = 2.f;
		float ThrVelCms = 25.f;
		float ThrTrackedDefault = 0.f;
		TMap<FString, float> TrackedThresholds;

		FCSVRow LastRow;
		FCSVRow PrevRow;
		FTrackedActorRow LastActorRow;
	};

	struct FLiveObservationSnapshot
	{
		FString ProfileName;
		FString RunId;
		EObserverState State = EObserverState::Idle;
		int32 FramesSampled = 0;
		double ElapsedSeconds = 0.0;

		FCSVRow LastRow;
		FCSVRow PrevRow;

		TArray<FString> TrackedActorIds;
		FTrackedActorRow LastActorRow;
	};

	class FPIEObserver
	{
	public:
		static FPIEObserver& Get();

		void Init();
		void Shutdown();

		bool Arm(const FObserverArmConfig& Cfg, FString& OutError, FString& OutMessage);
		bool Disarm(FString& OutError);
		FObserverFinishResult ForceStop();
		FObserverStatus GetStatus() const;
		bool IsActive() const;
		TArray<FLiveObservationSnapshot> GetLiveSnapshots() const;

		int32 NumSessions() const { return Sessions.Num(); }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnEndFrame();
		FObserverFinishResult FinaliseSession(FObservationSession& S);
		void BindEndFrame();
		void UnbindEndFrame();

		TArray<FObservationSession> Sessions;

		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle OnEndFrameHandle;
		bool bEndFrameBound = false;
	};
}
