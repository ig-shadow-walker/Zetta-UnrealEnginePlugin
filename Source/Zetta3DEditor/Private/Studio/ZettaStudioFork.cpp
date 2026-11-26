// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudioFork.h"

#include "ZettaCatalog.h"
#include "ZettaJson.h"

namespace ZettaStudioFork
{
	static const TCHAR* Slots[] = { TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right") };

	static bool WayInOf(const FString& Feature, EWayIn& Out)
	{
		if (Feature == TEXT("txt23d")) Out = EWayIn::Text;
		else if (Feature == TEXT("img23d")) Out = EWayIn::Image;
		else if (Feature == TEXT("multiView23d")) Out = EWayIn::MultiView;
		else return false;
		return true;
	}

	FName ToolFor(const FString& Model)
	{
		if (Model.IsEmpty()) return NAME_None;
		if (ZettaCatalog::GenerateModels().Contains(Model)) return TEXT("Generate");
		if (ZettaCatalog::SmartModels().Contains(Model)) return TEXT("Smart");
		return NAME_None;
	}

	bool CanFork(const FZettaGeneration& G)
	{
		EWayIn WayIn;
		return G.Availability != TEXT("deleted") && WayInOf(G.Feature, WayIn) && !ToolFor(G.Model).IsNone();
	}

	FSettings SettingsOf(const FZettaGeneration& G)
	{
		const FString Base = G.SpecBase();
		const TSet<FString> Mods = G.SpecModifiers();
		FSettings S;
		S.Model = G.Model;
		S.Quality = Base == TEXT("lowpoly") ? TEXT("low-poly") : Mods.Contains(TEXT("detailed_geometry")) ? TEXT("ultra") : TEXT("high-res");
		S.Output = Base == TEXT("geometry_only") ? TEXT("geometry") : Base == TEXT("textured") ? TEXT("textured") : FString();
		S.bPbr = Mods.Contains(TEXT("pbr"));
		S.bSegmentParts = Mods.Contains(TEXT("generate_parts"));
		if (Base == TEXT("textured"))
		{
			S.TextureQuality = Mods.Contains(TEXT("extreme_texture")) ? TEXT("extreme") : Mods.Contains(TEXT("detailed_texture")) ? TEXT("detailed") : TEXT("standard");
		}
		S.PolygonType = G.PolygonType;
		// Only a count that was chosen (and paid for) comes back; the engine's default is left to the form.
		if (Mods.Contains(TEXT("custom_face_count")) && G.FaceCount.IsSet() && G.FaceCount.GetValue() > 0 && G.FaceCount.GetValue() <= MAX_int32)
		{
			S.FaceCount = (int32)G.FaceCount.GetValue();
		}
		if (G.Feature == TEXT("txt23d") && !G.InputPrompt.TrimStartAndEnd().IsEmpty()) S.Prompt = G.InputPrompt;
		return S;
	}

	TOptional<FSetup> Of(const FZettaGeneration& G)
	{
		EWayIn WayIn;
		if (!CanFork(G) || !WayInOf(G.Feature, WayIn)) return {};
		FSetup Setup;
		Setup.FromId = G.Id;
		Setup.FromTitle = G.Title.TrimStartAndEnd().IsEmpty() ? FString::Printf(TEXT("#%lld"), G.Id) : G.Title.TrimStartAndEnd();
		Setup.Tool = ToolFor(G.Model);
		Setup.WayIn = WayIn;
		Setup.Settings = SettingsOf(G);

		// The row's input pictures, with the angle each was (the typed model keeps only their addresses).
		const TArray<TSharedPtr<FJsonObject>> Inputs = ZettaJson::Objects(G.Raw, TEXT("inputImages"));
		if (WayIn == EWayIn::MultiView)
		{
			for (const TSharedPtr<FJsonObject>& Input : Inputs)
			{
				const FString Url = ZettaJson::Str(Input, TEXT("url"));
				const FString View = ZettaJson::Str(Input, TEXT("viewType"));
				bool bKnown = false;
				for (const TCHAR* Slot : Slots) bKnown |= View == Slot;
				if (!Url.IsEmpty() && bKnown) Setup.Pictures.Add({ Url, FString::Printf(TEXT("%s – %s.png"), *Setup.FromTitle, *View), View });
			}
		}
		else if (WayIn == EWayIn::Image)
		{
			for (const FString& Url : G.InputImageUrls)
			{
				if (Url.IsEmpty()) continue;
				Setup.Pictures.Add({ Url, Setup.FromTitle + TEXT(".png"), FString() });
				break;
			}
		}
		return Setup;
	}
}
