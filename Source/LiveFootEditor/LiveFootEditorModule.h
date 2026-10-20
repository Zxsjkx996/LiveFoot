#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SDockTab;
class SLiveFootAnalysisWindow;
class UAnimSequence;

/** Registers editor entry points; sampling, queue state and reports have separate owners. */
class FLiveFootEditorModule final : public IModuleInterface
{
public:
	static const FName TabName;
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	void OpenTab();
	void OpenForSequence(UAnimSequence* Sequence);
	TWeakPtr<SLiveFootAnalysisWindow> Window;
	TWeakPtr<SDockTab> Tab;
	FDelegateHandle AnimationToolbarHandle;
};
