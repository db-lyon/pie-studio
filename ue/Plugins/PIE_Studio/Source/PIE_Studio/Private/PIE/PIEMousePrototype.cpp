#include "PIEMousePrototype.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformTime.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "PIESequenceFormat.h"

namespace UEMCPPIE::MousePrototype
{
	struct FEvent
	{
		double Ms;
		FVector2D Position;
		FString Type;
		TArray<FString> Actions;
		FKey Key;
		uint32 CharacterCode = 0, KeyCode = 0;
		int32 Modifiers = 0;
		bool bRepeat = false;
	};
	static TArray<FEvent> Recorded, Playback;
	static double RecordOrigin = 0, ReplayOrigin = 0;
	static int32 Next = 0;
	static bool bDispatching = false, bHeld = false;
	static bool bRecordedGameHeld = false, bReplayGameHeld = false;
	static FVector2D LastPosition;
	static TSharedPtr<IInputProcessor> Processor;
	static TMap<FKey, bool> RecordedKeys, ReplayKeys;

	static TSharedPtr<SViewport> Viewport()
	{
		APlayerController* PC = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
		return LP && LP->ViewportClient ? LP->ViewportClient->GetGameViewportWidget() : nullptr;
	}

	class FListener final : public IInputProcessor
	{
	public:
		virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override { ObserveKey(E, true); return false; }
		virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& E) override { ObserveKey(E, false); return false; }
		static void ObserveKey(const FKeyEvent& E, bool bDown)
		{
			if (bDispatching || E.GetKey().IsGamepadKey()) return;
			const APlayerController* PC = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
			const auto V = Viewport();
			if (!PC || !V || !FSlateApplication::IsInitialized()) return;
			const bool* Existing = RecordedKeys.Find(E.GetKey());
			if (!Existing && (!bDown || !(V->HasKeyboardFocus() || V->HasFocusedDescendants()))) return;
			if (!Existing && FSlateApplication::Get().FindWidgetWindow(V.ToSharedRef()) != FSlateApplication::Get().GetActiveTopLevelWindow()) return;
			const bool bGame = Existing ? *Existing : !PC->bShowMouseCursor;
			FEvent Saved{(FPlatformTime::Seconds() - RecordOrigin) * 1000., FVector2D::ZeroVector,
				bGame ? (bDown ? TEXT("key_game_down") : TEXT("key_game_up")) : (bDown ? TEXT("key_down") : TEXT("key_up"))};
			Saved.Key = E.GetKey(); Saved.CharacterCode = E.GetCharacter(); Saved.KeyCode = E.GetKeyCode(); Saved.bRepeat = E.IsRepeat();
			Saved.Modifiers = (E.IsLeftShiftDown() ? 1 : 0) | (E.IsRightShiftDown() ? 2 : 0) |
				(E.IsLeftControlDown() ? 4 : 0) | (E.IsRightControlDown() ? 8 : 0) |
				(E.IsLeftAltDown() ? 16 : 0) | (E.IsRightAltDown() ? 32 : 0) |
				(E.IsLeftCommandDown() ? 64 : 0) | (E.IsRightCommandDown() ? 128 : 0) | (E.AreCapsLocked() ? 256 : 0);
			if (const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput))
				for (const FEnhancedActionKeyMapping& Mapping : Input->GetEnhancedActionMappingsView())
					if (Mapping.Key == E.GetKey() && Mapping.Action) Saved.Actions.AddUnique(Mapping.Action->GetPathName());
			Recorded.Add(MoveTemp(Saved));
			if (bDown) RecordedKeys.Add(E.GetKey(), bGame); else RecordedKeys.Remove(E.GetKey());
		}
		virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& E) override { Observe(E, TEXT("move")); return false; }
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& E) override
		{ if (E.GetEffectingButton() == EKeys::LeftMouseButton) Observe(E, TEXT("down")); return false; }
		virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& E) override
		{ if (E.GetEffectingButton() == EKeys::LeftMouseButton) Observe(E, TEXT("up")); return false; }
		static void Observe(const FPointerEvent& E, const TCHAR* Type)
		{
			if (bDispatching) return;
			const APlayerController* PC = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
			if (!PC) return;
			const bool bDown = FCString::Strcmp(Type, TEXT("down")) == 0;
			const bool bUp = FCString::Strcmp(Type, TEXT("up")) == 0;
			const bool bGameButton = (bDown && !PC->bShowMouseCursor) || (bUp && bRecordedGameHeld);
			// Keep absolute movement out of first-person look. Keep the opening button edge.
			if (!PC->bShowMouseCursor && !bGameButton) return;
			const auto V = Viewport();
			if (!V || !FSlateApplication::IsInitialized()) return;
			if (FSlateApplication::Get().FindWidgetWindow(V.ToSharedRef()) != FSlateApplication::Get().GetActiveTopLevelWindow()) return;
			const FGeometry& G = V->GetCachedGeometry();
			const FVector2D Size = G.GetLocalSize();
			const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
			if (Size.X <= 0 || Size.Y <= 0 || P.X < 0 || P.Y < 0 || P.X >= Size.X || P.Y >= Size.Y) return;
			FEvent Saved{(FPlatformTime::Seconds() - RecordOrigin) * 1000., P / Size, bGameButton ? (bDown ? TEXT("game_down") : TEXT("game_up")) : Type};
			if (bGameButton)
			{
				bRecordedGameHeld = bDown;
				if (const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PC->PlayerInput))
				{
					for (const FEnhancedActionKeyMapping& Mapping : Input->GetEnhancedActionMappingsView())
						if (Mapping.Key == EKeys::LeftMouseButton && Mapping.Action) Saved.Actions.AddUnique(Mapping.Action->GetPathName());
				}
			}
			Recorded.Add(MoveTemp(Saved));
		}
	};

	double BeginRecord()
	{
		Shutdown(); Recorded.Reset(); RecordedKeys.Reset(); bRecordedGameHeld = false; RecordOrigin = FPlatformTime::Seconds();
		if (FSlateApplication::IsInitialized())
		{
			Processor = MakeShared<FListener>();
			FSlateApplication::Get().RegisterInputPreProcessor(Processor, 0);
		}
		return RecordOrigin;
	}

	void Shutdown()
	{
		if (Processor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
		Processor.Reset();
	}

	void EndRecord(const FString& Directory, double FirstSampleTime)
	{
		Shutdown();
		auto Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("prototype"), TEXT("raw-mouse-v1-wall-clock"));
		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FEvent& E : Recorded)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("ms"), FMath::Max(0., E.Ms - FirstSampleTime * 1000.));
			O->SetNumberField(TEXT("x"), E.Position.X); O->SetNumberField(TEXT("y"), E.Position.Y);
			O->SetStringField(TEXT("type"), E.Type);
			if (E.Key.IsValid())
			{
				O->SetStringField(TEXT("key"), E.Key.ToString());
				O->SetNumberField(TEXT("character_code"), E.CharacterCode); O->SetNumberField(TEXT("key_code"), E.KeyCode);
				O->SetNumberField(TEXT("modifiers"), E.Modifiers); O->SetBoolField(TEXT("repeat"), E.bRepeat);
			}
			Events.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("events"), Events);
		FString Text; auto Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Text, *(Directory / TEXT("mouse-prototype.json")));
	}

	void LoadReplay(const FString& Directory)
	{
		EndReplay(); Playback.Reset(); Next = 0;
		FString Text; TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *(Directory / TEXT("mouse-prototype.json")))) return;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) return;
		for (const auto& Value : Root->GetArrayField(TEXT("events")))
		{
			const auto O = Value->AsObject();
			FEvent Saved{O->GetNumberField(TEXT("ms")), FVector2D(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y"))), O->GetStringField(TEXT("type"))};
			FString Key;
			if (O->TryGetStringField(TEXT("key"), Key))
			{
				Saved.Key = FKey(FName(*Key));
				O->TryGetNumberField(TEXT("character_code"), Saved.CharacterCode); O->TryGetNumberField(TEXT("key_code"), Saved.KeyCode);
				O->TryGetNumberField(TEXT("modifiers"), Saved.Modifiers); O->TryGetBoolField(TEXT("repeat"), Saved.bRepeat);
			}
			Playback.Add(MoveTemp(Saved));
		}
	}
	bool HasReplay() { return !Playback.IsEmpty(); }
	void BeginReplay() { ReplayOrigin = FPlatformTime::Seconds(); Next = 0; bHeld = false; LastPosition = FSlateApplication::Get().GetCursorPos(); }
	double ReplaySeconds() { return FPlatformTime::Seconds() - ReplayOrigin; }

	void RemoveDuplicateButtonSteps(FSequence& Sequence, double FirstSampleTime)
	{
		// Keyboard actions are replayed through their keys, including axis holds (WASD).
		// Do not inject the sampled action a second time.
		TSet<FString> KeyboardActions;
		for (const FEvent& Event : Recorded)
			if (Event.Key.IsValid()) for (const FString& Action : Event.Actions) KeyboardActions.Add(Action);
		Sequence.Steps.RemoveAll([FirstSampleTime, &KeyboardActions](const FStep& Step)
		{
			if (KeyboardActions.Contains(Step.Action)) return true;
			for (const FEvent& Event : Recorded)
				if (Event.Type == TEXT("game_down") && Event.Actions.Contains(Step.Action) &&
					FMath::Abs(Step.DelayMs - (Event.Ms - FirstSampleTime * 1000.)) < 100.) return true;
			return false;
		});
	}

	static void GameButton(bool bDown)
	{
		APlayerController* PC = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
		if (!PC) return;
		bReplayGameHeld = bDown;
		PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::LeftMouseButton,
			bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f, false, FPlatformTime::Cycles64()));
	}

	static void Dispatch(const FEvent& E)
	{
		if (E.Type.StartsWith(TEXT("key_")))
		{
			if (!E.Key.IsValid()) return;
			const bool bDown = E.Type.EndsWith(TEXT("down"));
			const bool bGame = E.Type.StartsWith(TEXT("key_game_"));
			if (bDown) ReplayKeys.Add(E.Key, bGame); else ReplayKeys.Remove(E.Key);
			if (bGame)
			{
				APlayerController* Controller = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
				if (Controller) Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), E.Key,
					bDown ? (E.bRepeat ? IE_Repeat : IE_Pressed) : IE_Released, bDown ? 1.f : 0.f, false, FPlatformTime::Cycles64()));
			}
			else if (FSlateApplication::IsInitialized())
			{
				const int32 M = E.Modifiers;
				const FModifierKeysState Mods(M & 1, M & 2, M & 4, M & 8, M & 16, M & 32, M & 64, M & 128, M & 256);
				const FKeyEvent KeyEvent(E.Key, Mods, uint32(0), E.bRepeat, E.CharacterCode, E.KeyCode);
				TGuardValue<bool> Guard(bDispatching, true);
				if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent); else FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
			}
			return;
		}
		if (E.Type == TEXT("game_down") || E.Type == TEXT("game_up"))
		{
			GameButton(E.Type == TEXT("game_down"));
			return;
		}
		const APlayerController* PC = GEditor && GEditor->PlayWorld ? GEditor->PlayWorld->GetFirstPlayerController() : nullptr;
		// Also protects older recordings containing absolute first-person mouse events.
		if (!PC || !PC->bShowMouseCursor)
		{
			// An absolute pointer event can outlive a mode switch. Skip it without
			// releasing unrelated recorded keyboard holds.
			if (bHeld && FSlateApplication::IsInitialized())
			{
				TGuardValue<bool> Guard(bDispatching, true); bHeld = false;
				FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, LastPosition, LastPosition, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
			}
			return;
		}
		const auto V = Viewport(); if (!V || !FSlateApplication::IsInitialized()) return;
		auto& App = FSlateApplication::Get();
		const FVector2D P = V->GetCachedGeometry().LocalToAbsolute(E.Position * V->GetCachedGeometry().GetLocalSize());
		TGuardValue<bool> Guard(bDispatching, true);
		App.SetCursorPos(P);
		if (E.Type == TEXT("down")) bHeld = true;
		if (E.Type == TEXT("up")) bHeld = false;
		TSet<FKey> Buttons; if (bHeld) Buttons.Add(EKeys::LeftMouseButton);
		const FPointerEvent Event(0, P, LastPosition, Buttons, E.Type == TEXT("move") ? FKey() : EKeys::LeftMouseButton, 0, FModifierKeysState());
		if (E.Type == TEXT("move")) App.ProcessMouseMoveEvent(Event, false);
		else if (E.Type == TEXT("down"))
		{
			const auto Window = App.FindWidgetWindow(V.ToSharedRef());
			App.ProcessMouseButtonDownEvent(Window ? Window->GetNativeWindow() : nullptr, Event);
		}
		else App.ProcessMouseButtonUpEvent(Event);
		LastPosition = P;
	}
	void Advance(double ElapsedMs)
	{
		while (Next < Playback.Num() && Playback[Next].Ms <= ElapsedMs) Dispatch(Playback[Next++]);
	}
	bool IsDone() { return Next >= Playback.Num(); }
	void EndReplay()
	{
		const auto HeldKeys = ReplayKeys;
		for (const auto& Pair : HeldKeys)
		{
			FEvent Release{0., FVector2D::ZeroVector, Pair.Value ? TEXT("key_game_up") : TEXT("key_up")};
			Release.Key = Pair.Key; Dispatch(Release);
		}
		if (bReplayGameHeld) GameButton(false);
		if (bHeld && FSlateApplication::IsInitialized())
		{
			TGuardValue<bool> Guard(bDispatching, true); bHeld = false;
			FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, LastPosition, LastPosition, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
		}
	}
}
