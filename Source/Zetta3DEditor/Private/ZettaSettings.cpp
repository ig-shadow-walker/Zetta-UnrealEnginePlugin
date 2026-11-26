// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaSettings.h"

#include "Misc/Guid.h"

UZettaProjectSettings::UZettaProjectSettings()
{
	ModelsFolder.Path = TEXT("/Game/Zetta3D/Models");
	ImagesFolder.Path = TEXT("/Game/Zetta3D/Images");
}

FString UZettaProjectSettings::EnsureProjectId()
{
	if (ProjectId.Len() < 8)
	{
		ProjectId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		TryUpdateDefaultConfigFile();
	}
	return ProjectId;
}

static FString NormaliseContentPath(FString Path, const TCHAR* Fallback)
{
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Path.EndsWith(TEXT("/"))) Path.LeftChopInline(1);
	if (Path.IsEmpty()) return Fallback;
	if (!Path.StartsWith(TEXT("/"))) Path = TEXT("/Game/") + Path;
	return Path;
}

FString UZettaProjectSettings::GetModelsFolder() const
{
	return NormaliseContentPath(ModelsFolder.Path, TEXT("/Game/Zetta3D/Models"));
}

FString UZettaProjectSettings::GetImagesFolder() const
{
	return NormaliseContentPath(ImagesFolder.Path, TEXT("/Game/Zetta3D/Images"));
}
