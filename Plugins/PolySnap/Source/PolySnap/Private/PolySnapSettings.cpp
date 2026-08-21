// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSettings.h"

UPolySnapSettings::UPolySnapSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("PolySnap");
}

const UPolySnapSettings& UPolySnapSettings::Get()
{
	const UPolySnapSettings* Settings = GetDefault<UPolySnapSettings>();
	check(Settings != nullptr);

	return *Settings;
}

FName UPolySnapSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FPolySnapSettingsChanged& UPolySnapSettings::OnSettingsChanged()
{
	static FPolySnapSettingsChanged Delegate;

	return Delegate;
}

#if WITH_EDITOR
void UPolySnapSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Most of these settings are read where they are needed and so pick up an edit on their own.
	// The physics ones are not: they are pushed onto a body once, at BeginPlay, and a part that
	// is already simulating never looks at them again. Telling the world its settings moved is
	// what turns an afternoon of PIE restarts into a few minutes of tuning by feel.
	OnSettingsChanged().Broadcast();
}
#endif
