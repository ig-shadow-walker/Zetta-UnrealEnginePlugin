// Copyright Zetta 3D. See LICENSE.md.
// The 3D Studio for the agent: generate_3d_model and process_model (paid: the approval card shows the quote, and
// nothing is drawn, uploaded or charged until the user approves), import_generation and list_generations (free).
// Started jobs are handed to FZettaJobs, which imports them when they finish and tells the agent (job_finished).

#include "GameFramework/Actor.h"
#include "ZettaAgentTools.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaCloud.h"
#include "ZettaHttp.h"
#include "ZettaImporter.h"
#include "ZettaJobs.h"
#include "ZettaRequestBuilders.h"
#include "ZettaRequests.h"
#include "ZettaSettings.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaCloudImpl
{
	bool bPlanLoaded = false;
	FZettaEntitlements3D CachedGates;
	FZettaPriceList CachedPrices;
}

namespace ZettaCloud
{
	bool RequireSignIn(FString& OutError)
	{
		if (FZettaAuth::Get().IsSignedIn()) return true;
		OutError = TEXT("Not signed in to Zetta 3D. Ask the user to sign in from the Zetta 3D window, then try again.");
		return false;
	}

	FString Explain(const FZettaResponse& Response) { return ZettaCatalog::Explain(Response); }

	const FZettaEntitlements3D& Gates() { return ZettaCloudImpl::CachedGates; }
	const FZettaPriceList& Prices() { return ZettaCloudImpl::CachedPrices; }

	void LoadPlan(TFunction<void(const FString& Error)> Done)
	{
		if (ZettaCloudImpl::bPlanLoaded)
		{
			Done(FString());
			return;
		}
		ZettaApi::Entitlements3D([Done](const FZettaResponse& GatesResponse, const FZettaEntitlements3D& Gates)
		{
			if (!GatesResponse.bOk)
			{
				Done(Explain(GatesResponse));
				return;
			}
			ZettaApi::Pricing([Done, Gates](const FZettaResponse& PriceResponse, const FZettaPriceList& Prices)
			{
				if (!PriceResponse.bOk)
				{
					Done(Explain(PriceResponse));
					return;
				}
				ZettaCloudImpl::CachedGates = Gates;
				ZettaCloudImpl::CachedPrices = Prices;
				ZettaCloudImpl::bPlanLoaded = true;
				Done(FString());
			});
		});
	}

	void ImageUrl(int64 ImageId, TFunction<void(const FString& Url, const FString& Error)> Done)
	{
		const FString Known = ZettaAgentSession::ImageUrl(ImageId);
		if (!Known.IsEmpty())
		{
			Done(Known, FString());
			return;
		}
		FZettaHttp::Get(FString::Printf(TEXT("/unrealAgent/images/%lld"), ImageId), [Done, ImageId](const FZettaResponse& Response)
		{
			if (Response.Status == 404)
			{
				Done(FString(), FString::Printf(TEXT("Image #%lld was not found. Use an imageId a picture tool returned, or one from list_images."), ImageId));
				return;
			}
			if (!Response.bOk)
			{
				Done(FString(), Explain(Response));
				return;
			}
			const TSharedPtr<FJsonObject> O = Response.Object();
			const FString Url = ZettaTool::Str(O, TEXT("url"));
			if (Url.IsEmpty()) Done(FString(), FString::Printf(TEXT("Image #%lld has no downloadable file."), ImageId));
			else Done(Url, FString());
		});
	}

	void DownloadImage(int64 ImageId, TFunction<void(const TArray<uint8>& Bytes, const FString& Error)> Done)
	{
		ImageUrl(ImageId, [Done, ImageId](const FString& Url, const FString& UrlError)
		{
			if (!UrlError.IsEmpty())
			{
				Done(TArray<uint8>(), UrlError);
				return;
			}
			FZettaHttp::Download(Url, [Done, ImageId](const FZettaResponse& Response)
			{
				if (!Response.bOk || Response.Bytes.Num() == 0) Done(TArray<uint8>(), FString::Printf(TEXT("Image #%lld could not be downloaded%s."), ImageId, Response.Message.IsEmpty() ? TEXT("") : *(TEXT(": ") + Response.Message)));
				else Done(Response.Bytes, FString());
			});
		});
	}

	void Upload(const TArray<uint8>& Bytes, const FString& ContentType, TFunction<void(const FString& Url, const FString& Error)> Done)
	{
		ZettaApi::Upload(Bytes, ContentType, [Done](const FZettaResponse& Response, const FZettaUploadTicket& Ticket)
		{
			if (!Response.bOk || Ticket.FileUrl.IsEmpty()) Done(FString(), Explain(Response));
			else Done(Ticket.FileUrl, FString());
		});
	}

	FString FolderFor(const FZettaGeneration& Generation)
	{
		return FString::Printf(TEXT("%s/%lld-%s/"), *UZettaProjectSettings::Get()->GetModelsFolder(), Generation.Id, *ZettaImporter::Slug(Generation.DisplayTitle()));
	}
}

namespace ZettaStudioImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	FString Credits(TOptional<int32> C) { return C.IsSet() ? FString::Printf(TEXT(" (%d credits)"), C.GetValue()) : FString(); }

	/** Where a finished model should go: { parent?, location? }. */
	bool ReadPlace(const TSharedPtr<FJsonObject>& In, FZettaImportOptions& Out, bool& bOutGiven, FString& OutError)
	{
		bOutGiven = false;
		const TSharedPtr<FJsonObject> Place = ZettaTool::Obj(In, TEXT("place"));
		if (!Place.IsValid()) return true;
		bOutGiven = true;
		Out.bPlace = true;
		TOptional<FVector> Location;
		if (!ZettaTool::OptVector(Place, TEXT("location"), Location, OutError)) return false;
		if (!Location.IsSet()) ZettaTool::OptVector(Place, TEXT("position"), Location, OutError);
		Out.Location = Location;
		if (ZettaTool::Has(Place, TEXT("parent")))
		{
			AActor* Parent = ZettaTargets::FindActor(ZettaTool::Str(Place, TEXT("parent")), OutError);
			if (!Parent) return false;
			Out.Parent = Parent;
		}
		return true;
	}

	// ─── generate_3d_model ───────────────────────────────────────────────────

	struct FGenerate
	{
		FZettaGenerateOptions Options;
		TOptional<int64> ImageId;
		FZettaImportOptions Import;
		bool bPlaceGiven = false;
		/** The model needs a picture and none was given: one is drawn from the prompt after approval. */
		bool bDrawFirst = false;
	};

	bool PickChoice(const TSharedPtr<FJsonObject>& In, const FString& Key, const TArray<FString>& Allowed, const FString& Fallback, FString& Out, FString& OutError)
	{
		FString V = ZettaTool::Str(In, Key).TrimStartAndEnd().ToLower();
		if (V == TEXT("quad") || V == TEXT("quads")) V = TEXT("quadrilateral");
		if (V == TEXT("tri") || V == TEXT("tris") || V == TEXT("triangles")) V = TEXT("triangle");
		if (V.IsEmpty()) V = Fallback;
		if (!Allowed.Contains(V))
		{
			OutError = FString::Printf(TEXT("'%s' must be one of %s."), *Key, *FString::Join(Allowed, TEXT(", ")));
			return false;
		}
		Out = V;
		return true;
	}

	bool ParseGenerate(const TSharedPtr<FJsonObject>& In, FGenerate& G, FString& OutError)
	{
		FZettaGenerateOptions& O = G.Options;
		O.Prompt = ZettaTool::Str(In, TEXT("prompt")).TrimStartAndEnd();
		G.ImageId = ZettaTool::Int(In, TEXT("imageId"));
		if (O.Prompt.IsEmpty() && !G.ImageId.IsSet())
		{
			OutError = TEXT("Give a prompt, or an imageId from generate_image.");
			return false;
		}
		if (O.Prompt.Len() > 1024)
		{
			OutError = TEXT("The prompt is longer than 1,024 characters.");
			return false;
		}
		if (!PickChoice(In, TEXT("model"), { TEXT("n25"), TEXT("np1"), TEXT("np15") }, TEXT("n25"), O.Model, OutError)) return false;
		if (!PickChoice(In, TEXT("quality"), { TEXT("low-poly"), TEXT("high-res"), TEXT("ultra") }, TEXT("high-res"), O.Quality, OutError)) return false;
		if (!PickChoice(In, TEXT("output"), { TEXT("textured"), TEXT("geometry") }, TEXT("textured"), O.Output, OutError)) return false;
		if (!PickChoice(In, TEXT("polygonType"), { TEXT("triangle"), TEXT("quadrilateral") }, TEXT("triangle"), O.PolygonType, OutError)) return false;
		O.bPbr = ZettaTool::BoolOr(In, TEXT("pbr"), true);
		if (ZettaTool::Has(In, TEXT("faceCount")))
		{
			const TOptional<int64> Faces = ZettaTool::Int(In, TEXT("faceCount"));
			if (!Faces.IsSet() || Faces.GetValue() <= 0)
			{
				OutError = TEXT("'faceCount' must be a whole number.");
				return false;
			}
			O.FaceCount = (int32)Faces.GetValue();
		}
		FString Title = ZettaTool::Str(In, TEXT("title")).TrimStartAndEnd();
		if (Title.IsEmpty()) Title = !O.Prompt.IsEmpty() ? ZettaTool::Short(O.Prompt, 60) : FString::Printf(TEXT("Model from image #%lld"), G.ImageId.Get(0));
		O.Title = Title.Left(120);
		if (!ReadPlace(In, G.Import, G.bPlaceGiven, OutError)) return false;
		if (!G.bPlaceGiven) G.Import.bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;
		G.Import.bSelect = false;
		G.bDrawFirst = !G.ImageId.IsSet() && !ZettaCatalog::Model(O.Model).bBarePrompt;
		return true;
	}

	FString GateProblem(const FZettaGenerateOptions& O)
	{
		const FZettaEntitlements3D& Gates = ZettaCloud::Gates();
		const FString Label = ZettaCatalog::Model(O.Model).Label;
		if (O.Model == TEXT("n25") && !Gates.Allows(Gates.AllowsZettaN25)) return FString::Printf(TEXT("The user's plan does not include %s. Use model n1, or ask them to upgrade."), *Label);
		if ((O.Model == TEXT("np1") || O.Model == TEXT("np15")) && !Gates.Allows(Gates.AllowsZettaNp1)) return FString::Printf(TEXT("The user's plan does not include %s. Use model n1, or ask them to upgrade."), *Label);
		if (O.Quality == TEXT("low-poly") && !Gates.Allows(Gates.AllowsLowPoly)) return TEXT("The user's plan does not include Low-Poly. Use high-res.");
		if (O.PolygonType == TEXT("quadrilateral") && !Gates.Allows(Gates.AllowsQuadMesh)) return TEXT("The user's plan does not include quad meshes. Use triangle.");
		return FString();
	}

	/** The request as it will be sent, with a stand-in picture link where the real one is made after approval. */
	FZettaBuiltRequest BuildForQuote(FGenerate G)
	{
		if (G.ImageId.IsSet() || G.bDrawFirst)
		{
			G.Options.ImageUrl = TEXT("https://placeholder.invalid/reference.png");
			G.Options.bImageFromPrompt = !G.Options.Prompt.IsEmpty();
		}
		return ZettaRequests::Build(G.Options, &ZettaCloud::Gates());
	}

	FString GenerateDetail(const FGenerate& G)
	{
		const FZettaGenerateOptions& O = G.Options;
		const ZettaCatalog::FChoice* Quality = ZettaCatalog::Qualities().FindByPredicate([&](const ZettaCatalog::FChoice& C) { return C.Key == O.Quality; });
		FString D = FString::Printf(TEXT("%s · %s · %s"), *ZettaCatalog::Model(O.Model).Label, Quality ? *Quality->Label : *O.Quality, *O.Output);
		if (O.Output == TEXT("textured")) D += O.bPbr ? TEXT(" · PBR") : TEXT(" · no PBR");
		if (O.FaceCount.IsSet()) D += FString::Printf(TEXT(" · %s faces"), *FString::FormatAsNumber(O.FaceCount.GetValue()));
		if (O.PolygonType == TEXT("quadrilateral")) D += TEXT(" · quads");
		if (!O.Prompt.IsEmpty()) D += TEXT("\nPrompt: ") + ZettaTool::Short(O.Prompt, 300);
		if (G.ImageId.IsSet()) D += FString::Printf(TEXT("\nFrom picture #%lld"), G.ImageId.GetValue());
		if (G.bDrawFirst) D += TEXT("\nA reference picture is drawn from the prompt first.");
		D += FString::Printf(TEXT("\nImports to %s/ when done"), *UZettaProjectSettings::Get()->GetModelsFolder());
		if (G.Import.bPlace) D += G.Import.Parent.IsValid() ? TEXT(", placed under ") + ZettaTargets::Label(G.Import.Parent.Get()) : FString(TEXT(", placed in the level"));
		return D + TEXT(".");
	}

	void DescribeGenerate(const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		const FString Title = TEXT("Generate a 3D model");
		FString Error;
		FGenerate G;
		if (!ZettaCloud::RequireSignIn(Error) || !ParseGenerate(In, G, Error))
		{
			Done(ZettaTool::PreviewError(Title, Error));
			return;
		}
		ZettaCloud::LoadPlan([Done, G, Title](const FString& PlanError)
		{
			if (!PlanError.IsEmpty())
			{
				Done(ZettaTool::PreviewError(Title, PlanError));
				return;
			}
			const FString Gate = GateProblem(G.Options);
			const FZettaBuiltRequest Built = BuildForQuote(G);
			if (!Gate.IsEmpty() || !Built.IsOk())
			{
				Done(ZettaTool::PreviewError(Title, Gate.IsEmpty() ? FString::Join(Built.Problems, TEXT(" ")) : Gate));
				return;
			}
			FZettaToolPreview Preview = ZettaTool::Preview(Title, FString::Printf(TEXT("%s\n%s"), *G.Options.Title, *GenerateDetail(G)));
			const TOptional<int32> ListPrice = ZettaRequests::Price(ZettaCloud::Prices(), Built, G.Options.Model);
			// The server's quote is the authority; the price list stands in when it can't be reached.
			ZettaApi::Quote(Built.Base, Built.Modifiers, G.Options.Model, [Done, Preview, ListPrice](const FZettaResponse& QuoteResponse, const FZettaQuote& Quote)
			{
				FZettaToolPreview P = Preview;
				if (QuoteResponse.bOk) P.Credits = Quote.Credits;
				else P.Credits = ListPrice;
				Done(P);
			});
		});
	}

	void StartGeneration(FGenerate G, FZettaToolDone Done)
	{
		const FZettaBuiltRequest Built = ZettaRequests::Build(G.Options, &ZettaCloud::Gates());
		if (!Built.IsOk())
		{
			Done(Fail(FString::Join(Built.Problems, TEXT(" "))));
			return;
		}
		const TOptional<int32> Price = ZettaRequests::Price(ZettaCloud::Prices(), Built, G.Options.Model);
		ZettaApi::CreateGeneration(Built.Body.ToSharedRef(), [G, Done, Price](const FZettaResponse& Response, const FZettaGenerationPtr& Generation)
		{
			if (!Response.bOk || !Generation.IsValid())
			{
				Done(Fail(Response.bOk ? FString(TEXT("Zetta 3D did not return the new job.")) : ZettaCloud::Explain(Response)));
				return;
			}
			FZettaJobs::Get().Track(Generation, true, G.Import, TEXT("agent"));
			FString Text = FString::Printf(TEXT("Started generation #%lld%s. It will be imported to %s when done%s; you'll get a job_finished event."), Generation->Id, *Credits(Price),
				*ZettaCloud::FolderFor(*Generation), G.Import.bPlace ? TEXT(" and placed in the level") : TEXT(""));
			Done(Ok(Text, FString::Printf(TEXT("Started generation #%lld"), Generation->Id)));
		});
	}

	void Generate(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error;
		FGenerate G;
		if (!ZettaCloud::RequireSignIn(Error) || !ParseGenerate(In, G, Error))
		{
			Done(Fail(Error));
			return;
		}
		ZettaCloud::LoadPlan([G, Done](const FString& PlanError) mutable
		{
			if (!PlanError.IsEmpty())
			{
				Done(Fail(PlanError));
				return;
			}
			const FString Gate = GateProblem(G.Options);
			if (!Gate.IsEmpty())
			{
				Done(Fail(Gate));
				return;
			}
			auto WithPicture = [G, Done](const TArray<uint8>& Bytes, const FString& Type) mutable
			{
				ZettaCloud::Upload(Bytes, Type, [G, Done](const FString& Url, const FString& UploadError) mutable
				{
					if (!UploadError.IsEmpty())
					{
						Done(Fail(UploadError));
						return;
					}
					G.Options.ImageUrl = Url;
					G.Options.bImageFromPrompt = !G.Options.Prompt.IsEmpty();
					StartGeneration(G, Done);
				});
			};
			if (G.ImageId.IsSet())
			{
				ZettaCloud::DownloadImage(G.ImageId.GetValue(), [WithPicture, Done](const TArray<uint8>& Bytes, const FString& DownloadError) mutable
				{
					if (!DownloadError.IsEmpty()) Done(Fail(DownloadError));
					else WithPicture(Bytes, ZettaImages::SniffContentType(Bytes).IsEmpty() ? FString(TEXT("image/png")) : ZettaImages::SniffContentType(Bytes));
				});
			}
			else if (G.bDrawFirst)
			{
				ZettaApi::FImageOptions Options;
				Options.bFor3D = true;
				ZettaApi::GenerateImage(G.Options.Prompt, Options, {}, [WithPicture, Done](const FZettaResponse& Response, const FZettaImageResult& Image) mutable
				{
					if (!Response.bOk || Image.Images.Num() == 0 || Image.Images[0].Num() == 0)
					{
						Done(Fail(Response.bOk ? FString(TEXT("The reference picture could not be drawn; try again or use model n1. Nothing was charged.")) : ZettaCloud::Explain(Response)));
						return;
					}
					WithPicture(Image.Images[0], Image.ContentTypes.Num() > 0 && !Image.ContentTypes[0].IsEmpty() ? Image.ContentTypes[0] : FString(TEXT("image/png")));
				});
			}
			else
			{
				StartGeneration(G, Done);
			}
		});
	}

	// ─── process_model ───────────────────────────────────────────────────────

	/** The options object, or — when the model flattened them — the input's other keys. */
	TSharedPtr<FJsonObject> ProcessOptions(const TSharedPtr<FJsonObject>& In)
	{
		if (const TSharedPtr<FJsonObject> O = ZettaTool::Obj(In, TEXT("options"))) return O;
		TSharedRef<FJsonObject> Flat = MakeShared<FJsonObject>();
		for (const auto& Pair : In->Values)
		{
			const FString Key(Pair.Key);
			if (Key != TEXT("generationId") && Key != TEXT("operation") && Key != TEXT("options") && Pair.Value.IsValid() && !Pair.Value->IsNull()) Flat->SetField(Key, Pair.Value);
		}
		return Flat;
	}

	/** Validates everything that can be checked before spending: the plan, the source row, the plan's gates. */
	void PrepareProcess(const TSharedPtr<FJsonObject>& In, TFunction<void(const FString& Error, const ZettaRequestBuilders::FProcessPlan& Plan, const FZettaGenerationPtr& Source)> Done)
	{
		FString Error;
		ZettaRequestBuilders::FProcessPlan Plan;
		const TOptional<int64> Id = ZettaTool::Int(In, TEXT("generationId"));
		if (!Id.IsSet()) Error = TEXT("'generationId' is required.");
		if (!Error.IsEmpty() || !ZettaCloud::RequireSignIn(Error) || !ZettaRequestBuilders::Process(ZettaTool::Str(In, TEXT("operation")), ProcessOptions(In), Plan, Error))
		{
			Done(Error, Plan, nullptr);
			return;
		}
		const int64 SourceId = Id.GetValue();
		ZettaApi::GetGeneration(SourceId, [Done, Plan, SourceId](const FZettaResponse& Response, const FZettaGenerationPtr& Source, TOptional<int32>)
		{
			if (Response.Status == 404 || (Response.bOk && !Source.IsValid()))
			{
				Done(FString::Printf(TEXT("Generation #%lld is not in the user's library. Use list_generations to find the id."), SourceId), Plan, nullptr);
				return;
			}
			if (!Response.bOk)
			{
				Done(ZettaCloud::Explain(Response), Plan, nullptr);
				return;
			}
			FString Refusal = ZettaRequestBuilders::RefuseSource(Plan.Operation, *Source);
			if (Refusal.IsEmpty() && Plan.Operation == TEXT("smart_uv")) Refusal = ZettaRequestBuilders::SmartUvProblem(*Source);
			if (!Refusal.IsEmpty())
			{
				Done(Refusal, Plan, Source);
				return;
			}
			ZettaCloud::LoadPlan([Done, Plan, Source](const FString& PlanError)
			{
				if (!PlanError.IsEmpty())
				{
					Done(PlanError, Plan, Source);
					return;
				}
				const FZettaEntitlements3D& Gates = ZettaCloud::Gates();
				if (!Gates.Allows(Gates.AllowsPostProcess))
				{
					Done(FString::Printf(TEXT("%s is a finishing tool, which needs a paid plan. Ask the user to upgrade at zetta3d.ai/pricing."), *ZettaRequestBuilders::OperationLabel(Plan.Operation)), Plan, Source);
					return;
				}
				if (ZettaTool::Str(Plan.Options, TEXT("polygonType")) == TEXT("quadrilateral") && !Gates.Allows(Gates.AllowsQuadMesh))
				{
					Done(TEXT("The user's plan does not include quad meshes. Use polygonType triangle."), Plan, Source);
					return;
				}
				Done(FString(), Plan, Source);
			});
		});
	}

	void DescribeProcess(const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		PrepareProcess(In, [Done](const FString& Error, const ZettaRequestBuilders::FProcessPlan& Plan, const FZettaGenerationPtr& Source)
		{
			const FString Label = ZettaRequestBuilders::OperationLabel(Plan.Operation);
			if (!Error.IsEmpty())
			{
				Done(ZettaTool::PreviewError(Label, Error));
				return;
			}
			FString Body = FString::Printf(TEXT("%s on #%lld \"%s\" (%s)"), *Label, Source->Id, *Source->DisplayTitle(), *ZettaCatalog::FeatureLabel(Source->Feature));
			for (const FString& Line : Plan.Summary) Body += TEXT("\n") + Line;
			if (Plan.NeedsPicture() && !Plan.ImageId.IsSet() && !Plan.Prompt.IsEmpty() && Source->InputImageUrls.Num() > 0) Body += TEXT("\nThe description is applied to the model's own input picture.");
			Body += FString::Printf(TEXT("\nImports to %s/ when done."), *UZettaProjectSettings::Get()->GetModelsFolder());
			FZettaToolPreview P = ZettaTool::Preview(FString::Printf(TEXT("%s: %s"), *Label, *Source->DisplayTitle()), Body);
			P.Credits = ZettaCloud::Prices().Price(Plan.Code, Plan.PriceModel);
			Done(P);
		});
	}

	void CreateProcess(const ZettaRequestBuilders::FProcessPlan& Plan, const FZettaGenerationPtr& Source, const FString& ImageUrl, FZettaToolDone Done)
	{
		const TOptional<int32> Price = ZettaCloud::Prices().Price(Plan.Code, Plan.PriceModel);
		ZettaApi::CreateGeneration(ZettaRequestBuilders::ProcessBody(Plan, Source->Id, ImageUrl), [Plan, Source, Price, Done](const FZettaResponse& Response, const FZettaGenerationPtr& Generation)
		{
			if (!Response.bOk || !Generation.IsValid())
			{
				Done(Fail(Response.bOk ? FString(TEXT("Zetta 3D did not return the new job.")) : ZettaCloud::Explain(Response)));
				return;
			}
			FZettaImportOptions Import;
			Import.bPlace = false;
			Import.bSelect = false;
			FZettaJobs::Get().Track(Generation, true, Import, TEXT("agent"));
			const FString Label = ZettaRequestBuilders::OperationLabel(Plan.Operation);
			FString Extra;
			if (Plan.Operation == TEXT("segment")) Extra = TEXT(" The parts import as separate meshes in one folder.");
			else if (Plan.Operation == TEXT("rig")) Extra = TEXT(" The rigged model imports as a Skeletal Mesh with its skeleton.");
			else if (Plan.Operation == TEXT("smart_uv")) Extra = TEXT(" The UV layout picture comes with it when the job makes one.");
			Done(Ok(FString::Printf(TEXT("Started %s #%lld on #%lld%s. It will be imported to %s when done (not placed); you'll get a job_finished event.%s"), *Label.ToLower(), Generation->Id,
				Source->Id, *Credits(Price), *ZettaCloud::FolderFor(*Generation), *Extra), FString::Printf(TEXT("Started %s #%lld"), *Label, Generation->Id)));
		});
	}

	void ProcessModel(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		PrepareProcess(In, [Done](const FString& Error, const ZettaRequestBuilders::FProcessPlan& Plan, const FZettaGenerationPtr& Source)
		{
			if (!Error.IsEmpty())
			{
				Done(Fail(Error));
				return;
			}
			if (!Plan.NeedsPicture())
			{
				CreateProcess(Plan, Source, FString(), Done);
				return;
			}
			// A texture's reference picture: the agent's picture, changed as described; or the model's own input picture
			// changed as described; or one drawn from the description. The engine reads pictures, never the words.
			auto Upload = [Plan, Source, Done](const TArray<uint8>& Bytes, const FString& Type)
			{
				ZettaCloud::Upload(Bytes, Type, [Plan, Source, Done](const FString& Url, const FString& UploadError)
				{
					if (!UploadError.IsEmpty()) Done(Fail(UploadError));
					else CreateProcess(Plan, Source, Url, Done);
				});
			};
			auto Draw = [Plan, Upload, Done](const TArray<uint8>& Reference)
			{
				ZettaApi::FImageOptions Options;
				Options.bFor3D = true;
				auto Drawn = [Upload, Done](const FZettaResponse& Response, const FZettaImageResult& Image)
				{
					if (!Response.bOk || Image.Images.Num() == 0 || Image.Images[0].Num() == 0)
					{
						Done(Fail((Response.bOk ? FString(TEXT("The reference picture could not be drawn")) : ZettaCloud::Explain(Response)) + TEXT(". Nothing was charged for the texture.")));
						return;
					}
					Upload(Image.Images[0], Image.ContentTypes.Num() > 0 && !Image.ContentTypes[0].IsEmpty() ? Image.ContentTypes[0] : FString(TEXT("image/png")));
				};
				if (Reference.Num() > 0)
				{
					const FString Type = ZettaImages::SniffContentType(Reference);
					ZettaApi::EditImage(Plan.Prompt, Options, FZettaInlineImage::From(Reference, Type.IsEmpty() ? FString(TEXT("image/png")) : Type), TOptional<int64>(), Drawn);
				}
				else
				{
					ZettaApi::GenerateImage(Plan.Prompt, Options, {}, Drawn);
				}
			};
			auto WithReference = [Plan, Upload, Draw](const TArray<uint8>& Reference)
			{
				if (Plan.Prompt.IsEmpty()) Upload(Reference, ZettaImages::SniffContentType(Reference).IsEmpty() ? FString(TEXT("image/png")) : ZettaImages::SniffContentType(Reference));
				else Draw(Reference);
			};
			if (Plan.ImageId.IsSet())
			{
				ZettaCloud::DownloadImage(Plan.ImageId.GetValue(), [WithReference, Done](const TArray<uint8>& Bytes, const FString& DownloadError)
				{
					if (!DownloadError.IsEmpty()) Done(Fail(DownloadError));
					else WithReference(Bytes);
				});
			}
			else if (Source->InputImageUrls.Num() > 0 && !Source->InputImageUrls[0].IsEmpty())
			{
				// An expired input picture is no loss: the description is drawn from scratch instead.
				FZettaHttp::Download(Source->InputImageUrls[0], [Draw](const FZettaResponse& Response)
				{
					Draw(Response.bOk ? Response.Bytes : TArray<uint8>());
				});
			}
			else
			{
				Draw(TArray<uint8>());
			}
		});
	}

	// ─── import_generation / list_generations ────────────────────────────────

	void ImportGeneration(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error;
		const TOptional<int64> Id = ZettaTool::Int(In, TEXT("generationId"));
		if (!ZettaCloud::RequireSignIn(Error)) { Done(Fail(Error)); return; }
		if (!Id.IsSet()) { Done(Fail(TEXT("'generationId' is required."))); return; }
		FZettaImportOptions Options;
		bool bPlaceGiven = false;
		if (!ReadPlace(In, Options, bPlaceGiven, Error)) { Done(Fail(Error)); return; }
		// Without a place, the model still lands in the level, on the ground in front of the viewport.
		Options.bPlace = true;
		Options.bSelect = false;
		if (ZettaTool::Has(In, TEXT("folder")))
		{
			if (!ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("folder")), Options.Folder, Error)) { Done(Fail(Error)); return; }
		}
		if (ZettaTool::IsPlaying()) { Done(Fail(TEXT("Play In Editor is running. Stop it first (play_in_editor stop), then import."))); return; }
		const int64 GenerationId = Id.GetValue();
		ZettaApi::GetGeneration(GenerationId, [Done, Options, GenerationId](const FZettaResponse& Response, const FZettaGenerationPtr& Generation, TOptional<int32>)
		{
			if (Response.Status == 404 || (Response.bOk && !Generation.IsValid()))
			{
				Done(Fail(FString::Printf(TEXT("Generation #%lld is not in the user's library. Use list_generations to find the id."), GenerationId)));
				return;
			}
			if (!Response.bOk) { Done(Fail(ZettaCloud::Explain(Response))); return; }
			if (Generation->IsSplat()) { Done(Fail(ZettaImporter::SplatUnsupported)); return; }
			if (!Generation->IsSuccess())
			{
				Done(Fail(FString::Printf(TEXT("Generation #%lld is %s (%s); only finished models can be imported.%s"), GenerationId, *Generation->StatusLabel().ToLower(), *Generation->Status,
					Generation->IsRunning() ? TEXT(" Wait for it to finish.") : TEXT(""))));
				return;
			}
			ZettaImporter::Import(Generation, Options, [Done, Generation](const FZettaImportResult& Result)
			{
				if (!Result.bOk)
				{
					Done(Fail(FString::Printf(TEXT("Importing #%lld failed: %s"), Generation->Id, *Result.Error)));
					return;
				}
				TArray<FString> Assets;
				for (const FString& P : Result.AssetPaths)
				{
					const FString Package = ZettaTool::PackagePath(P);
					Assets.AddUnique(Package);
					ZettaAgentChanges::CreatedAsset(Package);
				}
				FString Text = FString::Printf(TEXT("Imported #%lld \"%s\" into %s: %s."), Generation->Id, *Generation->DisplayTitle(), *Result.Folder, *FString::Join(Assets, TEXT(", ")));
				if (!Result.UvLayoutPath.IsEmpty()) Text += TEXT(" UV layout picture: ") + Result.UvLayoutPath + TEXT(".");
				if (!Result.Note.IsEmpty()) Text += TEXT(" ") + Result.Note.TrimStartAndEnd();
				if (Result.PlacedActors.Num() > 0)
				{
					Text += FString::Printf(TEXT(" Placed in the level as %s."), *ZettaTool::JoinQuoted(Result.PlacedActors));
					for (const TWeakObjectPtr<AActor>& A : Result.Placed)
					{
						if (A.IsValid())
						{
							FVector Origin, Extent;
							A->GetActorBounds(true, Origin, Extent);
							Text += FString::Printf(TEXT(" %s is at %s, size %s cm."), *ZettaTargets::Label(A.Get()), *ZettaTool::Vec(A->GetActorLocation()), *ZettaTool::Vec(Extent * 2));
							break;
						}
					}
				}
				else
				{
					Text += FString::Printf(TEXT(" Not placed; use spawn_actor with mesh \"%s\" to put it in the level."), Assets.Num() > 0 ? *Assets[0] : TEXT(""));
				}
				Done(Ok(Text, FString::Printf(TEXT("Imported #%lld"), Generation->Id)));
			});
		});
	}

	void ListGenerations(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error;
		if (!ZettaCloud::RequireSignIn(Error)) { Done(Fail(Error)); return; }
		ZettaApi::FListQuery Query;
		Query.Page = 1;
		Query.PageSize = ZettaTool::IntIn(In, TEXT("limit"), 20, 1, 50);
		Query.Search = ZettaTool::Str(In, TEXT("search")).TrimStartAndEnd();
		ZettaApi::ListGenerations(Query, [Done, Query](const FZettaResponse& Response, const FZettaGenerationPage& Page)
		{
			if (!Response.bOk) { Done(Fail(ZettaCloud::Explain(Response))); return; }
			if (Page.Items.Num() == 0)
			{
				Done(Ok(Query.Search.IsEmpty() ? FString(TEXT("The Zetta 3D library is empty.")) : FString::Printf(TEXT("No models match \"%s\"."), *Query.Search)));
				return;
			}
			TArray<FString> Lines;
			for (const FZettaGenerationPtr& G : Page.Items)
			{
				if (!G.IsValid()) continue;
				FString Line = FString::Printf(TEXT("#%lld %s — kind: %s, %s"), G->Id, *G->DisplayTitle(), G->IsSplat() ? TEXT("splat") : TEXT("mesh"), *ZettaCatalog::FeatureLabel(G->Feature));
				if (!G->Model.IsEmpty()) Line += TEXT(", ") + ZettaCatalog::ModelLabel(G->Model);
				Line += TEXT(", ") + G->StatusLabel();
				if (!G->IsSuccess()) Line += FString::Printf(TEXT(" (%s)"), *G->Status);
				Line += TEXT(", ") + G->CreatedAt.ToString(TEXT("%Y-%m-%d %H:%M"));
				if (ZettaProjectIndex::Contains(G->Id)) Line += TEXT(" — already in the project at ") + ZettaProjectIndex::FolderOf(G->Id);
				Lines.Add(Line);
			}
			Done(Ok(FString::Printf(TEXT("%d of %d model(s), newest first:\n%s\nimport_generation brings a Ready mesh into the project (free); meshes can be processed with process_model. "
				"Splat rows (Gaussian splats and worlds made on the web) cannot be used in Unreal — the user can open them on zetta3d.ai."), Lines.Num(), Page.Total, *FString::Join(Lines, TEXT("\n")))));
		});
	}
}

void ZettaToolAreas::RegisterZettaTools(FZettaAgentTools& Tools)
{
	using namespace ZettaStudioImpl;
	Tools.RegisterAsync(TEXT("generate_3d_model"), &Generate, &DescribeGenerate);
	Tools.RegisterAsync(TEXT("process_model"), &ProcessModel, &DescribeProcess);
	Tools.RegisterAsync(TEXT("import_generation"), &ImportGeneration, [](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(FString::Printf(TEXT("Import model #%s"), *ZettaTool::Str(In, TEXT("generationId"))), TEXT("Free: downloads a finished model from the library and places it in the level.")));
	});
	Tools.RegisterAsync(TEXT("list_generations"), &ListGenerations, [](const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(TEXT("Browse your 3D library")));
	});
}
