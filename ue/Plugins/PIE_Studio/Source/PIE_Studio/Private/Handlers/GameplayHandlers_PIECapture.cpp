// Standalone viewport capture handler: capture.
// Member of FGameplayHandlers. Grabs N frames from the LIVE PIE viewport,
// decoupled from replay, so observe/inject flows can be visual too. Writes
// JPEGs plus a labeled contact sheet.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIEViewportCapture.h"
#include "PIE/PIEContactSheet.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "SceneViewExtension.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	using namespace UEMCPPIE;

	// A self-contained, self-terminating capture run. Grabs frames across rendered
	// frames on OnEndFrame, then (after letting the async image writes land)
	// composes a contact sheet and retires itself. Kept alive by GActiveSessions.
	class FCaptureSession : public TSharedFromThis<FCaptureSession>
	{
	public:
		FString Dir;
		FString ContactSheetPath;
		int32 Remaining = 1;
		int32 Every = 1;
		int32 Counter = 0;
		int32 Index = 0;
		bool bJpeg = true;
		int32 Quality = 80;
		bool bContactSheet = true;
		int32 DrainTicks = 0;
		bool bCapturing = true;
		TSharedPtr<FPIEViewportCapture> Capture;
		FDelegateHandle TickHandle;

		void Start()
		{
			Capture = FSceneViewExtensions::NewExtension<FPIEViewportCapture>();
			Capture->SetOutputFormat(bJpeg, Quality);
			Capture->SetEnabled(true);
			TickHandle = FCoreDelegates::OnEndFrame.AddSP(this, &FCaptureSession::Tick);
		}

		void Tick()
		{
			// If PIE went away, finalise immediately.
			const bool bPIE = (GEditor && GEditor->PlayWorld != nullptr);

			if (bCapturing && bPIE)
			{
				if (Remaining > 0 && (Counter % FMath::Max(1, Every)) == 0)
				{
					const TCHAR* Ext = bJpeg ? TEXT("jpg") : TEXT("png");
					const FString Path = Dir / FString::Printf(TEXT("frame_%05d.%s"), Index, Ext);
					Capture->RequestCapture(Path);
					++Index;
					--Remaining;
				}
				++Counter;
				if (Remaining <= 0)
				{
					bCapturing = false;
					DrainTicks = 12; // let async image writes land before composing
				}
				return;
			}

			// Draining / finalising.
			if (DrainTicks-- > 0 && bPIE)
			{
				return;
			}
			Finalise();
		}

		void Finalise()
		{
			if (Capture.IsValid())
			{
				Capture->SetEnabled(false); // blocks-drains GPU readbacks
				Capture.Reset();
			}

			if (bContactSheet)
			{
				const TCHAR* Pattern = bJpeg ? TEXT("frame_*.jpg") : TEXT("frame_*.png");
				TArray<FString> Frames;
				IFileManager::Get().FindFiles(Frames, *(Dir / Pattern), true, false);
				Frames.Sort();
				for (FString& F : Frames) { F = Dir / F; }
				if (Frames.Num() > 0)
				{
					TArray<FString> Labels;
					for (int32 i = 0; i < Frames.Num(); ++i) Labels.Add(FString::Printf(TEXT("F%d"), i));
					FContactSheetParams CP;
					CP.CellWidth = 300;
					if (ComposeContactSheet(Frames, ContactSheetPath, CP, &Labels))
					{
						// path already stored
					}
				}
			}

			FCoreDelegates::OnEndFrame.Remove(TickHandle);
			Retire(this);
		}

		static TArray<TSharedPtr<FCaptureSession>>& Registry()
		{
			static TArray<TSharedPtr<FCaptureSession>> R;
			return R;
		}
		static void Retire(FCaptureSession* Self)
		{
			Registry().RemoveAll([Self](const TSharedPtr<FCaptureSession>& S) { return S.Get() == Self; });
		}
	};
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieCapture(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	if (!GEditor || GEditor->PlayWorld == nullptr)
	{
		return MCPError(TEXT("capture requires a live PIE session"));
	}

	const int32 Frames = FMath::Clamp(OptionalInt(Params, TEXT("frames"), 1), 1, 2000);
	const int32 Every = FMath::Max(1, OptionalInt(Params, TEXT("every"), 1));
	const FString Format = OptionalString(Params, TEXT("format"), TEXT("jpg")).ToLower();
	const int32 Quality = FMath::Clamp(OptionalInt(Params, TEXT("quality"), 80), 1, 100);
	const bool bSheet = OptionalBool(Params, TEXT("contact_sheet"), true);

	FString Dir = OptionalString(Params, TEXT("output_dir"));
	if (Dir.IsEmpty())
	{
		Dir = FPaths::ProjectSavedDir() / TEXT("MCPCaptures") /
			FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	}
	IFileManager::Get().MakeDirectory(*Dir, true);

	TSharedPtr<FCaptureSession> Session = MakeShared<FCaptureSession>();
	Session->Dir = Dir;
	Session->Remaining = Frames;
	Session->Every = Every;
	Session->bJpeg = (Format != TEXT("png"));
	Session->Quality = Quality;
	Session->bContactSheet = bSheet;
	Session->ContactSheetPath = Dir / TEXT("contact.jpg");
	FCaptureSession::Registry().Add(Session);
	Session->Start();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("output_dir"), Dir);
	Result->SetNumberField(TEXT("frames_requested"), Frames);
	Result->SetStringField(TEXT("format"), Session->bJpeg ? TEXT("jpg") : TEXT("png"));
	Result->SetStringField(TEXT("frame_glob"), Dir / (Session->bJpeg ? TEXT("frame_*.jpg") : TEXT("frame_*.png")));
	if (bSheet)
	{
		Result->SetStringField(TEXT("contact_sheet_path"), Session->ContactSheetPath);
	}
	// Capture runs across rendered frames and images finalise asynchronously.
	Result->SetStringField(TEXT("note"),
		TEXT("Frames + contact sheet finalise a few frames after this call. Read the files from output_dir once PIE has ticked."));
	return MCPResult(Result);
}
