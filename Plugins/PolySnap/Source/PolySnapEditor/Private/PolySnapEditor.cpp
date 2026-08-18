// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapEditor.h"

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PolySnap.h"
#include "PolySnapMeshValidation.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "FPolySnapEditorModule"

namespace PolySnapEditorPrivate
{
void ValidateImportedAsset(UFactory* Factory, UObject* CreatedObject)
{
	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(CreatedObject))
	{
		FPolySnapMeshValidation::ValidateAndLog(StaticMesh);
	}
}

/**
	 * Sweeps every loaded static mesh.
	 *
	 * This exists because the import hook is not a guarantee: whether Interchange broadcasts
	 * OnAssetPostImport for every path a mesh can arrive by is not something to bet an
	 * asset pipeline on, and a check that silently never runs is worse than no check.
	 */
void ValidateAllLoadedMeshes()
{
	int32 MeshCount = 0;
	int32 FailedCount = 0;

	for (TObjectIterator<UStaticMesh> It; It; ++It)
	{
		++MeshCount;
		FailedCount += FPolySnapMeshValidation::ValidateAndLog(*It) ? 0 : 1;
	}

	UE_LOG(LogPolySnap, Log, TEXT("PolySnap validated %d loaded static mesh(es); %d failed."), MeshCount, FailedCount);
}

static FAutoConsoleCommand ValidateAssetsCommand(TEXT("PolySnap.ValidateAssets"),
	TEXT("Validates every loaded static mesh against the PolySnap socket conventions."),
		FConsoleCommandDelegate::CreateStatic(&ValidateAllLoadedMeshes));
}

void FPolySnapEditorModule::StartupModule()
{
	if (GEditor != nullptr)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			PostImportHandle =
				ImportSubsystem->OnAssetPostImport.AddStatic(&PolySnapEditorPrivate::ValidateImportedAsset);
		}
	}
}

void FPolySnapEditorModule::ShutdownModule()
{
	if (PostImportHandle.IsValid() && GEditor != nullptr)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetPostImport.Remove(PostImportHandle);
		}

		PostImportHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPolySnapEditorModule, PolySnapEditor);
