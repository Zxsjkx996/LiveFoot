#include "LiveFootEditorModule.h"
#include "SLiveFootAnalysisWindow.h"
#include "IAnimationEditorModule.h"
#include "IPersonaToolkit.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

const FName FLiveFootEditorModule::TabName(TEXT("LiveFootAnalysis"));
IMPLEMENT_MODULE(FLiveFootEditorModule, LiveFootEditor)

void FLiveFootEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
		FOnSpawnTab::CreateRaw(this, &FLiveFootEditorModule::SpawnTab))
		.SetDisplayName(FText::FromString(TEXT("LiveFoot Analysis"))).SetMenuType(ETabSpawnerMenuType::Hidden);
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLiveFootEditorModule::RegisterMenus));
	auto& Extenders = FModuleManager::LoadModuleChecked<IAnimationEditorModule>(TEXT("AnimationEditor")).GetAllAnimationEditorToolbarExtenders();
	auto ExtenderDelegate = IAnimationEditorModule::FAnimationEditorToolbarExtender::CreateLambda(
		[](const TSharedRef<FUICommandList> Commands, TSharedRef<IAnimationEditor> Editor)
		{
			TWeakPtr<IAnimationEditor> WeakEditor = Editor;
			auto Extender = MakeShared<FExtender>();
			Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, Commands,
				FToolBarExtensionDelegate::CreateLambda([WeakEditor](FToolBarBuilder& Builder)
				{
					Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([WeakEditor]
					{
						if (auto Pinned = WeakEditor.Pin())
						{
							if (auto* Module = FModuleManager::GetModulePtr<FLiveFootEditorModule>(TEXT("LiveFootEditor")))
							{ Module->OpenForSequence(Cast<UAnimSequence>(Pinned->GetPersonaToolkit()->GetAnimationAsset())); }
						}
					})), NAME_None, FText::FromString(TEXT("LiveFoot")),
					FText::FromString(TEXT("Analyze the current Animation Sequence in LiveFoot")),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")));
				}));
			return Extender;
		});
	AnimationToolbarHandle = ExtenderDelegate.GetHandle();
	Extenders.Add(ExtenderDelegate);
}

void FLiveFootEditorModule::ShutdownModule()
{
	if (auto Pinned = Window.Pin()) { Pinned->Cancel(); }
	if (auto Pinned = Tab.Pin()) { Pinned->RequestCloseTab(); }
	Window.Reset(); Tab.Reset();
	if (auto* Module = FModuleManager::GetModulePtr<IAnimationEditorModule>(TEXT("AnimationEditor")))
	{
		Module->GetAllAnimationEditorToolbarExtenders().RemoveAll([this](const auto& Delegate)
		{ return Delegate.GetHandle() == AnimationToolbarHandle; });
	}
	if (UToolMenus::TryGet()) { UToolMenus::UnRegisterStartupCallback(this); UToolMenus::UnregisterOwner(this); }
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

void FLiveFootEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	auto& Section = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->FindOrAddSection(TEXT("LiveFoot"));
	Section.AddMenuEntry(TEXT("LiveFootAnalysis"), FText::FromString(TEXT("LiveFoot Analysis")),
		FText::FromString(TEXT("Open LiveFoot animation analysis")), FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
		FUIAction(FExecuteAction::CreateRaw(this, &FLiveFootEditorModule::OpenTab)));
}

TSharedRef<SDockTab> FLiveFootEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
	auto Widget = SNew(SLiveFootAnalysisWindow);
	Window = Widget;
	auto NewTab = SNew(SDockTab).TabRole(ETabRole::NomadTab)[Widget];
	NewTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak = TWeakPtr<SLiveFootAnalysisWindow>(Widget)](TSharedRef<SDockTab>)
	{ if (auto Pinned = Weak.Pin()) { Pinned->Cancel(); } }));
	Tab = NewTab;
	return NewTab;
}
void FLiveFootEditorModule::OpenTab() { FGlobalTabmanager::Get()->TryInvokeTab(TabName); }
void FLiveFootEditorModule::OpenForSequence(UAnimSequence* Sequence)
{
	OpenTab();
	if (Sequence) { if (auto Pinned = Window.Pin()) { Pinned->AnalyzeAssets({FSoftObjectPath(Sequence)}); } }
}
