// Copyright Zetta 3D. See LICENSE.md.
// Materials: create_material (a Material wired from constants and textures, or a Material Instance of a parent with
// its parameters) and assign_material (on a level actor's mesh component or on a mesh asset's slot).

#include "AssetToolsModule.h"
#include "Components/MeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "GameFramework/Actor.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "ZettaAgentTools.h"
#include "ZettaAssetUtil.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaMaterialImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	/** A colour value: "#rrggbb(aa)" (sRGB) or [r, g, b(, a)] linear 0–1. */
	bool ReadColor(const TSharedPtr<FJsonValue>& V, FLinearColor& Out)
	{
		if (!V.IsValid()) return false;
		if (V->Type == EJson::String)
		{
			FString S = V->AsString().TrimStartAndEnd();
			if (!S.StartsWith(TEXT("#"))) return false;
			S.RightChopInline(1);
			if (S.Len() != 6 && S.Len() != 8) return false;
			Out = FLinearColor(FColor::FromHex(S));
			if (S.Len() == 6) Out.A = 1.f;
			return true;
		}
		if (V->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			if (A.Num() < 3 || A.Num() > 4) return false;
			float C[4] = { 0, 0, 0, 1 };
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const TOptional<double> D = ZettaTool::ValueNumber(A[i]);
				if (!D.IsSet()) return false;
				C[i] = (float)D.GetValue();
			}
			Out = FLinearColor(C[0], C[1], C[2], C[3]);
			return true;
		}
		return false;
	}

	EMaterialSamplerType SamplerFor(const UTexture* Texture)
	{
		if (Texture->CompressionSettings == TC_Normalmap) return SAMPLERTYPE_Normal;
		if (Texture->CompressionSettings == TC_Grayscale || Texture->CompressionSettings == TC_Alpha) return Texture->SRGB ? SAMPLERTYPE_Grayscale : SAMPLERTYPE_LinearGrayscale;
		if (Texture->CompressionSettings == TC_Masks || !Texture->SRGB) return SAMPLERTYPE_LinearColor;
		return SAMPLERTYPE_Color;
	}

	/** Builds a Material's graph: a column of inputs wired to the material's pins. */
	struct FGraph
	{
		UMaterial* Material = nullptr;
		int32 Row = 0;
		TMap<UTexture*, UMaterialExpressionTextureSample*> Samples;

		FIntPoint Next(int32 Column = 1)
		{
			const FIntPoint P(-400 * Column, Row * 160);
			++Row;
			return P;
		}

		UMaterialExpression* Constant(float V)
		{
			const FIntPoint P = Next();
			UMaterialExpressionConstant* E = Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), P.X, P.Y));
			E->R = V;
			return E;
		}

		UMaterialExpression* Color(const FLinearColor& C)
		{
			const FIntPoint P = Next();
			UMaterialExpressionConstant3Vector* E = Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), P.X, P.Y));
			E->Constant = C;
			return E;
		}

		/** One sample per texture, shared when several channels read it (an ORM map). */
		UMaterialExpressionTextureSample* Sample(UTexture* Texture, bool bNormal)
		{
			if (UMaterialExpressionTextureSample** Found = Samples.Find(Texture)) return *Found;
			const FIntPoint P = Next(2);
			++Row;
			UMaterialExpressionTextureSample* E = Cast<UMaterialExpressionTextureSample>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSample::StaticClass(), P.X, P.Y));
			E->Texture = Texture;
			E->SamplerType = bNormal ? SAMPLERTYPE_Normal : SamplerFor(Texture);
			Samples.Add(Texture, E);
			return E;
		}

		UMaterialExpression* Multiply(UMaterialExpression* A, const FString& AOut, UMaterialExpression* B, const FString& BOut)
		{
			const FIntPoint P = Next();
			UMaterialExpression* E = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), P.X + 200, P.Y);
			UMaterialEditingLibrary::ConnectMaterialExpressions(A, AOut, E, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(B, BOut, E, TEXT("B"));
			return E;
		}
	};

	bool LoadTexture(const TSharedPtr<FJsonObject>& Textures, const TCHAR* Key, UTexture*& Out, TArray<FString>& Errors)
	{
		Out = nullptr;
		if (!ZettaTool::Has(Textures, Key)) return false;
		FString Path = ZettaTool::Str(Textures, Key);
		if (const TSharedPtr<FJsonObject> O = ZettaTool::Obj(Textures, Key)) Path = ZettaTool::Str(O, TEXT("asset"));
		FString Error;
		Out = Cast<UTexture>(ZettaTargets::LoadAsset(Path, Error, UTexture::StaticClass()));
		if (!Out) Errors.Add(FString::Printf(TEXT("textures.%s: %s"), Key, *Error));
		return Out != nullptr;
	}

	/** The material wired from the protocol's shorthand. Returns what was connected. */
	TArray<FString> BuildMaterial(UMaterial* M, const TSharedPtr<FJsonObject>& In, TArray<FString>& Errors)
	{
		FGraph G;
		G.Material = M;
		TArray<FString> Done;
		const TSharedPtr<FJsonObject> Textures = ZettaTool::Obj(In, TEXT("textures"));

		const FString Blend = ZettaTool::Str(In, TEXT("blend")).ToLower();
		const bool bOpacity = ZettaTool::Has(In, TEXT("opacity")) || ZettaTool::Has(Textures, TEXT("opacity"));
		if (Blend == TEXT("masked")) M->BlendMode = BLEND_Masked;
		else if (Blend == TEXT("translucent") || (Blend.IsEmpty() && bOpacity)) M->BlendMode = BLEND_Translucent;
		else if (!Blend.IsEmpty() && Blend != TEXT("opaque")) Errors.Add(TEXT("'blend' must be Opaque, Masked or Translucent."));
		if (M->BlendMode != BLEND_Opaque) Done.Add(FString::Printf(TEXT("blend %s"), M->BlendMode == BLEND_Masked ? TEXT("Masked") : TEXT("Translucent")));
		if (ZettaTool::BoolOr(In, TEXT("two_sided"), false))
		{
			M->TwoSided = true;
			Done.Add(TEXT("two-sided"));
		}

		UTexture* Orm = nullptr;
		LoadTexture(Textures, TEXT("orm"), Orm, Errors);

		// Base colour: a texture, a colour, or the texture tinted by the colour.
		{
			UTexture* Tex = nullptr;
			LoadTexture(Textures, TEXT("base_color"), Tex, Errors);
			FLinearColor C;
			const bool bColor = ZettaTool::Has(In, TEXT("base_color"));
			if (bColor && !ReadColor(ZettaTool::Get(In, TEXT("base_color")), C))
			{
				Errors.Add(TEXT("'base_color' must be \"#rrggbb\" or [r, g, b]."));
			}
			else if (Tex && bColor)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Multiply(G.Sample(Tex, false), TEXT("RGB"), G.Color(C), TEXT("")), TEXT(""), MP_BaseColor);
				Done.Add(TEXT("base colour = texture × tint"));
			}
			else if (Tex)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Sample(Tex, false), TEXT("RGB"), MP_BaseColor);
				Done.Add(TEXT("base colour = ") + Tex->GetName());
			}
			else if (bColor)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Color(C), TEXT(""), MP_BaseColor);
				Done.Add(TEXT("base colour = ") + ZettaTool::Str(In, TEXT("base_color")));
			}
		}

		// Scalars that may also come from their own map or the ORM map's channel.
		struct FScalar
		{
			const TCHAR* Key;
			EMaterialProperty Property;
			const TCHAR* OrmChannel;
		};
		const FScalar Scalars[] = {
			{ TEXT("metallic"), MP_Metallic, TEXT("B") },
			{ TEXT("roughness"), MP_Roughness, TEXT("G") },
			{ TEXT("specular"), MP_Specular, nullptr },
			{ TEXT("ambient_occlusion"), MP_AmbientOcclusion, TEXT("R") },
		};
		for (const FScalar& S : Scalars)
		{
			UTexture* Tex = nullptr;
			LoadTexture(Textures, S.Key, Tex, Errors);
			if (Tex)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Sample(Tex, false), TEXT("R"), S.Property);
				Done.Add(FString::Printf(TEXT("%s = %s"), S.Key, *Tex->GetName()));
			}
			else if (Orm && S.OrmChannel)
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Sample(Orm, false), S.OrmChannel, S.Property);
				Done.Add(FString::Printf(TEXT("%s = %s.%s"), S.Key, *Orm->GetName(), S.OrmChannel));
			}
			else if (ZettaTool::Has(In, S.Key))
			{
				const TOptional<double> V = ZettaTool::Num(In, S.Key);
				if (!V.IsSet())
				{
					Errors.Add(FString::Printf(TEXT("'%s' must be a number (0–1)."), S.Key));
					continue;
				}
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Constant((float)V.GetValue()), TEXT(""), S.Property);
				Done.Add(FString::Printf(TEXT("%s = %s"), S.Key, *ZettaTool::Num(V.GetValue())));
			}
		}

		{
			UTexture* Normal = nullptr;
			if (LoadTexture(Textures, TEXT("normal"), Normal, Errors))
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Sample(Normal, true), TEXT("RGB"), MP_Normal);
				Done.Add(TEXT("normal = ") + Normal->GetName());
			}
		}

		// Emissive: a colour or a map, times the strength.
		{
			UTexture* Tex = nullptr;
			LoadTexture(Textures, TEXT("emissive"), Tex, Errors);
			FLinearColor C = FLinearColor::White;
			const bool bColor = ZettaTool::Has(In, TEXT("emissive"));
			if (bColor && !ReadColor(ZettaTool::Get(In, TEXT("emissive")), C)) Errors.Add(TEXT("'emissive' must be \"#rrggbb\" or [r, g, b]."));
			const float Strength = (float)ZettaTool::Num(In, TEXT("emissive_strength")).Get(1.0);
			if (Tex || bColor || ZettaTool::Has(In, TEXT("emissive_strength")))
			{
				UMaterialExpression* Source = nullptr;
				FString Out;
				if (Tex && bColor)
				{
					Source = G.Multiply(G.Sample(Tex, false), TEXT("RGB"), G.Color(C), TEXT(""));
				}
				else if (Tex)
				{
					Source = G.Sample(Tex, false);
					Out = TEXT("RGB");
				}
				else
				{
					Source = G.Color(C);
				}
				if (!FMath::IsNearlyEqual(Strength, 1.f))
				{
					Source = G.Multiply(Source, Out, G.Constant(Strength), TEXT(""));
					Out.Reset();
				}
				UMaterialEditingLibrary::ConnectMaterialProperty(Source, Out, MP_EmissiveColor);
				Done.Add(FString::Printf(TEXT("emissive%s × %s"), Tex ? *(TEXT(" ") + Tex->GetName()) : TEXT(""), *ZettaTool::Num(Strength)));
			}
		}

		// Opacity goes to Opacity (translucent) or Opacity Mask (masked).
		if (bOpacity)
		{
			const EMaterialProperty Pin = M->BlendMode == BLEND_Masked ? MP_OpacityMask : MP_Opacity;
			UTexture* Tex = nullptr;
			if (LoadTexture(Textures, TEXT("opacity"), Tex, Errors))
			{
				UMaterialEditingLibrary::ConnectMaterialProperty(G.Sample(Tex, false), TEXT("R"), Pin);
				Done.Add(TEXT("opacity = ") + Tex->GetName());
			}
			else if (ZettaTool::Has(In, TEXT("opacity")))
			{
				const TOptional<double> V = ZettaTool::Num(In, TEXT("opacity"));
				if (V.IsSet())
				{
					UMaterialEditingLibrary::ConnectMaterialProperty(G.Constant((float)V.GetValue()), TEXT(""), Pin);
					Done.Add(TEXT("opacity = ") + ZettaTool::Num(V.GetValue()));
				}
				else
				{
					Errors.Add(TEXT("'opacity' must be a number (0–1)."));
				}
			}
		}
		return Done;
	}

	// ─── Material Instances ──────────────────────────────────────────────────

	enum class EParam : uint8 { Scalar, Vector, Texture, Switch };

	struct FParams
	{
		TArray<FName> Scalar, Vector, Texture, Switch;

		explicit FParams(UMaterialInterface* M)
		{
			UMaterialEditingLibrary::GetScalarParameterNames(M, Scalar);
			UMaterialEditingLibrary::GetVectorParameterNames(M, Vector);
			UMaterialEditingLibrary::GetTextureParameterNames(M, Texture);
			UMaterialEditingLibrary::GetStaticSwitchParameterNames(M, Switch);
		}

		static FName Find(const TArray<FName>& In, const FString& Name)
		{
			const FString Wanted = Name.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT(""));
			for (const FName& N : In)
			{
				if (N.ToString().Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT("")).Equals(Wanted, ESearchCase::IgnoreCase)) return N;
			}
			return NAME_None;
		}

		FString All() const
		{
			TArray<FString> Out;
			for (const FName& N : Scalar) Out.Add(N.ToString() + TEXT(" (scalar)"));
			for (const FName& N : Vector) Out.Add(N.ToString() + TEXT(" (vector)"));
			for (const FName& N : Texture) Out.Add(N.ToString() + TEXT(" (texture)"));
			for (const FName& N : Switch) Out.Add(N.ToString() + TEXT(" (switch)"));
			return Out.Num() > 0 ? FString::Join(Out, TEXT(", ")) : FString(TEXT("none"));
		}
	};

	bool SetParameter(UMaterialInstanceConstant* MI, const FParams& Params, const FString& Name, const TSharedPtr<FJsonValue>& V, FString& OutLine, FString& OutError)
	{
		FName Found;
		if (V->Type == EJson::Boolean && !(Found = FParams::Find(Params.Switch, Name)).IsNone())
		{
			UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MI, Found, V->AsBool());
			OutLine = FString::Printf(TEXT("%s = %s"), *Found.ToString(), V->AsBool() ? TEXT("true") : TEXT("false"));
			return true;
		}
		if (!(Found = FParams::Find(Params.Scalar, Name)).IsNone())
		{
			const TOptional<double> D = ZettaTool::ValueNumber(V);
			if (!D.IsSet())
			{
				OutError = FString::Printf(TEXT("%s is a scalar: give a number."), *Found.ToString());
				return false;
			}
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MI, Found, (float)D.GetValue());
			OutLine = FString::Printf(TEXT("%s = %s"), *Found.ToString(), *ZettaTool::Num(D.GetValue()));
			return true;
		}
		if (!(Found = FParams::Find(Params.Vector, Name)).IsNone())
		{
			FLinearColor C;
			if (!ReadColor(V, C))
			{
				OutError = FString::Printf(TEXT("%s is a vector: give \"#rrggbb\" or [r, g, b, a]."), *Found.ToString());
				return false;
			}
			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MI, Found, C);
			OutLine = FString::Printf(TEXT("%s = #%s"), *Found.ToString(), *C.ToFColor(true).ToHex());
			return true;
		}
		if (!(Found = FParams::Find(Params.Texture, Name)).IsNone())
		{
			FString Path = ZettaTool::ValueText(V);
			if (V->Type == EJson::Object) Path = ZettaTool::Str(V->AsObject(), TEXT("asset"));
			UTexture* T = Cast<UTexture>(ZettaTargets::LoadAsset(Path, OutError, UTexture::StaticClass()));
			if (!T) return false;
			UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MI, Found, T);
			OutLine = FString::Printf(TEXT("%s = %s"), *Found.ToString(), *ZettaTool::PackagePath(T->GetPathName()));
			return true;
		}
		if (!(Found = FParams::Find(Params.Switch, Name)).IsNone())
		{
			const FString S = ZettaTool::ValueText(V).ToLower();
			const bool bOn = S == TEXT("true") || S == TEXT("1") || S == TEXT("on");
			UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MI, Found, bOn);
			OutLine = FString::Printf(TEXT("%s = %s"), *Found.ToString(), bOn ? TEXT("true") : TEXT("false"));
			return true;
		}
		OutError = FString::Printf(TEXT("the parent has no parameter \"%s\". Its parameters: %s."), *Name, *Params.All());
		return false;
	}

	/** The shorthand keys (base_color, roughness, …) mapped to the parameter names materials commonly use. */
	FString AliasFor(const FParams& Params, const FString& Key, bool bTexture)
	{
		static const TMap<FString, TArray<FString>> Values = {
			{ TEXT("base_color"), { TEXT("BaseColor"), TEXT("Color"), TEXT("Tint"), TEXT("Albedo"), TEXT("Diffuse"), TEXT("BaseColorTint") } },
			{ TEXT("metallic"), { TEXT("Metallic"), TEXT("Metalness"), TEXT("Metal") } },
			{ TEXT("roughness"), { TEXT("Roughness"), TEXT("Rough") } },
			{ TEXT("specular"), { TEXT("Specular") } },
			{ TEXT("emissive"), { TEXT("Emissive"), TEXT("EmissiveColor"), TEXT("Emission") } },
			{ TEXT("emissive_strength"), { TEXT("EmissiveStrength"), TEXT("EmissiveIntensity"), TEXT("EmissivePower"), TEXT("Glow") } },
			{ TEXT("opacity"), { TEXT("Opacity"), TEXT("Alpha") } },
		};
		static const TMap<FString, TArray<FString>> Maps = {
			{ TEXT("base_color"), { TEXT("BaseColorTexture"), TEXT("BaseColorMap"), TEXT("Albedo"), TEXT("AlbedoMap"), TEXT("Diffuse"), TEXT("DiffuseTexture"), TEXT("BaseColor"), TEXT("Texture"), TEXT("Color") } },
			{ TEXT("normal"), { TEXT("Normal"), TEXT("NormalMap"), TEXT("NormalTexture") } },
			{ TEXT("roughness"), { TEXT("Roughness"), TEXT("RoughnessMap"), TEXT("RoughnessTexture") } },
			{ TEXT("metallic"), { TEXT("Metallic"), TEXT("MetallicMap"), TEXT("MetallicTexture") } },
			{ TEXT("ambient_occlusion"), { TEXT("AO"), TEXT("AmbientOcclusion"), TEXT("Occlusion"), TEXT("AOMap") } },
			{ TEXT("emissive"), { TEXT("Emissive"), TEXT("EmissiveMap"), TEXT("EmissiveTexture") } },
			{ TEXT("opacity"), { TEXT("Opacity"), TEXT("OpacityMap"), TEXT("OpacityMask") } },
			{ TEXT("orm"), { TEXT("ORM"), TEXT("OcclusionRoughnessMetallic"), TEXT("Packed"), TEXT("Mask"), TEXT("Masks") } },
		};
		const TArray<FString>* Names = (bTexture ? Maps : Values).Find(Key);
		if (!Names) return FString();
		for (const FString& N : *Names)
		{
			const TArray<FName>& Pool = bTexture ? Params.Texture : (Key == TEXT("base_color") || Key == TEXT("emissive") ? Params.Vector : Params.Scalar);
			const FName Found = FParams::Find(Pool, N);
			if (!Found.IsNone()) return Found.ToString();
		}
		return FString();
	}

	// ─── assign_material ─────────────────────────────────────────────────────

	int32 SlotIndex(const TSharedPtr<FJsonObject>& In, TFunctionRef<int32(FName)> ByName, int32 Count, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Slot = ZettaTool::Get(In, TEXT("slot"));
		int32 Index = 0;
		if (Slot.IsValid())
		{
			const TOptional<double> N = ZettaTool::ValueNumber(Slot);
			Index = N.IsSet() ? (int32)N.GetValue() : ByName(FName(*ZettaTool::ValueText(Slot)));
			if (Index == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("No material slot named \"%s\"."), *ZettaTool::ValueText(Slot));
				return INDEX_NONE;
			}
		}
		if (Index < 0 || Index >= Count)
		{
			OutError = FString::Printf(TEXT("Slot %d is out of range: it has %d material slot(s) (0–%d)."), Index, Count, Count - 1);
			return INDEX_NONE;
		}
		return Index;
	}

	bool Assign(const FString& Target, UMaterialInterface* Material, const TSharedPtr<FJsonObject>& In, FString& OutLine, FString& OutError)
	{
		if (Target.StartsWith(TEXT("/")))
		{
			UObject* Asset = ZettaTargets::LoadAsset(Target, OutError);
			if (!Asset) return false;
			const FString Package = ZettaTool::PackagePath(Asset->GetPathName());
			if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
			{
				const int32 Index = SlotIndex(In, [SM](FName N) { return SM->GetMaterialIndex(N); }, SM->GetStaticMaterials().Num(), OutError);
				if (Index == INDEX_NONE) return false;
				ZettaAgentChanges::WillChangeAsset(Package, TEXT("changed (material assigned)"));
				const FScopedTransaction Transaction(FText::FromString(ZettaTool::TransactionTitle(TEXT("Assign ") + Material->GetName())));
				SM->Modify();
				SM->SetMaterial(Index, Material);
				SM->PostEditChange();
			}
			else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
			{
				TArray<FSkeletalMaterial>& Materials = SK->GetMaterials();
				const int32 Index = SlotIndex(In, [&Materials](FName N) { return Materials.IndexOfByPredicate([N](const FSkeletalMaterial& M) { return M.MaterialSlotName == N; }); }, Materials.Num(), OutError);
				if (Index == INDEX_NONE) return false;
				ZettaAgentChanges::WillChangeAsset(Package, TEXT("changed (material assigned)"));
				const FScopedTransaction Transaction(FText::FromString(ZettaTool::TransactionTitle(TEXT("Assign ") + Material->GetName())));
				SK->Modify();
				Materials[Index].MaterialInterface = Material;
				SK->PostEditChange();
			}
			else
			{
				OutError = FString::Printf(TEXT("%s is a %s; assign materials to static or skeletal meshes."), *Package, *Asset->GetClass()->GetName());
				return false;
			}
			FString SaveError;
			OutLine = FString::Printf(TEXT("%s now uses %s%s"), *Package, *ZettaTool::PackagePath(Material->GetPathName()), ZettaAssets::Save(Asset, SaveError) ? TEXT(" (saved)") : *(TEXT(" (not saved: ") + SaveError + TEXT(")")));
			return true;
		}

		// A level actor ("Chair") or one of its components ("Chair/Cushion").
		AActor* Actor = nullptr;
		UMeshComponent* Mesh = nullptr;
		FString ActorError;
		Actor = ZettaTargets::FindActor(Target, ActorError);
		if (Actor)
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Actor);
			for (UMeshComponent* C : Meshes)
			{
				if (C && C->GetNumMaterials() > 0)
				{
					Mesh = C;
					break;
				}
			}
			if (!Mesh)
			{
				OutError = FString::Printf(TEXT("%s has no mesh component with material slots."), *ZettaTargets::Describe(Actor));
				return false;
			}
		}
		else
		{
			UActorComponent* Component = nullptr;
			if (!ZettaTargets::FindActorComponent(Target, Actor, Component, OutError))
			{
				if (!Target.Contains(TEXT("/"))) OutError = ActorError;
				return false;
			}
			Mesh = Cast<UMeshComponent>(Component);
			if (!Mesh)
			{
				OutError = FString::Printf(TEXT("%s is not a mesh component."), *ZettaTargets::DescribeComponent(Component));
				return false;
			}
		}
		const int32 Index = SlotIndex(In, [Mesh](FName N) { return Mesh->GetMaterialIndex(N); }, Mesh->GetNumMaterials(), OutError);
		if (Index == INDEX_NONE) return false;
		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Assign %s to %s"), *Material->GetName(), *ZettaTargets::Label(Actor)));
		{
			const FScopedTransaction Transaction(FText::FromString(Title));
			Mesh->Modify();
			Mesh->SetMaterial(Index, Material);
		}
		ZettaAgentChanges::LevelTransaction(Title);
		OutLine = FString::Printf(TEXT("%s/%s slot %d now uses %s"), *ZettaTargets::Label(Actor), *Mesh->GetName(), Index, *ZettaTool::PackagePath(Material->GetPathName()));
		return true;
	}

	FZettaToolResult AssignMaterial(const TSharedPtr<FJsonObject>& In)
	{
		FString Error, Line;
		UMaterialInterface* Material = Cast<UMaterialInterface>(ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("material")), Error, UMaterialInterface::StaticClass()));
		if (!Material) return Fail(Error);
		if (!Assign(ZettaTool::Str(In, TEXT("target")), Material, In, Line, Error)) return Fail(Error);
		return Ok(Line + TEXT("."), FString::Printf(TEXT("Assigned %s"), *Material->GetName()));
	}

	// ─── create_material ─────────────────────────────────────────────────────

	FZettaToolResult CreateMaterial(const TSharedPtr<FJsonObject>& In)
	{
		FString Package, Error;
		if (!ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("path")), Package, Error)) return Fail(Error);
		if (ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists. Change it with set_properties, or pick another path."), *Package));
		const FString Name = FPackageName::GetShortName(Package);
		const FString Folder = FPackageName::GetLongPackagePath(Package);
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<FString> Done, Errors;
		UMaterialInterface* Made = nullptr;

		if (ZettaTool::Has(In, TEXT("parent")))
		{
			UMaterialInterface* Parent = Cast<UMaterialInterface>(ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("parent")), Error, UMaterialInterface::StaticClass()));
			if (!Parent) return Fail(Error);
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Tools.CreateAsset(Name, Folder, UMaterialInstanceConstant::StaticClass(), Factory));
			if (!MI) return Fail(FString::Printf(TEXT("Unreal could not create %s."), *Package));
			ZettaAgentChanges::CreatedAsset(Package);
			Made = MI;
			const FParams Params(Parent);
			if (const TSharedPtr<FJsonObject> P = ZettaTool::Obj(In, TEXT("parameters")))
			{
				for (const auto& Pair : P->Values)
				{
					FString Line, ParamError;
					if (SetParameter(MI, Params, FString(Pair.Key), Pair.Value, Line, ParamError)) Done.Add(Line);
					else Errors.Add(FString::Printf(TEXT("%s: %s"), *FString(Pair.Key), *ParamError));
				}
			}
			// The shorthand keys, matched to the parent's parameters by their usual names.
			for (const TCHAR* Key : { TEXT("base_color"), TEXT("metallic"), TEXT("roughness"), TEXT("specular"), TEXT("emissive"), TEXT("emissive_strength"), TEXT("opacity") })
			{
				if (!ZettaTool::Has(In, Key)) continue;
				const FString Param = AliasFor(Params, Key, false);
				FString Line, ParamError;
				if (Param.IsEmpty()) Errors.Add(FString::Printf(TEXT("%s: the parent has no matching parameter. Its parameters: %s."), Key, *Params.All()));
				else if (SetParameter(MI, Params, Param, ZettaTool::Get(In, Key), Line, ParamError)) Done.Add(Line);
				else Errors.Add(FString::Printf(TEXT("%s: %s"), Key, *ParamError));
			}
			if (const TSharedPtr<FJsonObject> Textures = ZettaTool::Obj(In, TEXT("textures")))
			{
				for (const auto& Pair : Textures->Values)
				{
					const FString Key(Pair.Key);
					const FString Param = AliasFor(Params, Key, true);
					FString Line, ParamError;
					if (Param.IsEmpty()) Errors.Add(FString::Printf(TEXT("textures.%s: the parent has no matching texture parameter. Its parameters: %s."), *Key, *Params.All()));
					else if (SetParameter(MI, Params, Param, Pair.Value, Line, ParamError)) Done.Add(Line);
					else Errors.Add(FString::Printf(TEXT("textures.%s: %s"), *Key, *ParamError));
				}
			}
			for (const TCHAR* Key : { TEXT("blend"), TEXT("two_sided") })
			{
				if (ZettaTool::Has(In, Key)) Errors.Add(FString::Printf(TEXT("%s: an instance takes these from its parent (set them on the parent material)."), Key));
			}
			UMaterialEditingLibrary::UpdateMaterialInstance(MI);
		}
		else
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			UMaterial* M = Cast<UMaterial>(Tools.CreateAsset(Name, Folder, UMaterial::StaticClass(), Factory));
			if (!M) return Fail(FString::Printf(TEXT("Unreal could not create %s."), *Package));
			ZettaAgentChanges::CreatedAsset(Package);
			Made = M;
			if (ZettaTool::Has(In, TEXT("parameters"))) Errors.Add(TEXT("parameters: only a Material Instance (with 'parent') takes parameters."));
			Done = BuildMaterial(M, In, Errors);
			UMaterialEditingLibrary::RecompileMaterial(M);
		}

		FString SaveError;
		const bool bSaved = ZettaAssets::Save(Made, SaveError);
		TArray<FString> Assigned;
		for (const FString& Target : ZettaTool::Strings(In, TEXT("assign_to")))
		{
			FString Line, AssignError;
			if (Assign(Target, Made, MakeShared<FJsonObject>(), Line, AssignError)) Assigned.Add(Line);
			else Errors.Add(FString::Printf(TEXT("assign_to %s: %s"), *Target, *AssignError));
		}

		FString Text = FString::Printf(TEXT("Created %s%s."), *Package, Made->IsA<UMaterialInstanceConstant>() ? *FString::Printf(TEXT(" (an instance of %s)"), *ZettaTool::Str(In, TEXT("parent"))) : TEXT(""));
		if (Done.Num() > 0) Text += TEXT("\n") + FString::Join(Done, TEXT("\n"));
		if (Assigned.Num() > 0) Text += TEXT("\n") + FString::Join(Assigned, TEXT("\n"));
		if (Errors.Num() > 0) Text += TEXT("\nNot done:\n") + FString::Join(Errors, TEXT("\n"));
		Text += bSaved ? TEXT("\nSaved.") : TEXT("\nNot saved: ") + SaveError;
		return Ok(Text, FString::Printf(TEXT("Created %s"), *Name));
	}
}

void ZettaToolAreas::RegisterMaterialTools(FZettaAgentTools& Tools)
{
	using namespace ZettaMaterialImpl;
	Tools.Register(TEXT("create_material"), &CreateMaterial, [](const TSharedPtr<FJsonObject>& In)
	{
		const FString Path = ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path")));
		FString Body = ZettaTool::Has(In, TEXT("parent")) ? TEXT("A Material Instance of ") + ZettaTool::Str(In, TEXT("parent")) : FString(TEXT("A new Material"));
		const TArray<FString> Targets = ZettaTool::Strings(In, TEXT("assign_to"));
		if (Targets.Num() > 0) Body += TEXT("\nAssigned to ") + ZettaTool::JoinQuoted(Targets);
		return ZettaTool::Preview(FString::Printf(TEXT("Create %s"), *FPackageName::GetShortName(Path)), Body);
	});
	Tools.Register(TEXT("assign_material"), &AssignMaterial, [](const TSharedPtr<FJsonObject>& In)
	{
		return ZettaTool::Preview(FString::Printf(TEXT("Assign %s"), *FPackageName::GetShortName(ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("material"))))), TEXT("To ") + ZettaTool::Str(In, TEXT("target")));
	});
}
