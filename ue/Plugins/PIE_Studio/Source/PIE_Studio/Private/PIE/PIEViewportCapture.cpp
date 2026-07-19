#include "PIEViewportCapture.h"
#include "PIE_StudioModule.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"

namespace UEMCPPIE
{

BEGIN_SHADER_PARAMETER_STRUCT(FMCPCapturePassParameters, )
END_SHADER_PARAMETER_STRUCT()

// Encode a BGRA FColor buffer to disk as JPEG or PNG. Safe on a background
// thread: FImageUtils PNG compression is thread-safe, and IImageWrapper is fine
// once the ImageWrapper module is loaded (it is a module dependency, loaded at
// startup). Falls back to PNG if the JPEG wrapper is unavailable.
static bool EncodeColorsToFile(const TArray<FColor>& Pixels, int32 W, int32 H, bool bJpeg, int32 Quality, const FString& Path)
{
	if (bJpeg)
	{
		IImageWrapperModule* IWM = FModuleManager::Get().GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (IWM)
		{
			TSharedPtr<IImageWrapper> Wrapper = IWM->CreateImageWrapper(EImageFormat::JPEG);
			if (Wrapper.IsValid() &&
				Wrapper->SetRaw(Pixels.GetData(), static_cast<int64>(Pixels.Num()) * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
			{
				const TArray64<uint8>& Data = Wrapper->GetCompressed(FMath::Clamp(Quality, 1, 100));
				if (Data.Num() > 0)
				{
					return FFileHelper::SaveArrayToFile(Data, *Path);
				}
			}
		}
		// fall through to PNG on any failure
	}
	TArray64<uint8> PNG;
	FImageUtils::PNGCompressImageArray(W, H, Pixels, PNG);
	return FFileHelper::SaveArrayToFile(PNG, *Path);
}

FPIEViewportCapture::FPIEViewportCapture(const FAutoRegister& AutoReg)
	: FSceneViewExtensionBase(AutoReg)
{
}

FPIEViewportCapture::~FPIEViewportCapture()
{
	// InFlight should already be empty: SetEnabled(false) drains it. This is
	// only here so the TUniquePtr<FRHIGPUTextureReadback> destructor sees the
	// complete type (the header forward-declares it).
}

bool FPIEViewportCapture::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (!bEnabled.load(std::memory_order_acquire)) return false;
	if (!GEngine || !GEngine->GameViewport) return false;
	return Context.Viewport == GEngine->GameViewport->Viewport;
}

void FPIEViewportCapture::SetEnabled(bool bEnable)
{
	bEnabled.store(bEnable, std::memory_order_release);
	if (!bEnable)
	{
		{
			FScopeLock SL(&Lock);
			PendingPath.Reset();
		}
		// Drain outstanding GPU copies so their PNGs get written and the RHI
		// resources are released on the render thread before we're torn down.
		FlushPending();
	}
}

void FPIEViewportCapture::RequestCapture(const FString& OutputPath)
{
	FScopeLock SL(&Lock);
	PendingPath = OutputPath;
}

void FPIEViewportCapture::SetOutputFormat(bool bInUseJpeg, int32 InQuality)
{
	bUseJpeg.store(bInUseJpeg, std::memory_order_release);
	JpegQuality.store(FMath::Clamp(InQuality, 1, 100), std::memory_order_release);
}

int32 FPIEViewportCapture::GetCapturedCount() const
{
	return CapturedCount.load(std::memory_order_acquire);
}

void FPIEViewportCapture::ProcessReadbacks_RenderThread(FRHICommandListImmediate& RHICmdList, bool bDrainAll)
{
	// A full drain (teardown) blocks once so every enqueued copy is guaranteed
	// ready; the per-frame path never blocks and just skips copies still in
	// flight, picking them up on a later frame.
	if (bDrainAll && InFlight.Num() > 0)
	{
		RHICmdList.BlockUntilGPUIdle();
	}

	for (int32 i = 0; i < InFlight.Num(); )
	{
		FInFlightReadback& R = InFlight[i];
		if (!bDrainAll && !R.Readback->IsReady())
		{
			++i;
			continue;
		}

		const int32 W = R.Width;
		const int32 H = R.Height;
		int32 RowPitchInPixels = 0;
		void* Data = R.Readback->Lock(RowPitchInPixels);
		if (Data && RowPitchInPixels >= W)
		{
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(W * H);
			const FColor* Src = reinterpret_cast<const FColor*>(Data);
			for (int32 y = 0; y < H; ++y)
			{
				FMemory::Memcpy(&Pixels[y * W], Src + y * RowPitchInPixels, W * sizeof(FColor));
			}
			R.Readback->Unlock();

			// Async readback returns the texture's native byte order; swap when
			// the source is RGBA so the PNG matches FColor's BGRA layout.
			if (R.bSwapRB)
			{
				for (FColor& C : Pixels) { Swap(C.R, C.B); }
			}

			CapturedCount.fetch_add(1, std::memory_order_relaxed);
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
				[Pix = MoveTemp(Pixels), W, H, Path = R.Path, bJpeg = R.bJpeg, Quality = R.Quality]()
				{
					EncodeColorsToFile(Pix, W, H, bJpeg, Quality, Path);
				});
		}
		else if (Data)
		{
			R.Readback->Unlock();
		}

		InFlight.RemoveAt(i);
	}
}

void FPIEViewportCapture::FlushPending()
{
	FPIEViewportCapture* Self = this;
	ENQUEUE_RENDER_COMMAND(MCPViewportCaptureDrain)(
		[Self](FRHICommandListImmediate& RHICmdList)
		{
			Self->ProcessReadbacks_RenderThread(RHICmdList, /*bDrainAll=*/true);
		});
	// Block the game thread until the drain has run and released its resources.
	FlushRenderingCommands();
}

void FPIEViewportCapture::PostRenderViewFamily_RenderThread(
	FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Retire any completed copies from earlier frames first. Never blocks.
	ProcessReadbacks_RenderThread(GraphBuilder.RHICmdList, /*bDrainAll=*/false);

	FString Path;
	{
		FScopeLock SL(&Lock);
		if (PendingPath.IsEmpty()) return;
		Path = PendingPath;
		PendingPath.Reset();
	}

	const FRenderTarget* RT = InViewFamily.RenderTarget;
	if (!RT) return;

	FTextureRHIRef Texture = RT->GetRenderTargetTexture();
	if (!Texture.IsValid()) return;

	const FIntPoint Size = RT->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) return;

	const bool bSwapRB = (Texture->GetFormat() == PF_R8G8B8A8);

	// Enqueue an async GPU->CPU copy inside an RDG pass so it runs after the
	// scene has finished rendering. Unlike ReadSurfaceData this does not stall
	// the render thread; we poll for completion on a later frame.
	TUniquePtr<FRHIGPUTextureReadback> Readback =
		MakeUnique<FRHIGPUTextureReadback>(TEXT("MCPViewportCapture"));
	FRHIGPUTextureReadback* ReadbackPtr = Readback.Get();

	FMCPCapturePassParameters* Params = GraphBuilder.AllocParameters<FMCPCapturePassParameters>();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MCPViewportCapture"),
		Params,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[ReadbackPtr, RHITex = Texture.GetReference()](FRHICommandListImmediate& RHICmdList)
		{
			ReadbackPtr->EnqueueCopy(RHICmdList, RHITex);
		});

	FInFlightReadback Entry;
	Entry.Readback = MoveTemp(Readback);
	Entry.Path = Path;
	Entry.Width = Size.X;
	Entry.Height = Size.Y;
	Entry.bSwapRB = bSwapRB;
	Entry.bJpeg = bUseJpeg.load(std::memory_order_acquire);
	Entry.Quality = JpegQuality.load(std::memory_order_acquire);
	InFlight.Add(MoveTemp(Entry));
}

} // namespace UEMCPPIE
