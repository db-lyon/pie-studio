#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FRHIGPUTextureReadback;

namespace UEMCPPIE
{

class FPIEViewportCapture : public FSceneViewExtensionBase
{
public:
	FPIEViewportCapture(const FAutoRegister& AutoReg);
	virtual ~FPIEViewportCapture() override;

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	void SetEnabled(bool bEnable);
	void RequestCapture(const FString& OutputPath);
	int32 GetCapturedCount() const;

	// Output encoding for subsequent captures. JPEG (default) is far smaller and
	// is what a vision model reads well; PNG is lossless for pixel-exact work.
	// The path extension passed to RequestCapture should match.
	void SetOutputFormat(bool bInUseJpeg, int32 InQuality);

	// Block-drain any in-flight GPU readbacks and dispatch their PNG writes.
	// Call from the game thread at teardown before releasing this extension.
	void FlushPending();

private:
	// A GPU->CPU copy that has been enqueued but not yet completed. All fields
	// are touched only on the render thread.
	struct FInFlightReadback
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;
		FString Path;
		int32 Width = 0;
		int32 Height = 0;
		bool bSwapRB = false;
		bool bJpeg = true;
		int32 Quality = 80;
	};

	// Poll (or, when bDrainAll, block on) in-flight readbacks and hand each
	// completed one off to a background PNG write. Render thread only.
	void ProcessReadbacks_RenderThread(FRHICommandListImmediate& RHICmdList, bool bDrainAll);

	TArray<FInFlightReadback> InFlight;

	std::atomic<bool> bEnabled{false};
	std::atomic<bool> bUseJpeg{true};
	std::atomic<int32> JpegQuality{80};
	mutable FCriticalSection Lock;
	FString PendingPath;
	std::atomic<int32> CapturedCount{0};
};

} // namespace UEMCPPIE
