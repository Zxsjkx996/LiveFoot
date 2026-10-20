#pragma once

#include "CoreMinimal.h"

struct FLiveFootControlRigCapability
{
	bool bPluginFound = false;
	bool bPluginEnabled = false;
	bool bRuntimeModuleAvailable = false;
	bool bEditorModuleAvailable = false;
	bool bEditorModuleLoaded = false;
	FString Message;
};

namespace LiveFootControlRigPreview
{
	/** Read-only capability probe. It never loads a Control Rig module or changes project/plugin state. */
	LIVEFOOTEDITOR_API FLiveFootControlRigCapability Inspect();
}
