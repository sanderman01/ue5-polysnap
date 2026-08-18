// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only half of PolySnap: validates that a static mesh honours the socket conventions,
 * at import rather than as a runtime surprise. See CONVENTIONS.md section 6.
 */
class FPolySnapEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	/** Handle for the import delegate, so shutdown can unhook cleanly. */
	FDelegateHandle PostImportHandle;
};
