#include "PIEUatRunner.h"

#include "PIE_StudioModule.h"
#include "PIEInputInjector.h"
#include "PIERemotePlayers.h"
#include "PIEStudioRemoteControl.h"
#include "PIEViewportCapture.h"
#include "SceneViewExtension.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Algo/AllOf.h"

#if UE_ENABLE_ICU
THIRD_PARTY_INCLUDES_START
#include <unicode/regex.h>
THIRD_PARTY_INCLUDES_END
#endif

namespace UEMCPPIE
{
	namespace
	{
		// ── Evidence ────────────────────────────────────────────────────────

		/** Every line the editor process logs while a run is active. */
		class FHostLogCapture : public FOutputDevice
		{
		public:
			virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				// Same shape as a log file line, so one pattern reads host and client logs alike.
				const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
				FString Line;
				if (Category != NAME_None) Line = Category.ToString() + TEXT(": ");
				if (Level != ELogVerbosity::Log) Line += FString(ToString(Level)) + TEXT(": ");
				Line += V;
				FScopeLock Lock(&Mutex);
				Lines.Add(MoveTemp(Line));
			}
			virtual bool CanBeUsedOnAnyThread() const override { return true; }
			virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

			int32 Num() const { FScopeLock Lock(&Mutex); return Lines.Num(); }
			void CopyFrom(int32 Start, TArray<FString>& Out) const
			{
				FScopeLock Lock(&Mutex);
				for (int32 i = FMath::Max(Start, 0); i < Lines.Num(); ++i) { Out.Add(Lines[i]); }
			}
			void Reset() { FScopeLock Lock(&Mutex); Lines.Reset(); }

		private:
			mutable FCriticalSection Mutex;
			TArray<FString> Lines;
		};

		/** A separate-process client's log file, read as it grows. */
		struct FClientLogTail
		{
			FString Path;
			int64 Offset = 0;
			TArray<uint8> Partial;
			TArray<FString> Lines;

			void Begin()
			{
				// A client process writes a fresh log file, so the whole file belongs to this run.
				Offset = 0;
				Partial.Reset();
			}

			void Poll()
			{
				IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
				const int64 Size = PF.FileSize(*Path);
				if (Size < 0)
				{
					return;
				}
				if (Size < Offset)
				{
					Offset = 0; // the client restarted its log
				}
				if (Size == Offset)
				{
					return;
				}
				TUniquePtr<IFileHandle> Handle(PF.OpenRead(*Path, /*bAllowWrite*/ true));
				if (!Handle || !Handle->Seek(Offset))
				{
					return;
				}
				const int64 Count = Size - Offset;
				TArray<uint8> Bytes;
				Bytes.SetNumUninitialized(Count);
				if (!Handle->Read(Bytes.GetData(), Count))
				{
					return;
				}
				Offset = Size;
				// Decode whole lines only: a read can end inside a multi-byte character.
				Partial.Append(Bytes);
				int32 Start = 0;
				for (int32 i = 0; i < Partial.Num(); ++i)
				{
					if (Partial[i] != '\n') continue;
					FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Partial.GetData() + Start), i - Start);
					FString Line(Converted.Length(), Converted.Get());
					Line.TrimEndInline();
					Lines.Add(MoveTemp(Line));
					Start = i + 1;
				}
				Partial.RemoveAt(0, Start, EAllowShrinking::No);
			}
		};

		// ── Scenario model ──────────────────────────────────────────────────

		struct FUatAction
		{
			enum class EKind { Tape, Hold, Inject, Console, Wait, Place, Capture } Kind = EKind::Wait;
			int32 Client = 0;
			FString ActionPath;
			TArray<FVector> Values;
			FVector Value = FVector(1, 0, 0);
			float Ms = 0.0f;
			FString Command;
			// Place: next to actor `At` (offset in its local frame, facing it), or at `Value` with `Yaw`.
			FString At;
			FVector Offset = FVector(300, 0, 0);
			bool bFace = true;
			TOptional<double> Yaw;
		};

		struct FUatRule
		{
			FString Log;      // "host" or "client:N"
			int32 Client = 0; // 0 = the host log
			FString Pattern;
			int32 Min = 1;
			int32 Max = -1;
			bool bSinceRun = false; // judge every line since the run began, not only this step's
		};

		struct FUatStep
		{
			FString Name;
			TArray<FUatAction> Do;
			TArray<FUatRule> Expect;
			TArray<FUatRule> Forbid;
			float WindowMs = 4000.0f;
		};

		struct FUatStepResult
		{
			FString Name;
			FString Verdict; // PASS | FAIL | SKIPPED
			TArray<FString> Details;
			TArray<FString> Captures; // images written during the step, relative to the report directory
		};

		FVector ParseVector(const TSharedPtr<FJsonValue>& V)
		{
			FVector Out(0, 0, 0);
			if (!V.IsValid())
			{
				return FVector(1, 0, 0);
			}
			if (V->Type == EJson::Number)
			{
				Out.X = V->AsNumber();
			}
			else if (V->Type == EJson::Boolean)
			{
				Out.X = V->AsBool() ? 1.0 : 0.0;
			}
			else if (V->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
				if (A.Num() > 0) Out.X = A[0]->AsNumber();
				if (A.Num() > 1) Out.Y = A[1]->AsNumber();
				if (A.Num() > 2) Out.Z = A[2]->AsNumber();
			}
			return Out;
		}

		/** FRegexPattern swallows a bad pattern and then matches nothing, so compile it with ICU first. */
		bool ValidateRegex(const FString& Pattern, FString& Err)
		{
#if UE_ENABLE_ICU
			UErrorCode Status = U_ZERO_ERROR;
			UParseError Parse;
			const FTCHARToUTF8 Utf8(*Pattern);
			const icu::UnicodeString Source = icu::UnicodeString::fromUTF8(icu::StringPiece(Utf8.Get(), Utf8.Length()));
			TUniquePtr<icu::RegexPattern> Compiled(icu::RegexPattern::compile(Source, 0, Parse, Status));
			if (U_FAILURE(Status))
			{
				Err = FString::Printf(TEXT("pattern /%s/ is not a valid regex: %s at offset %d"), *Pattern, UTF8_TO_TCHAR(u_errorName(Status)), Parse.offset);
				return false;
			}
#endif
			return true;
		}

		bool ParseRule(const TSharedPtr<FJsonObject>& O, FUatRule& Out, FString& Err)
		{
			if (!O->TryGetStringField(TEXT("pattern"), Out.Pattern) || Out.Pattern.IsEmpty())
			{
				Err = TEXT("an expect/forbid entry has no pattern");
				return false;
			}
			Out.Log = O->HasField(TEXT("log")) ? O->GetStringField(TEXT("log")) : TEXT("host");
			const FString Index = Out.Log.StartsWith(TEXT("client:")) ? Out.Log.RightChop(7) : FString();
			const bool bDigits = !Index.IsEmpty() && Index.Len() < 4 && Algo::AllOf(Index, [](TCHAR C) { return FChar::IsDigit(C); });
			if (Out.Log != TEXT("host") && !bDigits)
			{
				Err = FString::Printf(TEXT("log must be \"host\" or \"client:N\", not \"%s\""), *Out.Log);
				return false;
			}
			Out.Client = bDigits ? FCString::Atoi(*Index) : 0;
			if (!ValidateRegex(Out.Pattern, Err))
			{
				return false;
			}
			double N = 0;
			if (O->TryGetNumberField(TEXT("min"), N)) Out.Min = static_cast<int32>(N);
			if (O->TryGetNumberField(TEXT("max"), N)) Out.Max = static_cast<int32>(N);
			FString Since;
			if (O->TryGetStringField(TEXT("since"), Since))
			{
				if (!Since.Equals(TEXT("run"), ESearchCase::CaseSensitive) && !Since.Equals(TEXT("step"), ESearchCase::CaseSensitive))
				{
					Err = FString::Printf(TEXT("since must be \"step\" or \"run\", not \"%s\""), *Since);
					return false;
				}
				Out.bSinceRun = Since == TEXT("run");
			}
			return true;
		}

		bool ParseAction(const TSharedPtr<FJsonObject>& O, FUatAction& Out, FString& Err)
		{
			double N = 0;
			if (O->TryGetNumberField(TEXT("client"), N)) Out.Client = static_cast<int32>(N);

			if (O->TryGetNumberField(TEXT("wait_ms"), N))
			{
				Out.Kind = FUatAction::EKind::Wait;
				Out.Ms = static_cast<float>(N);
				return true;
			}
			FString Cmd;
			if (O->TryGetStringField(TEXT("console"), Cmd))
			{
				Out.Kind = FUatAction::EKind::Console;
				Out.Command = Cmd;
				return true;
			}

			const TSharedPtr<FJsonObject>* Body = nullptr;
			auto ActionOf = [&Out, &Err](const TSharedPtr<FJsonObject>& B) -> bool
			{
				if (!B->TryGetStringField(TEXT("action"), Out.ActionPath) || Out.ActionPath.IsEmpty())
				{
					Err = TEXT("an input action entry has no action path");
					return false;
				}
				return true;
			};

			if (O->TryGetObjectField(TEXT("tape"), Body))
			{
				Out.Kind = FUatAction::EKind::Tape;
				if (!ActionOf(*Body)) return false;
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!(*Body)->TryGetArrayField(TEXT("values"), Values) || Values->Num() == 0)
				{
					Err = TEXT("a tape has no values");
					return false;
				}
				for (const TSharedPtr<FJsonValue>& V : *Values) { Out.Values.Add(ParseVector(V)); }
				return true;
			}
			if (O->TryGetObjectField(TEXT("press"), Body))
			{
				Out.Kind = FUatAction::EKind::Tape;
				if (!ActionOf(*Body)) return false;
				double Frames = 4;
				(*Body)->TryGetNumberField(TEXT("frames"), Frames);
				for (int32 i = 0; i < FMath::Max(1, static_cast<int32>(Frames)); ++i) { Out.Values.Add(FVector(1, 0, 0)); }
				Out.Values.Add(FVector::ZeroVector);
				return true;
			}
			if (O->TryGetObjectField(TEXT("hold"), Body))
			{
				Out.Kind = FUatAction::EKind::Hold;
				if (!ActionOf(*Body)) return false;
				Out.Value = ParseVector((*Body)->TryGetField(TEXT("value")));
				double Ms = 0;
				if (!(*Body)->TryGetNumberField(TEXT("ms"), Ms) || Ms <= 0)
				{
					Err = TEXT("a hold needs ms > 0");
					return false;
				}
				Out.Ms = static_cast<float>(Ms);
				return true;
			}
			if (O->TryGetObjectField(TEXT("inject"), Body))
			{
				Out.Kind = FUatAction::EKind::Inject;
				if (!ActionOf(*Body)) return false;
				Out.Value = ParseVector((*Body)->TryGetField(TEXT("value")));
				return true;
			}
			if (O->TryGetObjectField(TEXT("place"), Body))
			{
				Out.Kind = FUatAction::EKind::Place;
				const TSharedPtr<FJsonObject>& P = *Body;
				double Yaw = 0;
				if (P->TryGetNumberField(TEXT("yaw"), Yaw)) Out.Yaw = Yaw;
				P->TryGetBoolField(TEXT("face"), Out.bFace);
				if (P->TryGetStringField(TEXT("at"), Out.At) && !Out.At.IsEmpty())
				{
					if (P->HasField(TEXT("offset"))) Out.Offset = ParseVector(P->TryGetField(TEXT("offset")));
					return true;
				}
				if (P->HasField(TEXT("location")))
				{
					Out.Value = ParseVector(P->TryGetField(TEXT("location")));
					return true;
				}
				Err = TEXT("place needs at (an actor name or label) or location");
				return false;
			}
			if (const TSharedPtr<FJsonValue> Capture = O->TryGetField(TEXT("capture")))
			{
				Out.Kind = FUatAction::EKind::Capture;
				if (Capture->Type == EJson::String)
				{
					Out.Command = FPaths::MakeValidFileName(Capture->AsString());
				}
				else if (Capture->Type != EJson::Boolean || !Capture->AsBool())
				{
					Err = TEXT("capture takes a label (string) or true");
					return false;
				}
				return true;
			}
			Err = TEXT("an action must be one of tape, press, hold, inject, console, place, capture, wait_ms");
			return false;
		}
	}

	// ── Runner ──────────────────────────────────────────────────────────────

	struct FPIEUatRunner::FImpl
	{
		enum class EState { Idle, Starting, Joining, Settling, Acting, Watching, Stopping, Finished };

		EState State = EState::Idle;
		FString Name;
		int32 Players = 2;
		bool bListen = true;
		bool bOneProcess = false;
		float JoinTimeoutS = 180.0f;
		float SettleMs = 2000.0f;
		TArray<FUatStep> Steps;
		TArray<FUatStepResult> Results;
		FString Error;
		FString ReportDir;
		bool bAborted = false;

		// Progress.
		int32 StepIndex = 0;
		int32 ActionIndex = 0;
		double PhaseStart = 0.0;
		double WaitUntil = 0.0;
		int32 HostMark = 0;
		TMap<int32, int32> ClientMarks;
		struct FUatPendingStop { double At; int32 Client; FString Id; };
		TArray<FUatPendingStop> PendingStops;
		// Requests relayed to remote players in the current step; each must be confirmed applied.
		struct FUatRelayed { FString Id; int32 Client; FString What; };
		TArray<FUatRelayed> Relayed;
		// Captures taken in the current step. Host captures report through Written (0 pending, 1 written, 2 failed).
		struct FUatCapture { int32 Client; FString File; TSharedPtr<int32> Written; };
		TArray<FUatCapture> StepCaptures;
		TSharedPtr<FPIEViewportCapture> HostCapture;

		// Separate-process clients render offscreen (no window) at a fixed frame rate.
		bool bWindowless = false;
		int32 ClientFps = 60;

		// Evidence.
		FHostLogCapture HostLog;
		TMap<int32, FClientLogTail> ClientLogs;

		// Restored after the run.
		EPlayNetMode SavedNetMode = PIE_Standalone;
		int32 SavedClients = 1;
		bool SavedOneProcess = true;
		FString SavedLaunchParameters;

		FTSTicker::FDelegateHandle TickHandle;

		static double Now() { return FPlatformTime::Seconds(); }

		void Fail(const FString& Why)
		{
			Error = Why;
			UE_LOG(LogPIEStudio, Warning, TEXT("[UAT] %s: %s"), *Name, *Why);
			BeginStop();
		}

		bool ConfigureAndStartPIE()
		{
			ULevelEditorPlaySettings* PS = GetMutableDefault<ULevelEditorPlaySettings>();
			PS->GetPlayNetMode(SavedNetMode);
			PS->GetPlayNumberOfClients(SavedClients);
			PS->GetRunUnderOneProcess(SavedOneProcess);
			SavedLaunchParameters = PS->AdditionalLaunchParameters;

			PS->SetPlayNetMode(bListen ? PIE_ListenServer : PIE_Client);
			PS->SetPlayNumberOfClients(Players);
			PS->SetRunUnderOneProcess(bOneProcess);
			if (bWindowless && !bOneProcess)
			{
				// No window, so nothing takes focus; the frame rate is pinned because no vsync paces an offscreen client.
				// The engine reads only the first -ExecCmds, so join an existing one rather than adding a second.
				const FString Cmds = FString::Printf(TEXT("t.MaxFPS %d,t.IdleWhenNotForeground 0"), ClientFps);
				FString LaunchParams = SavedLaunchParameters;
				const int32 At = LaunchParams.Find(TEXT("-ExecCmds="));
				if (At == INDEX_NONE)
				{
					LaunchParams += FString::Printf(TEXT(" -ExecCmds=\"%s\""), *Cmds);
				}
				else
				{
					const int32 ValueAt = At + FCString::Strlen(TEXT("-ExecCmds="));
					if (LaunchParams.IsValidIndex(ValueAt) && LaunchParams[ValueAt] == TEXT('"'))
					{
						LaunchParams.InsertAt(ValueAt + 1, Cmds + TEXT(","));
					}
					else
					{
						// Unquoted: the value ends at the next space; quote it, since ours contains spaces.
						int32 End = ValueAt;
						while (LaunchParams.IsValidIndex(End) && !FChar::IsWhitespace(LaunchParams[End])) ++End;
						const FString Existing = LaunchParams.Mid(ValueAt, End - ValueAt);
						LaunchParams = LaunchParams.Left(ValueAt) + TEXT("\"") + Cmds + (Existing.IsEmpty() ? TEXT("") : TEXT(",")) + Existing + TEXT("\"") + LaunchParams.Mid(End);
					}
				}
				PS->AdditionalLaunchParameters = (LaunchParams + TEXT(" -RenderOffscreen")).TrimStart();
			}

			FRequestPlaySessionParams Params;
			GEditor->RequestPlaySession(Params);
			return true;
		}

		void RestoreSettings()
		{
			ULevelEditorPlaySettings* PS = GetMutableDefault<ULevelEditorPlaySettings>();
			PS->SetPlayNetMode(SavedNetMode);
			PS->SetPlayNumberOfClients(SavedClients);
			PS->SetRunUnderOneProcess(SavedOneProcess);
			PS->AdditionalLaunchParameters = SavedLaunchParameters;
		}

		int32 RemoteCount() const { return bListen ? Players - 1 : Players; }

		bool AllJoined() const
		{
			const TArray<PIERemotePlayers::FRemotePlayer> Remote = PIERemotePlayers::List();
			int32 Ready = 0;
			for (const PIERemotePlayers::FRemotePlayer& P : Remote)
			{
				Ready += P.bReady ? 1 : 0;
			}
			if (Ready < RemoteCount())
			{
				return false;
			}
			if (bListen)
			{
				UWorld* Server = PIERemotePlayers::FindServerWorld();
				APlayerController* Host = Server ? Server->GetFirstPlayerController() : nullptr;
				return Host && Host->IsLocalController() && Host->GetPawn();
			}
			return true;
		}

		// Each client names its own log file when it joins; join order and launch order differ.
		bool BeginClientLogs(FString& OutError)
		{
			ClientLogs.Reset();
			if (bOneProcess)
			{
				return true;
			}
			for (const PIERemotePlayers::FRemotePlayer& P : PIERemotePlayers::List())
			{
				if (P.LogFile.IsEmpty())
				{
					OutError = FString::Printf(TEXT("client %d (%s) reported no log file"), P.Client, *P.PlayerName);
					return false;
				}
				FClientLogTail& Tail = ClientLogs.Add(P.Client);
				Tail.Path = P.LogFile;
				Tail.Begin();
			}
			return true;
		}

		void PollClientLogs()
		{
			for (TPair<int32, FClientLogTail>& Pair : ClientLogs)
			{
				Pair.Value.Poll();
			}
		}

		// Lines in a log since the current step began.
		void LinesSinceMark(const FUatRule& Rule, TArray<FString>& Out) const
		{
			if (Rule.Client == 0 || bOneProcess)
			{
				HostLog.CopyFrom(Rule.bSinceRun ? 0 : HostMark, Out);
				return;
			}
			const FClientLogTail* Tail = ClientLogs.Find(Rule.Client);
			const int32* Mark = ClientMarks.Find(Rule.Client);
			if (Tail && Mark)
			{
				for (int32 i = Rule.bSinceRun ? 0 : *Mark; i < Tail->Lines.Num(); ++i) { Out.Add(Tail->Lines[i]); }
			}
		}

		static int32 CountMatches(const TArray<FString>& Lines, const FString& Pattern, TArray<FString>* Samples)
		{
			const FRegexPattern Regex(Pattern);
			int32 Count = 0;
			for (const FString& Line : Lines)
			{
				FRegexMatcher Matcher(Regex, Line);
				if (Matcher.FindNext())
				{
					++Count;
					if (Samples && Samples->Num() < 3) { Samples->Add(Line); }
				}
			}
			return Count;
		}

		bool ExpectationsMet(const FUatStep& Step) const
		{
			for (const FUatRule& Rule : Step.Expect)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				const int32 Count = CountMatches(Lines, Rule.Pattern, nullptr);
				if (Count < Rule.Min || (Rule.Max >= 0 && Count > Rule.Max))
				{
					return false;
				}
			}
			return true;
		}

		FUatStepResult Evaluate(const FUatStep& Step) const
		{
			FUatStepResult R;
			R.Name = Step.Name;
			bool bPass = true;
			for (const FUatRule& Rule : Step.Expect)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				TArray<FString> Samples;
				const int32 Count = CountMatches(Lines, Rule.Pattern, &Samples);
				const bool bOk = Count >= Rule.Min && (Rule.Max < 0 || Count <= Rule.Max);
				bPass &= bOk;
				R.Details.Add(FString::Printf(TEXT("%s expect %s /%s/ %s: %d match(es)%s"),
					bOk ? TEXT("ok  ") : TEXT("MISS"), *Rule.Log, *Rule.Pattern,
					Rule.Max >= 0 ? *FString::Printf(TEXT("[%d..%d]"), Rule.Min, Rule.Max) : *FString::Printf(TEXT("[>=%d]"), Rule.Min),
					Count, Samples.Num() ? *FString::Printf(TEXT(" e.g. %s"), *Samples[0].Left(200)) : TEXT("")));
			}
			for (const FUatRule& Rule : Step.Forbid)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				TArray<FString> Samples;
				const int32 Count = CountMatches(Lines, Rule.Pattern, &Samples);
				bPass &= Count == 0;
				R.Details.Add(FString::Printf(TEXT("%s forbid %s /%s/: %d match(es)%s"),
					Count == 0 ? TEXT("ok  ") : TEXT("HIT "), *Rule.Log, *Rule.Pattern, Count,
					Samples.Num() ? *FString::Printf(TEXT(" e.g. %s"), *Samples[0].Left(200)) : TEXT("")));
			}
			for (const FUatRelayed& Req : Relayed)
			{
				const UPIEStudioRemoteControl* Remote = PIERemotePlayers::FindById(Req.Id);
				const FPIEStudioRemoteResult* Result = Remote ? Remote->FindResult(Req.Id) : nullptr;
				if (!Result)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("MISS client %d never confirmed %s"), Req.Client, *Req.What));
				}
				else if (!Result->bOk)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("FAIL client %d could not run %s: %s"), Req.Client, *Req.What, *Result->Error));
				}
			}
			for (const FUatCapture& C : StepCaptures)
			{
				if (C.Written.IsValid() && *C.Written != 1)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("%s host capture %s"), *C.Written == 0 ? TEXT("MISS") : TEXT("FAIL"),
						*C.Written == 0 ? *FString::Printf(TEXT("%s was not written in the window"), *C.File) : *FString::Printf(TEXT("%s could not be written"), *C.File)));
					continue;
				}
				if (!C.Written.IsValid() && !IFileManager::Get().FileExists(*(ReportDir / C.File)))
				{
					continue; // a remote capture that did not land is already reported through its relayed result
				}
				R.Captures.Add(C.File);
			}
			R.Verdict = bPass ? TEXT("PASS") : TEXT("FAIL");
			return R;
		}

		bool HostCapturesWritten() const
		{
			for (const FUatCapture& C : StepCaptures)
			{
				if (C.Written.IsValid() && *C.Written == 0) return false;
			}
			return true;
		}

		// Every relayed request has been answered.
		bool RelayedSettled() const
		{
			for (const FUatRelayed& Req : Relayed)
			{
				const UPIEStudioRemoteControl* Remote = PIERemotePlayers::FindById(Req.Id);
				if (!Remote || !Remote->FindResult(Req.Id)) return false;
			}
			return true;
		}

		// A step may end before its window only when nothing later in the window could change the verdict.
		bool CanFinishEarly(const FUatStep& Step) const
		{
			if (Step.Forbid.Num() > 0 || (Step.Expect.Num() == 0 && StepCaptures.Num() == 0))
			{
				return false;
			}
			for (const FUatRule& Rule : Step.Expect)
			{
				if (Rule.Max >= 0) return false;
			}
			return ExpectationsMet(Step) && RelayedSettled() && HostCapturesWritten();
		}

		static FInputActionValue ToValue(const UInputAction* Input, const FVector& Value)
		{
			switch (Input->ValueType)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(Value.X != 0.0);
			case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(Value.X));
			case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(Value.X, Value.Y));
			default:                             return FInputActionValue(Value);
			}
		}

		/** Teleports a player's pawn on the server; the owning client follows through movement correction. */
		bool Place(const FUatAction& A, FString& Err)
		{
			UWorld* World = PIERemotePlayers::FindServerWorld();
			if (!World) World = GEditor->PlayWorld;
			if (!World)
			{
				Err = TEXT("place: no PIE world");
				return false;
			}
			APlayerController* PC = nullptr;
			UPIEStudioRemoteControl* Remote = nullptr;
			if (A.Client > 0)
			{
				Remote = PIERemotePlayers::Find(A.Client, Err);
				PC = Remote ? Cast<APlayerController>(Remote->GetOwner()) : nullptr;
			}
			else
			{
				PC = World->GetFirstPlayerController();
				PC = PC && PC->IsLocalController() ? PC : nullptr;
			}
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn)
			{
				Err = FString::Printf(TEXT("place: client %d has no pawn%s"), A.Client, Err.IsEmpty() ? TEXT("") : *(TEXT(" (") + Err + TEXT(")")));
				return false;
			}

			const FVector Requested = A.Value;
			AActor* Target = nullptr;
			if (!A.At.IsEmpty())
			{
				for (TActorIterator<AActor> It(World); It && !Target; ++It)
				{
					if (It->GetName() == A.At || It->GetActorLabel() == A.At)
					{
						Target = *It;
					}
				}
				if (!Target)
				{
					Err = FString::Printf(TEXT("place: no actor named or labelled %s in %s"), *A.At, *World->GetName());
					return false;
				}
			}
			const FVector Wanted = Target ? Target->GetActorTransform().TransformPosition(A.Offset) : Requested;

			// The spot asked for, else the nearest free one around it: in a level with props an exact spot is often
			// taken. Each candidate keeps facing the target when there is one.
			FVector Location = Wanted;
			FRotator Rotation(0, A.Yaw.Get(Pawn->GetActorRotation().Yaw), 0);
			bool bPlaced = false;
			for (int32 Ring = 0; Ring <= 6 && !bPlaced; ++Ring)
			{
				const int32 Count = Ring == 0 ? 1 : 12;
				for (int32 i = 0; i < Count && !bPlaced; ++i)
				{
					const double Angle = 2.0 * PI * i / Count;
					Location = Wanted + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (60.0 * Ring);
					if (Target && A.bFace && !A.Yaw.IsSet())
					{
						Rotation.Yaw = (Target->GetActorLocation() - Location).Rotation().Yaw;
					}
					bPlaced = Pawn->TeleportTo(Location, Rotation);
				}
			}
			if (!bPlaced)
			{
				Err = FString::Printf(TEXT("place: %s fits nowhere within 360 units of %s"), *Pawn->GetName(), *Wanted.ToCompactString());
				return false;
			}
			PC->SetControlRotation(Rotation);
			if (Remote)
			{
				// The owning client keeps its own facing and moves from it; it has to turn itself.
				const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
				PIERemotePlayers::RememberId(Id, Remote);
				Remote->SendFace(Id, Rotation);
				Relayed.Add({ Id, A.Client, FString::Printf(TEXT("turning to yaw %.0f"), Rotation.Yaw) });
			}
			UE_LOG(LogPIEStudio, Log, TEXT("[UAT] placed %s at %s yaw %.0f"), *Pawn->GetName(), *Location.ToCompactString(), Rotation.Yaw);
			return true;
		}

		/** Saves a player's view into the report's captures folder; the step checks it was written. */
		bool RequestCapture(const FUatAction& A, FString& Err)
		{
			const FString Label = A.Command.IsEmpty() ? FString::FromInt(StepCaptures.Num() + 1) : A.Command;
			const FString File = FString::Printf(TEXT("captures/step%02d_client%d_%s.jpg"), StepIndex + 1, A.Client, *Label);
			const FString Path = ReportDir / File;
			if (A.Client > 0)
			{
				UPIEStudioRemoteControl* Remote = PIERemotePlayers::Find(A.Client, Err);
				if (!Remote)
				{
					return false;
				}
				const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
				PIERemotePlayers::RememberId(Id, Remote);
				Remote->SendCapture(Id, Path);
				Relayed.Add({ Id, A.Client, FString::Printf(TEXT("capture %s"), *File) });
				StepCaptures.Add({ A.Client, File, nullptr });
				return true;
			}
			UWorld* Server = PIERemotePlayers::FindServerWorld();
			const FWorldContext* HostContext = Server ? GEditor->GetWorldContextFromWorld(Server) : nullptr;
			if (!HostContext || !HostContext->GameViewport || !HostContext->GameViewport->Viewport)
			{
				Err = TEXT("capture: the host player has no viewport");
				return false;
			}
			if (!HostCapture.IsValid())
			{
				HostCapture = FSceneViewExtensions::NewExtension<FPIEViewportCapture>();
				HostCapture->SetOutputFormat(/*bJpeg*/ true, 85);
				HostCapture->SetEnabled(true);
			}
			HostCapture->SetTargetViewport(HostContext->GameViewport->Viewport);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			TSharedPtr<int32> Written = MakeShared<int32>(0);
			HostCapture->RequestCapture(Path, [Written](bool bOk) { *Written = bOk ? 1 : 2; });
			StepCaptures.Add({ 0, File, Written });
			return true;
		}

		bool RunAction(const FUatAction& A, FString& Err)
		{
			UInputAction* Input = nullptr;
			if (A.Kind == FUatAction::EKind::Tape || A.Kind == FUatAction::EKind::Hold || A.Kind == FUatAction::EKind::Inject)
			{
				Input = Cast<UInputAction>(FSoftObjectPath(A.ActionPath).TryLoad());
				if (!Input)
				{
					Err = FString::Printf(TEXT("InputAction %s not found"), *A.ActionPath);
					return false;
				}
			}

			if (A.Kind == FUatAction::EKind::Place)
			{
				return Place(A, Err);
			}
			if (A.Kind == FUatAction::EKind::Capture)
			{
				return RequestCapture(A, Err);
			}

			if (A.Client > 0)
			{
				UPIEStudioRemoteControl* Remote = PIERemotePlayers::Find(A.Client, Err);
				if (!Remote)
				{
					return false;
				}
				const FString Id = PIERemotePlayers::NewId(TEXT("uat"));
				PIERemotePlayers::RememberId(Id, Remote);
				FString What;
				switch (A.Kind)
				{
				case FUatAction::EKind::Tape:
					Remote->SendTape(Id, FSoftObjectPath(Input), A.Values);
					What = FString::Printf(TEXT("a tape on %s"), *Input->GetName());
					break;
				case FUatAction::EKind::Inject:
					Remote->SendInject(Id, FSoftObjectPath(Input), A.Value);
					What = FString::Printf(TEXT("an inject on %s"), *Input->GetName());
					break;
				case FUatAction::EKind::Console:
					Remote->SendConsole(Id, A.Command);
					What = FString::Printf(TEXT("console '%s'"), *A.Command);
					break;
				case FUatAction::EKind::Hold:
					Remote->SendStartHold(Id, FSoftObjectPath(Input), A.Value);
					PendingStops.Add({ Now() + A.Ms / 1000.0, A.Client, Id });
					What = FString::Printf(TEXT("a hold on %s"), *Input->GetName());
					break;
				default: break;
				}
				Relayed.Add({ Id, A.Client, What });
				return true;
			}

			// The editor's own player.
			switch (A.Kind)
			{
			case FUatAction::EKind::Tape:
				return !FPIEInputInjector::StartTape(Input, A.Values, 60, FString(), Err).IsEmpty();
			case FUatAction::EKind::Inject:
				return FPIEInputInjector::InjectOnce(Input, ToValue(Input, A.Value), Err);
			case FUatAction::EKind::Hold:
			{
				// Timed by the wall clock, like a remote hold; a tape would stretch with the frame rate.
				const FString Id = FPIEInputInjector::StartHold(Input, ToValue(Input, A.Value), FString(), Err);
				if (Id.IsEmpty())
				{
					return false;
				}
				PendingStops.Add({ Now() + A.Ms / 1000.0, 0, Id });
				return true;
			}
			case FUatAction::EKind::Console:
			{
				UWorld* World = PIERemotePlayers::FindServerWorld();
				if (!World) World = GEditor->PlayWorld;
				APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
				if (PC && PC->IsLocalController())
				{
					PC->ConsoleCommand(A.Command, true);
				}
				else if (World)
				{
					GEngine->Exec(World, *A.Command);
				}
				return World != nullptr;
			}
			default:
				return true;
			}
		}

		void MarkStepStart()
		{
			HostMark = HostLog.Num();
			Relayed.Reset();
			StepCaptures.Reset();
			ClientMarks.Reset();
			for (const TPair<int32, FClientLogTail>& Pair : ClientLogs)
			{
				ClientMarks.Add(Pair.Key, Pair.Value.Lines.Num());
			}
		}

		void BeginStop()
		{
			if (State == EState::Stopping || State == EState::Finished)
			{
				return;
			}
			State = EState::Stopping;
			PhaseStart = Now();
			if (GEditor && GEditor->IsPlaySessionRequestQueued())
			{
				GEditor->CancelRequestPlaySession();
			}
			if (GEditor && GEditor->PlayWorld)
			{
				GEditor->RequestEndPlayMap();
			}
		}

		void Finish()
		{
			State = EState::Finished;
			GLog->RemoveOutputDevice(&HostLog);
			RestoreSettings();
			if (HostCapture.IsValid())
			{
				HostCapture->SetEnabled(false);
				HostCapture.Reset();
			}
			for (int32 i = Results.Num(); i < Steps.Num(); ++i)
			{
				Results.Add({ Steps[i].Name, TEXT("SKIPPED"), {} });
			}
			WriteReport();
			UE_LOG(LogPIEStudio, Log, TEXT("[UAT] %s finished: %s (%s)"), *Name, *OverallVerdict(), *ReportDir);
		}

		FString OverallVerdict() const
		{
			if (!Error.IsEmpty() || bAborted)
			{
				return TEXT("ERROR");
			}
			for (const FUatStepResult& R : Results)
			{
				if (R.Verdict != TEXT("PASS")) return TEXT("FAIL");
			}
			return TEXT("PASS");
		}

		void WriteReport()
		{
			IFileManager::Get().MakeDirectory(*ReportDir, true);

			FString Md = FString::Printf(TEXT("# UAT: %s\n\nVerdict: **%s**\n\n"), *Name, *OverallVerdict());
			if (!Error.IsEmpty()) Md += FString::Printf(TEXT("Error: %s\n\n"), *Error);
			Md += TEXT("| # | Step | Verdict |\n|---|---|---|\n");
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				Md += FString::Printf(TEXT("| %d | %s | %s |\n"), i + 1, *Results[i].Name, *Results[i].Verdict);
			}
			Md += TEXT("\n");
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				if (Results[i].Verdict == TEXT("PASS") || Results[i].Details.Num() == 0) continue;
				Md += FString::Printf(TEXT("## %d. %s: %s\n\n"), i + 1, *Results[i].Name, *Results[i].Verdict);
				for (const FString& D : Results[i].Details) Md += FString::Printf(TEXT("- `%s`\n"), *D.Replace(TEXT("`"), TEXT("'")));
				Md += TEXT("\n");
			}
			bool bAnyCapture = false;
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				for (const FString& C : Results[i].Captures)
				{
					if (!bAnyCapture) { Md += TEXT("## Captures\n\n"); bAnyCapture = true; }
					Md += FString::Printf(TEXT("%d. %s: ![%s](%s)\n\n"), i + 1, *Results[i].Name, *FPaths::GetBaseFilename(C), *C);
				}
			}
			FFileHelper::SaveStringToFile(Md, *(ReportDir / TEXT("report.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

			FString Json;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(BuildStatus().ToSharedRef(), Writer);
			FFileHelper::SaveStringToFile(Json, *(ReportDir / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		TSharedPtr<FJsonObject> BuildStatus() const
		{
			static const TCHAR* StateNames[] = { TEXT("idle"), TEXT("starting"), TEXT("joining"), TEXT("settling"), TEXT("acting"), TEXT("watching"), TEXT("stopping"), TEXT("finished") };
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("state"), StateNames[static_cast<int32>(State)]);
			S->SetStringField(TEXT("name"), Name);
			S->SetNumberField(TEXT("steps_total"), Steps.Num());
			S->SetNumberField(TEXT("steps_done"), Results.Num());
			if (State == EState::Acting || State == EState::Watching)
			{
				S->SetStringField(TEXT("current_step"), Steps.IsValidIndex(StepIndex) ? Steps[StepIndex].Name : FString());
			}
			if (State == EState::Finished)
			{
				S->SetStringField(TEXT("verdict"), OverallVerdict());
				S->SetStringField(TEXT("report_dir"), ReportDir);
			}
			if (!Error.IsEmpty()) S->SetStringField(TEXT("error"), Error);

			TArray<TSharedPtr<FJsonValue>> Arr;
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("step"), i + 1);
				R->SetStringField(TEXT("name"), Results[i].Name);
				R->SetStringField(TEXT("verdict"), Results[i].Verdict);
				TArray<TSharedPtr<FJsonValue>> Details;
				for (const FString& D : Results[i].Details) Details.Add(MakeShared<FJsonValueString>(D));
				R->SetArrayField(TEXT("details"), Details);
				if (Results[i].Captures.Num())
				{
					TArray<TSharedPtr<FJsonValue>> Captures;
					for (const FString& C : Results[i].Captures) Captures.Add(MakeShared<FJsonValueString>(ReportDir / C));
					R->SetArrayField(TEXT("captures"), Captures);
				}
				Arr.Add(MakeShared<FJsonValueObject>(R));
			}
			S->SetArrayField(TEXT("results"), Arr);
			return S;
		}

		bool Tick()
		{
			const double T = Now();
			PollClientLogs();

			for (int32 i = PendingStops.Num() - 1; i >= 0; --i)
			{
				if (T >= PendingStops[i].At)
				{
					if (PendingStops[i].Client == 0)
					{
						FPIEInputInjector::StopHold(PendingStops[i].Id);
					}
					else if (UPIEStudioRemoteControl* Remote = PIERemotePlayers::FindById(PendingStops[i].Id))
					{
						Remote->SendStop(PendingStops[i].Id);
					}
					PendingStops.RemoveAt(i);
				}
			}

			switch (State)
			{
			case EState::Starting:
				if (GEditor->PlayWorld)
				{
					State = EState::Joining;
					PhaseStart = T;
				}
				else if (T - PhaseStart > 60.0)
				{
					Fail(TEXT("PIE did not start within 60 s"));
				}
				break;

			case EState::Joining:
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended while players were joining"));
				}
				else if (AllJoined())
				{
					FString Err;
					if (!BeginClientLogs(Err))
					{
						Fail(Err);
						break;
					}
					State = EState::Settling;
					PhaseStart = T;
				}
				else if (T - PhaseStart > JoinTimeoutS)
				{
					Fail(FString::Printf(TEXT("players did not finish joining within %.0f s"), JoinTimeoutS));
				}
				break;

			case EState::Settling:
				if (T - PhaseStart >= SettleMs / 1000.0)
				{
					StepIndex = 0;
					ActionIndex = 0;
					MarkStepStart();
					WaitUntil = 0.0;
					State = Steps.Num() ? EState::Acting : EState::Stopping;
					if (State == EState::Stopping) BeginStop();
				}
				break;

			case EState::Acting:
			{
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended during the run"));
					break;
				}
				if (T < WaitUntil)
				{
					break;
				}
				const FUatStep& Step = Steps[StepIndex];
				while (ActionIndex < Step.Do.Num())
				{
					const FUatAction& A = Step.Do[ActionIndex++];
					if (A.Kind == FUatAction::EKind::Wait)
					{
						WaitUntil = T + A.Ms / 1000.0;
						return true;
					}
					FString Err;
					if (!RunAction(A, Err))
					{
						Results.Add({ Step.Name, TEXT("FAIL"), { FString::Printf(TEXT("action %d could not run: %s"), ActionIndex, *Err) } });
						AdvanceStep(T);
						return true;
					}
				}
				State = EState::Watching;
				PhaseStart = T;
				break;
			}

			case EState::Watching:
			{
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended during the run"));
					break;
				}
				const FUatStep& Step = Steps[StepIndex];
				const bool bWindowOver = T - PhaseStart >= Step.WindowMs / 1000.0;
				if (bWindowOver || CanFinishEarly(Step))
				{
					FUatStepResult R = Evaluate(Step);
					UE_LOG(LogPIEStudio, Log, TEXT("[UAT] %s step %d %s: %s"), *Name, StepIndex + 1, *Step.Name, *R.Verdict);
					Results.Add(MoveTemp(R));
					AdvanceStep(T);
				}
				break;
			}

			case EState::Stopping:
				if (!GEditor->PlayWorld)
				{
					Finish();
					return false;
				}
				if (T - PhaseStart > 60.0)
				{
					Finish();
					return false;
				}
				break;

			default:
				break;
			}
			return true;
		}

		void AdvanceStep(double T)
		{
			++StepIndex;
			ActionIndex = 0;
			WaitUntil = 0.0;
			if (bAborted || StepIndex >= Steps.Num())
			{
				BeginStop();
				return;
			}
			MarkStepStart();
			State = EState::Acting;
			PhaseStart = T;
		}
	};

	FPIEUatRunner& FPIEUatRunner::Get()
	{
		static FPIEUatRunner Instance;
		return Instance;
	}

	FPIEUatRunner::FPIEUatRunner() : Impl(MakeUnique<FImpl>()) {}
	FPIEUatRunner::~FPIEUatRunner() = default;

	void FPIEUatRunner::Init() {}

	void FPIEUatRunner::Shutdown()
	{
		if (IsRunning())
		{
			Impl->RestoreSettings();
		}
		if (Impl->HostCapture.IsValid())
		{
			Impl->HostCapture->SetEnabled(false);
			Impl->HostCapture.Reset();
		}
		if (Impl->TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Impl->TickHandle);
			Impl->TickHandle.Reset();
		}
		if (GLog)
		{
			GLog->RemoveOutputDevice(&Impl->HostLog);
		}
	}

	namespace
	{
		/** Opens the scenario's map unless it is already the editor world. Never prompts: a dirty map is an error. */
		bool LoadMapIfNeeded(FString Map, FString& OutError)
		{
			if (int32 Dot = INDEX_NONE; Map.FindChar(TEXT('.'), Dot))
			{
				Map.LeftInline(Dot);
			}
			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			UPackage* Current = EditorWorld ? EditorWorld->GetOutermost() : nullptr;
			if (Current && Current->GetName() == Map)
			{
				return true;
			}
			if (Current && Current->IsDirty())
			{
				OutError = FString::Printf(TEXT("map %s has unsaved changes; save or discard them before a run opens %s"), *Current->GetName(), *Map);
				return false;
			}
			FString File;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Map, File, FPackageName::GetMapPackageExtension()) || !FPaths::FileExists(File))
			{
				OutError = FString::Printf(TEXT("map %s does not exist"), *Map);
				return false;
			}
			if (!FEditorFileUtils::LoadMap(File, /*LoadAsTemplate*/ false, /*bShowProgress*/ false))
			{
				OutError = FString::Printf(TEXT("map %s failed to load"), *Map);
				return false;
			}
			return true;
		}
	}

	bool FPIEUatRunner::IsRunning() const
	{
		return Impl->State != FImpl::EState::Idle && Impl->State != FImpl::EState::Finished;
	}

	bool FPIEUatRunner::Start(const TSharedPtr<FJsonObject>& Scenario, FString& OutError)
	{
		if (IsRunning())
		{
			OutError = FString::Printf(TEXT("UAT '%s' is already running"), *Impl->Name);
			return false;
		}
		if (!GEditor || GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
		{
			OutError = TEXT("Stop the current PIE session first; the run starts its own");
			return false;
		}

		FImpl Fresh;
		Fresh.Name = Scenario->HasField(TEXT("name")) ? Scenario->GetStringField(TEXT("name")) : TEXT("uat");
		const TSharedPtr<FJsonObject>* Pie = nullptr;
		if (Scenario->TryGetObjectField(TEXT("pie"), Pie))
		{
			double N = 0;
			if ((*Pie)->TryGetNumberField(TEXT("players"), N)) Fresh.Players = FMath::Clamp(static_cast<int32>(N), 1, 16);
			FString Mode;
			if ((*Pie)->TryGetStringField(TEXT("net_mode"), Mode))
			{
				if (!Mode.Equals(TEXT("listen"), ESearchCase::CaseSensitive) && !Mode.Equals(TEXT("dedicated"), ESearchCase::CaseSensitive))
				{
					OutError = FString::Printf(TEXT("pie.net_mode must be \"listen\" or \"dedicated\", not \"%s\""), *Mode);
					return false;
				}
				Fresh.bListen = Mode.Equals(TEXT("listen"), ESearchCase::CaseSensitive);
			}
			// A dedicated server only stays inside the editor, where the run can read it, in one-process PIE.
			bool b = !Fresh.bListen;
			const bool bOneProcessGiven = (*Pie)->TryGetBoolField(TEXT("one_process"), b);
			Fresh.bOneProcess = b;
			if (!Fresh.bListen && bOneProcessGiven && !b)
			{
				OutError = TEXT("net_mode \"dedicated\" needs one_process true: a separate-process dedicated server runs outside the editor, where the run cannot drive or read it");
				return false;
			}
			(*Pie)->TryGetBoolField(TEXT("windowless"), Fresh.bWindowless);
			if (Fresh.bWindowless && Fresh.bOneProcess)
			{
				OutError = TEXT("windowless applies to separate-process clients; set one_process false");
				return false;
			}
			double Fps = 0;
			if ((*Pie)->TryGetNumberField(TEXT("client_fps"), Fps))
			{
				if (Fps < 1 || Fps > 1000)
				{
					OutError = TEXT("pie.client_fps must be between 1 and 1000");
					return false;
				}
				Fresh.ClientFps = static_cast<int32>(Fps);
			}
		}
		double N = 0;
		if (Scenario->TryGetNumberField(TEXT("join_timeout_s"), N)) Fresh.JoinTimeoutS = static_cast<float>(N);
		if (Scenario->TryGetNumberField(TEXT("settle_ms"), N)) Fresh.SettleMs = static_cast<float>(N);

		const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
		if (!Scenario->TryGetArrayField(TEXT("steps"), StepsArr) || StepsArr->Num() == 0)
		{
			OutError = TEXT("scenario has no steps");
			return false;
		}
		for (int32 i = 0; i < StepsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> SO = (*StepsArr)[i]->AsObject();
			if (!SO.IsValid())
			{
				OutError = FString::Printf(TEXT("step %d is not an object"), i + 1);
				return false;
			}
			FUatStep Step;
			Step.Name = SO->HasField(TEXT("name")) ? SO->GetStringField(TEXT("name")) : FString::Printf(TEXT("step %d"), i + 1);
			if (SO->TryGetNumberField(TEXT("window_ms"), N)) Step.WindowMs = static_cast<float>(N);
			FString Err;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (SO->TryGetArrayField(TEXT("do"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					FUatAction A;
					if (!V->AsObject().IsValid() || !ParseAction(V->AsObject(), A, Err))
					{
						OutError = FString::Printf(TEXT("step %d (%s): %s"), i + 1, *Step.Name, *Err);
						return false;
					}
					if (A.Client < 0 || A.Client >= Fresh.Players + (Fresh.bListen ? 0 : 1) || (A.Client == 0 && !Fresh.bListen && A.Kind != FUatAction::EKind::Wait))
					{
						OutError = FString::Printf(TEXT("step %d (%s): client %d does not exist with %d players (%s)"), i + 1, *Step.Name, A.Client, Fresh.Players, Fresh.bListen ? TEXT("listen") : TEXT("dedicated"));
						return false;
					}
					Step.Do.Add(MoveTemp(A));
				}
			}
			for (const TCHAR* Key : { TEXT("expect"), TEXT("forbid") })
			{
				if (SO->TryGetArrayField(Key, Arr))
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						FUatRule R;
						if (!V->AsObject().IsValid() || !ParseRule(V->AsObject(), R, Err))
						{
							OutError = FString::Printf(TEXT("step %d (%s): %s"), i + 1, *Step.Name, *Err);
							return false;
						}
						// client:0 is the listen server's own player, whose lines are in the host log.
						const bool bHostLog = R.Log == TEXT("host") || (R.Client == 0 && Fresh.bListen);
						if (!bHostLog && (R.Client < 1 || R.Client > Fresh.RemoteCount()))
						{
							OutError = FString::Printf(TEXT("step %d (%s): log %s does not exist with %d players (%s)"), i + 1, *Step.Name, *R.Log,
								Fresh.Players, Fresh.bListen ? TEXT("listen") : TEXT("dedicated"));
							return false;
						}
						(FCString::Strcmp(Key, TEXT("expect")) == 0 ? Step.Expect : Step.Forbid).Add(MoveTemp(R));
					}
				}
			}
			const bool bCaptures = Step.Do.ContainsByPredicate([](const FUatAction& A) { return A.Kind == FUatAction::EKind::Capture; });
			if (Step.Expect.Num() == 0 && Step.Forbid.Num() == 0 && !bCaptures)
			{
				OutError = FString::Printf(TEXT("step %d (%s) checks nothing; give it expect, forbid or a capture"), i + 1, *Step.Name);
				return false;
			}
			Fresh.Steps.Add(MoveTemp(Step));
		}

		FString Map;
		if (Scenario->TryGetStringField(TEXT("map"), Map) && !Map.IsEmpty() && !LoadMapIfNeeded(Map, OutError))
		{
			return false;
		}

		// Commit.
		if (Impl->TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Impl->TickHandle);
		}
		Impl = MakeUnique<FImpl>();
		Impl->Name = Fresh.Name;
		Impl->Players = Fresh.Players;
		Impl->bListen = Fresh.bListen;
		Impl->bOneProcess = Fresh.bOneProcess;
		Impl->JoinTimeoutS = Fresh.JoinTimeoutS;
		Impl->SettleMs = Fresh.SettleMs;
		Impl->bWindowless = Fresh.bWindowless;
		Impl->ClientFps = Fresh.ClientFps;
		Impl->Steps = MoveTemp(Fresh.Steps);
		Impl->ReportDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PIEStudio") / TEXT("UAT") /
			FString::Printf(TEXT("%s_%s"), *FPaths::MakeValidFileName(Impl->Name), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
		IFileManager::Get().MakeDirectory(*Impl->ReportDir, true);

		GLog->AddOutputDevice(&Impl->HostLog);
		Impl->State = FImpl::EState::Starting;
		Impl->PhaseStart = FImpl::Now();
		Impl->ConfigureAndStartPIE();
		FImpl* Raw = Impl.Get();
		Impl->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Raw](float) { return Raw->Tick(); }));
		UE_LOG(LogPIEStudio, Log, TEXT("[UAT] %s started: %d players, %s, %s, %d steps"), *Impl->Name, Impl->Players,
			Impl->bListen ? TEXT("listen server") : TEXT("dedicated server"), Impl->bOneProcess ? TEXT("one process") : TEXT("separate processes"), Impl->Steps.Num());
		return true;
	}

	void FPIEUatRunner::Abort(const FString& Reason)
	{
		if (!IsRunning())
		{
			return;
		}
		Impl->bAborted = true;
		Impl->Error = Reason.IsEmpty() ? TEXT("aborted") : Reason;
		Impl->BeginStop();
	}

	TSharedPtr<FJsonObject> FPIEUatRunner::Status() const
	{
		return Impl->BuildStatus();
	}
}
