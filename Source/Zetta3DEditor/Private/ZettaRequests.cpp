// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaRequests.h"

#include "ZettaCatalog.h"
#include "ZettaJson.h"

namespace ZettaRequests
{
	static bool MayChooseCount(const FZettaEntitlements3D* Gates)
	{
		return Gates == nullptr || Gates->Allows(Gates->AllowsCustomFaceCount);
	}

	static bool CustomCount(const FZettaGenerateOptions& O, const FZettaEntitlements3D* Gates)
	{
		return MayChooseCount(Gates) && O.FaceCount.IsSet()
			&& O.FaceCount.GetValue() != ZettaCatalog::FacePreset(O.Model, O.Quality, O.PolygonType == TEXT("quadrilateral"));
	}

	void Spec(const FZettaGenerateOptions& O, const FZettaEntitlements3D* Gates, FString& OutBase, TArray<FString>& OutModifiers)
	{
		const ZettaCatalog::FModelInfo& M = ZettaCatalog::Model(O.Model);
		OutBase = O.Quality == TEXT("low-poly") && M.bLowPoly ? TEXT("lowpoly") : O.Output == TEXT("geometry") ? TEXT("geometry_only") : TEXT("textured");
		OutModifiers.Reset();
		if (O.IsMultiView()) OutModifiers.Add(TEXT("multi_view"));
		if (O.bPbr && O.Output == TEXT("textured")) OutModifiers.Add(TEXT("pbr"));
		if (O.Quality == TEXT("ultra") && M.bDetailedGeometry) OutModifiers.Add(TEXT("detailed_geometry"));
		if (O.bSegmentParts && O.Output == TEXT("geometry") && O.Quality != TEXT("low-poly") && M.bParts) OutModifiers.Add(TEXT("generate_parts"));
		if (O.Output == TEXT("textured"))
		{
			if (O.TextureQuality == TEXT("extreme") && M.bExtremeTexture) OutModifiers.Add(TEXT("extreme_texture"));
			else if (O.TextureQuality == TEXT("detailed") && M.bDetailedTexture) OutModifiers.Add(TEXT("detailed_texture"));
		}
		if (CustomCount(O, Gates)) OutModifiers.Add(TEXT("custom_face_count"));
		// No quad_mesh modifier: the web Studio never sends it; the request's polygonType selects quads.
	}

	FZettaBuiltRequest Build(const FZettaGenerateOptions& O, const FZettaEntitlements3D* Gates)
	{
		FZettaBuiltRequest R;
		const ZettaCatalog::FModelInfo& M = ZettaCatalog::Model(O.Model);
		Spec(O, Gates, R.Base, R.Modifiers);
		const bool bQuads = O.PolygonType == TEXT("quadrilateral");

		if (O.FaceCount.IsSet() && MayChooseCount(Gates))
		{
			const FInt32Range Range = ZettaCatalog::FaceRange(O.Model, O.Quality, bQuads);
			if (!Range.Contains(O.FaceCount.GetValue()))
			{
				R.Problems.Add(FString::Printf(TEXT("The polygon count must be between %s and %s."),
					*FText::AsNumber(Range.GetLowerBoundValue()).ToString(), *FText::AsNumber(Range.GetUpperBoundValue()).ToString()));
			}
		}

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		{
			TSharedRef<FJsonObject> Generation = MakeShared<FJsonObject>();
			Generation->SetStringField(TEXT("base"), R.Base);
			Generation->SetField(TEXT("modifiers"), ZettaJson::StringArray(R.Modifiers));
			TSharedRef<FJsonObject> SpecObj = MakeShared<FJsonObject>();
			SpecObj->SetObjectField(TEXT("generation"), Generation);
			Body->SetObjectField(TEXT("spec"), SpecObj);
		}

		const FString Prompt = O.Prompt.TrimStartAndEnd();
		const FString Image = O.ImageUrl.TrimStartAndEnd();
		if (O.IsMultiView())
		{
			TArray<TSharedPtr<FJsonValue>> Views;
			for (const TPair<FString, FString>& V : O.Views)
			{
				TSharedRef<FJsonObject> View = MakeShared<FJsonObject>();
				View->SetStringField(TEXT("viewType"), V.Key);
				View->SetStringField(TEXT("imageUrl"), V.Value);
				Views.Add(MakeShared<FJsonValueObject>(View));
			}
			Body->SetArrayField(TEXT("multiViewImages"), Views);
		}
		else
		{
			const bool bHasImage = !Image.IsEmpty();
			const bool bHasPrompt = !Prompt.IsEmpty();
			if (bHasImage && bHasPrompt && !O.bImageFromPrompt) R.Problems.Add(TEXT("Use either a prompt or an image, not both."));
			if (!bHasImage && !bHasPrompt) R.Problems.Add(TEXT("Describe the model or attach an image."));
			if (!bHasImage && bHasPrompt && !M.bBarePrompt) R.Problems.Add(M.Label + TEXT(" generates from a picture — draw one from the prompt first."));
			if (bHasPrompt && Prompt.Len() > 1024) R.Problems.Add(TEXT("The prompt is longer than 1,024 characters."));
			if (bHasImage && O.bImageFromPrompt && bHasPrompt)
			{
				Body->SetStringField(TEXT("imageUrl"), Image);
				Body->SetStringField(TEXT("prompt"), Prompt);
				Body->SetBoolField(TEXT("imageFromPrompt"), true);
			}
			else if (bHasImage) Body->SetStringField(TEXT("imageUrl"), Image);
			else Body->SetStringField(TEXT("prompt"), Prompt);
		}

		if (O.FaceCount.IsSet() && MayChooseCount(Gates)) Body->SetNumberField(TEXT("faceCount"), O.FaceCount.GetValue());

		const bool bParts = R.Modifiers.Contains(TEXT("generate_parts"));
		const bool bQuadCapable = (R.Base == TEXT("lowpoly") || M.bQuadAnyBase) && !bParts;
		if (bQuads && bQuadCapable) Body->SetStringField(TEXT("polygonType"), TEXT("quadrilateral"));
		else if (bQuadCapable) Body->SetStringField(TEXT("polygonType"), TEXT("triangle"));
		if (R.Base == TEXT("lowpoly") && O.Output == TEXT("geometry") && O.Model == TEXT("n25")) Body->SetStringField(TEXT("generationType"), TEXT("Geometry"));
		if (!O.Availability.IsEmpty()) Body->SetStringField(TEXT("availability"), O.Availability);
		if (M.bImageTuning)
		{
			if (O.bRealWorldScale) Body->SetBoolField(TEXT("autoSize"), true);
			if (O.TextureAlignment != TEXT("original_image")) Body->SetStringField(TEXT("textureAlignment"), O.TextureAlignment);
			if (O.bAutofixImage) Body->SetBoolField(TEXT("autofixImage"), true);
			if (O.bCompressGeometry) Body->SetBoolField(TEXT("compressGeometry"), true);
			if (O.bDelight) Body->SetBoolField(TEXT("delight"), true);
			// Inverted on purpose: the engine defaults UVs on, so only "off" is sent.
			if (!O.bExportUv) Body->SetBoolField(TEXT("exportUv"), false);
		}

		const FString Title = O.Title.TrimStartAndEnd();
		if (!Title.IsEmpty()) Body->SetStringField(TEXT("title"), Title.Left(255));
		if (O.bControlMode && !O.IsMultiView()) Body->SetBoolField(TEXT("advanceControl"), true);
		Body->SetStringField(TEXT("model"), O.Model);

		R.Body = Body;
		return R;
	}

	TOptional<int32> Price(const FZettaPriceList& Prices, const FZettaBuiltRequest& Request, const FString& Model)
	{
		TArray<FString> Codes = { Request.Base };
		Codes.Append(Request.Modifiers);
		return Prices.Total(Model, Codes);
	}

	TSharedRef<FJsonObject> PostProcess(const FString& Code, int64 SourcePostId, const TSharedPtr<FJsonObject>& Options)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> SpecObj = MakeShared<FJsonObject>();
		SpecObj->SetField(TEXT("postProcess"), ZettaJson::StringArray({ Code }));
		Body->SetObjectField(TEXT("spec"), SpecObj);
		Body->SetNumberField(TEXT("sourcePostId"), (double)SourcePostId);
		if (Options.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Options->Values)
			{
				if (Pair.Value.IsValid() && Pair.Value->Type != EJson::Null) Body->SetField(Pair.Key, Pair.Value);
			}
		}
		return Body;
	}
}
