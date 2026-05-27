#include "PIEObserver.h"
#include "MCPObservationProfile.h"
#include "PIESequenceFormat.h"
#include "PIE_TransportModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UEMCPPIE
{
	namespace
	{
		void SampleActors(UWorld* World,
		                  const TArray<FString>& Ids,
		                  TMap<FString, TWeakObjectPtr<AActor>>& Cache,
		                  FTrackedActorRow& OutRow)
		{
			for (const FString& Id : Ids)
			{
				FActorState AS;
				AActor* A = nullptr;
				if (TWeakObjectPtr<AActor>* Cached = Cache.Find(Id))
				{
					A = Cached->Get();
				}
				if (!A)
				{
					A = FindActorById(World, Id);
					if (A) Cache.Add(Id, A);
				}
				if (A)
				{
					AS.Location = A->GetActorLocation();
					AS.Rotation = A->GetActorRotation();
					AS.Velocity = A->GetVelocity();
					AS.bResolved = true;
				}
				OutRow.Actors.Add(Id, AS);
			}
		}
	}

	FPIEObserver& FPIEObserver::Get()
	{
		static FPIEObserver Instance;
		return Instance;
	}

	void FPIEObserver::Init()
	{
		if (BeginPIEHandle.IsValid()) return;
		BeginPIEHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool bSim)
		{
			this->OnBeginPIE(bSim);
		});
		EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool bSim)
		{
			this->OnEndPIE(bSim);
		});
	}

	void FPIEObserver::Shutdown()
	{
		if (BeginPIEHandle.IsValid()) FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		if (EndPIEHandle.IsValid())   FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		BeginPIEHandle.Reset();
		EndPIEHandle.Reset();
		UnbindEndFrame();
		Sessions.Reset();
	}

	void FPIEObserver::BindEndFrame()
	{
		if (!bEndFrameBound)
		{
			OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
			{
				this->OnEndFrame();
			});
			bEndFrameBound = true;
		}
	}

	void FPIEObserver::UnbindEndFrame()
	{
		if (bEndFrameBound && OnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);
			OnEndFrameHandle.Reset();
			bEndFrameBound = false;
		}
	}

	bool FPIEObserver::Arm(const FObserverArmConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		UMCPObservationProfile* Profile = LoadObject<UMCPObservationProfile>(
			nullptr, *Cfg.ProfilePath);
		if (!Profile)
		{
			OutError = FString::Printf(TEXT("Profile not found: %s"), *Cfg.ProfilePath);
			return false;
		}

		FObservationSession S;
		S.Config = Cfg;
		S.ProfilePath = Cfg.ProfilePath;

		for (const FMCPTrackedValueEntry& E : Profile->TrackedValues)
		{
			S.TrackedValuePaths.Add(E.Path);
			if (E.DriftThreshold > 0.f)
			{
				S.TrackedThresholds.Add(E.Path, E.DriftThreshold);
			}
		}

		for (const FMCPTrackedActorEntry& E : Profile->TrackedActors)
		{
			S.TrackedActorIds.Add(E.ActorId);
		}

		S.bCapturePawnState = Profile->bCapturePawnState;
		S.bCaptureMontage = Profile->bCaptureMontage;
		S.ThrPosCm = Profile->PositionThresholdCm;
		S.ThrRotDeg = Profile->RotationThresholdDeg;
		S.ThrVelCms = Profile->VelocityThresholdCms;
		S.ThrTrackedDefault = Profile->TrackedValueDefaultThreshold;

		S.RunId = FString::Printf(TEXT("obs_%s_%s"),
			*FPaths::GetBaseFilename(Cfg.ProfilePath),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		const FString Root = Cfg.OutputDir.IsEmpty()
			? (FPaths::ProjectSavedDir() / TEXT("MCPObservations"))
			: Cfg.OutputDir;
		S.OutputDir = Root / S.RunId;

		S.State = EObserverState::Armed;

		OutMessage = FString::Printf(TEXT("Armed: profile=%s run=%s values=%d actors=%d"),
			*FPaths::GetBaseFilename(Cfg.ProfilePath),
			*S.RunId,
			S.TrackedValuePaths.Num(),
			S.TrackedActorIds.Num());

		Sessions.Add(MoveTemp(S));

		if (GEditor && GEditor->PlayWorld)
		{
			OnBeginPIE(false);
		}
		return true;
	}

	bool FPIEObserver::Disarm(FString& OutError)
	{
		int32 Removed = Sessions.RemoveAll([](const FObservationSession& S)
		{
			return S.State == EObserverState::Armed;
		});
		if (Removed == 0)
		{
			OutError = TEXT("No armed sessions to disarm.");
			return false;
		}
		return true;
	}

	void FPIEObserver::OnBeginPIE(bool /*bIsSimulating*/)
	{
		bool bAnyStarted = false;
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Armed) continue;

			FPIEFrameSampler::FConfig SC;
			SC.AxisThreshold = 0.15f;
			SC.bCapturePawnState = S.bCapturePawnState;
			SC.bCaptureMontage = S.bCaptureMontage;
			SC.TrackedValuePaths = S.TrackedValuePaths;
			SC.ClientIndex = S.Config.ClientId;
			S.Sampler.Reset();
			S.Sampler.SetConfig(SC);

			S.State = EObserverState::WaitingForPawn;
			S.AttachTime = 0.0;
			S.StartedAt = ISOTimestampNow();

			UE_LOG(LogPIETransport, Log, TEXT("[PIE-OBS] Armed -> BeginPIE: profile=%s run=%s"),
				*FPaths::GetBaseFilename(S.ProfilePath), *S.RunId);
			bAnyStarted = true;
		}

		if (bAnyStarted)
		{
			BindEndFrame();
		}
	}

	void FPIEObserver::OnEndFrame()
	{
		UWorld* PIEWorld = nullptr;
		if (GEditor) PIEWorld = GEditor->PlayWorld;
		if (!PIEWorld) return;

		for (FObservationSession& S : Sessions)
		{
			if (S.State == EObserverState::WaitingForPawn)
			{
				if (S.Sampler.AttachToPIE(PIEWorld))
				{
					S.AttachTime = PIEWorld->GetTimeSeconds();

					S.CSVHdr = FCSVHeader();
					S.CSVHdr.RecordingId = S.RunId;
					S.CSVHdr.SampleHz = S.Config.SampleHz > 0 ? S.Config.SampleHz : 60;
					S.CSVHdr.Actions = S.Sampler.GetActions();
					S.CSVHdr.TrackedValues = S.Sampler.GetTrackedValues();
					S.CSVHeaderStr = BuildCSVHeader(S.CSVHdr);
					S.CSVBody.Reset();

					S.State = EObserverState::Observing;
					UE_LOG(LogPIETransport, Log, TEXT("[PIE-OBS] Pawn attached, observing: %s"), *S.RunId);
				}
				continue;
			}

			if (S.State == EObserverState::Observing)
			{
				const double GameTime = PIEWorld->GetTimeSeconds();
				const double Dt = PIEWorld->GetDeltaSeconds();
				const uint64 FrameNum = static_cast<uint64>(S.FramesSampled);
				FCSVRow Row = S.Sampler.SampleFrame(PIEWorld, FrameNum, GameTime, Dt);

				if (S.TrackedActorIds.Num() > 0)
				{
					FTrackedActorRow AR;
					AR.Frame = FrameNum;
					AR.Time = GameTime;
					SampleActors(PIEWorld, S.TrackedActorIds, S.ActorCache, AR);
					S.ActorRows.Add(MoveTemp(AR));
				}

				AppendCSVRow(S.CSVBody, Row, S.CSVHdr);
				S.FramesSampled++;
			}
		}
	}

	void FPIEObserver::OnEndPIE(bool /*bIsSimulating*/)
	{
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				FinaliseSession(S);
			}
		}
		Sessions.Reset();
		UnbindEndFrame();
	}

	FObserverFinishResult FPIEObserver::FinaliseSession(FObservationSession& S)
	{
		FObserverFinishResult R;
		R.RunId = S.RunId;
		R.OutputDir = S.OutputDir;
		R.FramesSampled = S.FramesSampled;

		if (S.FramesSampled == 0)
		{
			R.bSuccess = true;
			S.State = EObserverState::Completed;
			return R;
		}

		IFileManager::Get().MakeDirectory(*S.OutputDir, true);

		{
			const FString FullCSV = S.CSVHeaderStr + S.CSVBody;
			FString Err;
			if (SaveCSV(S.OutputDir / TEXT("observation.csv"), FullCSV, Err))
			{
				R.CSVPath = S.OutputDir / TEXT("observation.csv");
			}
			else
			{
				UE_LOG(LogPIETransport, Warning, TEXT("[PIE-OBS] CSV write failed: %s"), *Err);
			}
		}

		if (S.ActorRows.Num() > 0)
		{
			FString Err;
			if (SaveTrackedActorsJSONL(S.OutputDir / TEXT("tracked.jsonl"), S.ActorRows, Err))
			{
				R.TrackedActorsPath = S.OutputDir / TEXT("tracked.jsonl");
			}
			else
			{
				UE_LOG(LogPIETransport, Warning, TEXT("[PIE-OBS] tracked.jsonl write failed: %s"), *Err);
			}
		}

		{
			TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetNumberField(TEXT("version"), kFormatVersion);
			M->SetStringField(TEXT("type"), TEXT("observation"));
			M->SetStringField(TEXT("run_id"), S.RunId);
			M->SetStringField(TEXT("profile"), S.ProfilePath);
			M->SetStringField(TEXT("started_at"), S.StartedAt);
			M->SetStringField(TEXT("ended_at"), ISOTimestampNow());
			M->SetNumberField(TEXT("frames_sampled"), S.FramesSampled);
			M->SetNumberField(TEXT("sample_hz"), S.CSVHdr.SampleHz);

			TArray<TSharedPtr<FJsonValue>> Vals;
			for (const FString& P : S.TrackedValuePaths)
			{
				Vals.Add(MakeShared<FJsonValueString>(P));
			}
			M->SetArrayField(TEXT("tracked_values"), Vals);

			TArray<TSharedPtr<FJsonValue>> Acts;
			for (const FString& A : S.TrackedActorIds)
			{
				Acts.Add(MakeShared<FJsonValueString>(A));
			}
			M->SetArrayField(TEXT("tracked_actors"), Acts);

			TSharedRef<FJsonObject> Thr = MakeShared<FJsonObject>();
			Thr->SetNumberField(TEXT("position_cm"), S.ThrPosCm);
			Thr->SetNumberField(TEXT("rotation_deg"), S.ThrRotDeg);
			Thr->SetNumberField(TEXT("velocity_cms"), S.ThrVelCms);
			Thr->SetNumberField(TEXT("tracked_default"), S.ThrTrackedDefault);
			if (S.TrackedThresholds.Num() > 0)
			{
				TSharedRef<FJsonObject> PerPath = MakeShared<FJsonObject>();
				for (const TPair<FString, float>& KV : S.TrackedThresholds)
				{
					PerPath->SetNumberField(KV.Key, KV.Value);
				}
				Thr->SetObjectField(TEXT("tracked"), PerPath);
			}
			M->SetObjectField(TEXT("thresholds"), Thr);

			FString JsonStr;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(M, W);
			FFileHelper::SaveStringToFile(JsonStr, *(S.OutputDir / TEXT("manifest.json")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		R.bSuccess = true;
		S.State = EObserverState::Completed;

		UE_LOG(LogPIETransport, Log, TEXT("[PIE-OBS] Finalized: %s %d frames -> %s"),
			*FPaths::GetBaseFilename(S.ProfilePath), S.FramesSampled, *S.OutputDir);
		return R;
	}

	FObserverFinishResult FPIEObserver::ForceStop()
	{
		FObserverFinishResult LastResult;
		for (FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				LastResult = FinaliseSession(S);
			}
		}
		Sessions.Reset();
		UnbindEndFrame();
		return LastResult;
	}

	FObserverStatus FPIEObserver::GetStatus() const
	{
		FObserverStatus Out;
		Out.State = EObserverState::Idle;

		for (const FObservationSession& S : Sessions)
		{
			if (S.State == EObserverState::Observing)
			{
				Out.State = EObserverState::Observing;
				Out.RunId = S.RunId;
				Out.ProfilePath = S.ProfilePath;
				Out.FramesSampled += S.FramesSampled;
				if (GEditor && GEditor->PlayWorld && S.AttachTime > 0.0)
				{
					Out.ElapsedSeconds = FMath::Max(Out.ElapsedSeconds,
						GEditor->PlayWorld->GetTimeSeconds() - S.AttachTime);
				}
			}
			else if (S.State == EObserverState::WaitingForPawn && Out.State == EObserverState::Idle)
			{
				Out.State = EObserverState::WaitingForPawn;
			}
			else if (S.State == EObserverState::Armed && Out.State == EObserverState::Idle)
			{
				Out.State = EObserverState::Armed;
			}
		}

		if (Out.State == EObserverState::Observing && Sessions.Num() > 1)
		{
			Out.RunId = FString::Printf(TEXT("%d sessions"), Sessions.Num());
		}

		return Out;
	}

	bool FPIEObserver::IsActive() const
	{
		for (const FObservationSession& S : Sessions)
		{
			if (S.State != EObserverState::Idle && S.State != EObserverState::Completed)
			{
				return true;
			}
		}
		return false;
	}
}
