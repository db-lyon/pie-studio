#pragma once
#include "CoreMinimal.h"

// Local disposable spike: single-client PIE, mouse movement, left button and keyboard edges.
namespace UEMCPPIE::MousePrototype
{
	double BeginRecord();
	void EndRecord(const FString& Directory, double FirstSampleTime);
	void Shutdown();
	void LoadReplay(const FString& Directory);
	bool HasReplay();
	void BeginReplay();
	double ReplaySeconds();
	void Advance(double ElapsedMs);
	bool IsDone();
	void EndReplay();
}

namespace UEMCPPIE
{
	struct FSequence;
	namespace MousePrototype { void RemoveDuplicateButtonSteps(FSequence& Sequence, double FirstSampleTime); }
}
