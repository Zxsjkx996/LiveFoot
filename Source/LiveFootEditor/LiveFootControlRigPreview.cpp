#include "LiveFootControlRigPreview.h"

#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

FLiveFootControlRigCapability LiveFootControlRigPreview::Inspect()
{
	FLiveFootControlRigCapability Capability;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ControlRig"));
	Capability.bPluginFound = Plugin.IsValid();
	Capability.bPluginEnabled = Capability.bPluginFound && Plugin->IsEnabled();
	Capability.bRuntimeModuleAvailable = FModuleManager::Get().ModuleExists(TEXT("ControlRig"));
	Capability.bEditorModuleAvailable = FModuleManager::Get().ModuleExists(TEXT("ControlRigEditor"));
	Capability.bEditorModuleLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("ControlRigEditor"));

	if (!Capability.bPluginFound)
	{
		Capability.Message = TEXT("Control Rig plugin was not found; static target preview only.");
	}
	else if (!Capability.bPluginEnabled)
	{
		Capability.Message = TEXT("Control Rig plugin is disabled; static target preview only.");
	}
	else if (!Capability.bRuntimeModuleAvailable || !Capability.bEditorModuleAvailable)
	{
		Capability.Message = TEXT("Control Rig modules are unavailable in this target; static target preview only.");
	}
	else if (!Capability.bEditorModuleLoaded)
	{
		Capability.Message = TEXT("Control Rig editor module is available but not loaded; mapping is unverified, static target preview only.");
	}
	else
	{
		Capability.Message = TEXT("Control Rig editor module is loaded; foot control mapping is still unverified, static target preview only.");
	}
	return Capability;
}
