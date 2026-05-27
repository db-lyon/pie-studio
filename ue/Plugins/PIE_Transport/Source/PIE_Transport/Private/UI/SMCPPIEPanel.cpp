#include "SMCPPIEPanel.h"
#include "PIE_TransportModule.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIEInputReplayer.h"
#include "PIE/PIEObserver.h"
#include "PIE/MCPObservationProfile.h"
#include "PIE/PIESequenceFormat.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SavePackage.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Misc/Paths.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

const FName SMCPPIEPanel::TabId(TEXT("MCPPIEPanel"));

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(PIETransportStyleSet->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

static TSharedPtr<FSlateStyleSet> PIETransportStyleSet;

static void RegisterPIETransportStyle()
{
	if (PIETransportStyleSet.IsValid()) return;

	FString ResourcesDir = FPaths::Combine(
		FPaths::ProjectPluginsDir(), TEXT("PIE_Transport"), TEXT("Resources"));

	PIETransportStyleSet = MakeShareable(new FSlateStyleSet("PIETransportStyle"));
	PIETransportStyleSet->SetContentRoot(ResourcesDir);
	PIETransportStyleSet->Set("PIETransport.Record", new IMAGE_BRUSH("Record_Icon40x", CoreStyleConstants::Icon16x16));
	PIETransportStyleSet->Set("PIETransport.RecordPlay", new IMAGE_BRUSH("RecordPlay_Icon40x", CoreStyleConstants::Icon16x16));
	PIETransportStyleSet->Set("PIETransport.Play", new IMAGE_BRUSH("Play_Icon40x", CoreStyleConstants::Icon16x16));
	FSlateStyleRegistry::RegisterSlateStyle(*PIETransportStyleSet);
}

static void UnregisterPIETransportStyle()
{
	if (PIETransportStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*PIETransportStyleSet);
		PIETransportStyleSet.Reset();
	}
}

#undef IMAGE_BRUSH

namespace
{
	FString RecorderStateStr(UEMCPPIE::ERecorderState S)
	{
		switch (S)
		{
		case UEMCPPIE::ERecorderState::Idle:           return TEXT("Idle");
		case UEMCPPIE::ERecorderState::Armed:          return TEXT("Armed");
		case UEMCPPIE::ERecorderState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::ERecorderState::Recording:      return TEXT("Recording");
		}
		return TEXT("?");
	}

	FString ReplayerStateStr(UEMCPPIE::EReplayerState S)
	{
		switch (S)
		{
		case UEMCPPIE::EReplayerState::Idle:           return TEXT("Idle");
		case UEMCPPIE::EReplayerState::Armed:          return TEXT("Armed");
		case UEMCPPIE::EReplayerState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::EReplayerState::Replaying:      return TEXT("Replaying");
		case UEMCPPIE::EReplayerState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FString ObserverStateStr(UEMCPPIE::EObserverState S)
	{
		switch (S)
		{
		case UEMCPPIE::EObserverState::Idle:           return TEXT("Idle");
		case UEMCPPIE::EObserverState::Armed:          return TEXT("Armed");
		case UEMCPPIE::EObserverState::WaitingForPawn: return TEXT("Waiting for Pawn");
		case UEMCPPIE::EObserverState::Observing:      return TEXT("Observing");
		case UEMCPPIE::EObserverState::Completed:      return TEXT("Completed");
		}
		return TEXT("?");
	}

	FSlateColor StateColor(bool bActive)
	{
		return bActive ? FSlateColor(FLinearColor::Green) : FSlateColor(FSlateColor::UseForeground());
	}
}

void SMCPPIEPanel::RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(NomadTab)
				.Label(FText::FromString(TEXT("MCP PIE")))
				[
					SNew(SMCPPIEPanel)
				];
		}))
		.SetDisplayName(FText::FromString(TEXT("MCP PIE")))
		.SetTooltipText(FText::FromString(TEXT("PIE Record / Replay / Observe")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void SMCPPIEPanel::UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void SMCPPIEPanel::OpenTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

TSharedPtr<FExtender> SMCPPIEPanel::ToolbarExtender;

void SMCPPIEPanel::RegisterToolbarButton()
{
	RegisterPIETransportStyle();

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("PIETransport");

	Section.AddDynamicEntry("PIETransportActions", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		{
			FToolMenuEntry Entry =
				FToolMenuEntry::InitToolBarButton(
					"Record",
					FExecuteAction::CreateLambda([]()
					{
						UEMCPPIE::FRecorderArmConfig Cfg;
						FString Err, Msg;
						UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("Arm MCP recorder (waits for PIE start)")),
					FSlateIcon("PIETransportStyle", "PIETransport.Record"));
			Entry.StyleNameOverride = FName("Toolbar.BackplateLeft");
			InSection.AddEntry(Entry);
		}

		{
			FToolMenuEntry Entry =
				FToolMenuEntry::InitToolBarButton(
					"RecordPlay",
					FExecuteAction::CreateLambda([]()
					{
						UEMCPPIE::FRecorderArmConfig Cfg;
						FString Err, Msg;
						UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
						if (GEditor && !GEditor->PlayWorld)
						{
							FRequestPlaySessionParams P;
							GEditor->RequestPlaySession(P);
						}
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("Arm MCP recorder and start PIE")),
					FSlateIcon("PIETransportStyle", "PIETransport.RecordPlay"));
			Entry.StyleNameOverride = FName("Toolbar.BackplateCenter");
			InSection.AddEntry(Entry);
		}

		{
			FToolMenuEntry ComboEntry =
				FToolMenuEntry::InitComboButton(
					"PIETransportMenu",
					FUIAction(),
					FNewMenuDelegate::CreateLambda([](FMenuBuilder& Menu)
					{
						Menu.BeginSection("Recording", FText::FromString(TEXT("Recording")));

						Menu.AddMenuEntry(
							FText::FromString(TEXT("Record + Play")),
							FText::FromString(TEXT("Arm MCP recorder and start PIE")),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Recording"),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								UEMCPPIE::FRecorderArmConfig Cfg;
								FString Err, Msg;
								UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
								if (GEditor && !GEditor->PlayWorld)
								{
									FRequestPlaySessionParams P;
									GEditor->RequestPlaySession(P);
								}
							}))
						);

						Menu.AddMenuEntry(
							FText::FromString(TEXT("Arm Recorder")),
							FText::FromString(TEXT("Arm MCP recorder (waits for PIE start)")),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Recording"),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								UEMCPPIE::FRecorderArmConfig Cfg;
								FString Err, Msg;
								UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
							}))
						);

						const auto RecState = UEMCPPIE::FPIEInputRecorder::Get().GetStatus().State;

						if (RecState == UEMCPPIE::ERecorderState::Armed || RecState == UEMCPPIE::ERecorderState::WaitingForPawn)
						{
							Menu.AddMenuEntry(
								FText::FromString(TEXT("Disarm Recorder")),
								FText::FromString(TEXT("Disarm MCP recorder")),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									FString Err;
									UEMCPPIE::FPIEInputRecorder::Get().Disarm(Err);
								}))
							);
						}

						if (RecState == UEMCPPIE::ERecorderState::Recording)
						{
							Menu.AddMenuEntry(
								FText::FromString(TEXT("Stop Recording")),
								FText::FromString(TEXT("Force stop MCP recording")),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									UEMCPPIE::FPIEInputRecorder::Get().ForceStop();
								}))
							);
						}

						Menu.EndSection();

						Menu.BeginSection("Panel", FText::FromString(TEXT("Panel")));
						Menu.AddMenuEntry(
							FText::FromString(TEXT("Open PIE Transport Panel")),
							FText::FromString(TEXT("Open the full PIE Transport control panel")),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								SMCPPIEPanel::OpenTab();
							}))
						);
						Menu.EndSection();
					}),
					FText::GetEmpty(),
					FText::FromString(TEXT("PIE Transport Options")));
			ComboEntry.StyleNameOverride = FName("Toolbar.BackplateRightCombo");
			InSection.AddEntry(ComboEntry);
		}
	}));
}

void SMCPPIEPanel::UnregisterToolbarButton()
{
	UnregisterPIETransportStyle();
}

void SMCPPIEPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(8)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[ BuildRecorderSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildTimeScaleSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildRecordingsSection() ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
			[ BuildProfilesSection() ]
		]
	];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildRecorderSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Recorder")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(RecorderStateText, STextBlock)
				.Text(FText::FromString(TEXT("Idle")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Record + Play")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					if (GEditor && !GEditor->PlayWorld)
					{
						FRequestPlaySessionParams P; GEditor->RequestPlaySession(P);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Arm")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FRecorderArmConfig Cfg;
					FString Err, Msg;
					UEMCPPIE::FPIEInputRecorder::Get().Arm(Cfg, Err, Msg);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Disarm")))
				.OnClicked_Lambda([]()
				{
					FString Err;
					UEMCPPIE::FPIEInputRecorder::Get().Disarm(Err);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Stop")))
				.OnClicked_Lambda([]()
				{
					UEMCPPIE::FPIEInputRecorder::Get().ForceStop();
					return FReply::Handled();
				})
			]
		];
}


void SMCPPIEPanel::ApplyTimeScale(float Scale)
{
	CurrentTimeScale = Scale;
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (World)
	{
		AWorldSettings* WS = World->GetWorldSettings();
		if (WS)
		{
			WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, 1000.f);
			WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);
			UGameplayStatics::SetGlobalTimeDilation(World, Scale);
		}
	}
	if (TimeScaleText.IsValid())
	{
		TimeScaleText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Scale * 100.f)));
	}
}

TSharedRef<SWidget> SMCPPIEPanel::BuildTimeScaleSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Time Scale")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SAssignNew(TimeScaleText, STextBlock)
				.Text(FText::FromString(TEXT("100%")))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SSlider)
			.MinValue(0.f)
			.MaxValue(1.f)
			.Value(0.5f)
			.OnValueChanged_Lambda([this](float Val)
			{
				float Scale;
				if (Val <= 0.5f)
				{
					Scale = FMath::Lerp(0.01f, 1.0f, Val * 2.0f);
				}
				else
				{
					Scale = FMath::Lerp(1.0f, 4.0f, (Val - 0.5f) * 2.0f);
				}
				ApplyTimeScale(Scale);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("1%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.01f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("10%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.1f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("25%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.25f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("50%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(0.5f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("100%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(1.0f); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("200%")))
				.OnClicked_Lambda([this]() { ApplyTimeScale(2.0f); return FReply::Handled(); })
			]
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildRecordingsSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Recordings")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Refresh")))
				.OnClicked_Lambda([this]()
				{
					RefreshRecordings();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(RecordingsListBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SMCPPIEPanel::BuildProfilesSection()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::FromString(TEXT("Observation Profiles")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Create")))
				.OnClicked_Lambda([this]()
				{
					FString PackagePath = TEXT("/Game/PIETransport");
					FString AssetName = FString::Printf(TEXT("OP_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
					FString FullPath = PackagePath / AssetName;

					UPackage* Pkg = CreatePackage(*FullPath);
					UMCPObservationProfile* NewProfile = NewObject<UMCPObservationProfile>(Pkg, *AssetName, RF_Public | RF_Standalone);
					FAssetRegistryModule::AssetCreated(NewProfile);
					NewProfile->MarkPackageDirty();

					FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
					UPackage::SavePackage(Pkg, NewProfile, *PackageFilename, FSavePackageArgs());

					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewProfile);
					RefreshProfiles();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Refresh")))
				.OnClicked_Lambda([this]()
				{
					RefreshProfiles();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(ProfilesListBox, SVerticalBox)
		];
}

void SMCPPIEPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Update state labels
	{
		const auto RS = UEMCPPIE::FPIEInputRecorder::Get().GetStatus();
		const FString RecText = FString::Printf(TEXT("%s  %s  F:%d  %.1fs"),
			*RecorderStateStr(RS.State), *RS.Id, RS.CurrentFrame, RS.ElapsedSeconds);
		const bool bRecActive = RS.State == UEMCPPIE::ERecorderState::Recording;
		RecorderStateText->SetText(FText::FromString(RecText));
		RecorderStateText->SetColorAndOpacity(StateColor(bRecActive));
	}

	// Re-apply time scale if PIE is running and dilation doesn't match
	if (CurrentTimeScale != 1.0f)
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (World)
		{
			AWorldSettings* WS = World->GetWorldSettings();
			if (WS && !FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), CurrentTimeScale, 0.001f))
			{
				ApplyTimeScale(CurrentTimeScale);
			}
		}
	}

	// Auto-refresh lists every 5 seconds
	if (InCurrentTime - LastRecordingsRefresh > 5.0)
	{
		RefreshRecordings();
		LastRecordingsRefresh = InCurrentTime;
	}
	if (InCurrentTime - LastProfilesRefresh > 5.0)
	{
		RefreshProfiles();
		LastProfilesRefresh = InCurrentTime;
	}
}

void SMCPPIEPanel::RefreshRecordings()
{
	if (!RecordingsListBox.IsValid()) return;
	RecordingsListBox->ClearChildren();
	CachedRecordingIds.Reset();

	const FString Root = UEMCPPIE::DefaultRecordingsRoot();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
	Dirs.Sort([](const FString& A, const FString& B) { return A > B; });
	if (Dirs.Num() > 50) Dirs.SetNum(50);

	for (const FString& D : Dirs)
	{
		CachedRecordingIds.Add(D);
		const FString Id = D;

		RecordingsListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Id))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ContentPadding(FMargin(2))
				.ToolTipText(FText::FromString(TEXT("Replay")))
				.OnClicked_Lambda([this, Id]()
				{
					UEMCPPIE::FReplayerArmConfig Cfg;
					Cfg.SourceRecordingId = Id;
					Cfg.CaptureFrameEvery = 2;
					FString Err, Msg;
					UEMCPPIE::FPIEInputReplayer::Get().Arm(Cfg, Err, Msg);
					for (const FString& ProfilePath : ActiveProfilePaths)
					{
						UEMCPPIE::FObserverArmConfig OCfg;
						OCfg.ProfilePath = ProfilePath;
						FString OErr, OMsg;
						UEMCPPIE::FPIEObserver::Get().Arm(OCfg, OErr, OMsg);
					}
					if (GEditor && !GEditor->PlayWorld)
					{
						FRequestPlaySessionParams P;
						GEditor->RequestPlaySession(P);
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FSlateStyleRegistry::FindSlateStyle("PIETransportStyle")->GetBrush("PIETransport.Play"))
					.DesiredSizeOverride(FVector2D(16.f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Delete")))
				.OnClicked_Lambda([this, Id]()
				{
					const FString RecPath = UEMCPPIE::DefaultRecordingsRoot() / Id;
					IFileManager::Get().DeleteDirectory(*RecPath, false, true);
					RefreshRecordings();
					return FReply::Handled();
				})
			]
		];
	}

	if (Dirs.Num() == 0)
	{
		RecordingsListBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No recordings found")))
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
	}
}

void SMCPPIEPanel::RefreshProfiles()
{
	if (!ProfilesListBox.IsValid()) return;
	ProfilesListBox->ClearChildren();
	CachedProfilePaths.Reset();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPObservationProfile::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& A : Assets)
	{
		const FString Path = A.GetObjectPathString();
		CachedProfilePaths.Add(Path);
		const FString Name = A.AssetName.ToString();
		const bool bActive = ActiveProfilePaths.Contains(Path);

		ProfilesListBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SCheckBox)
				.IsChecked(bActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this, Path](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
						ActiveProfilePaths.Add(Path);
					else
						ActiveProfilePaths.Remove(Path);
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Edit")))
				.OnClicked_Lambda([Path]()
				{
					UObject* Asset = LoadObject<UMCPObservationProfile>(nullptr, *Path);
					if (Asset)
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Delete")))
				.OnClicked_Lambda([this, Path]()
				{
					UEditorAssetLibrary::DeleteAsset(Path);
					ActiveProfilePaths.Remove(Path);
					RefreshProfiles();
					return FReply::Handled();
				})
			]
		];
	}

	if (Assets.Num() == 0)
	{
		ProfilesListBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No profiles found")))
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
	}
}
