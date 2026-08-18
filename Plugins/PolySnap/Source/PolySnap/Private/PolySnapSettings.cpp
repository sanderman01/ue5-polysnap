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
