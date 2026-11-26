// Copyright Zetta 3D. See LICENSE.md.

// The agent's 3D Pipelines tools: list_pipelines, get_pipeline, build_pipeline, edit_pipeline, run_pipeline,
// pipeline_run_status, manage_pipeline_run, import_pipeline_results. All the work — validation, the step requests,
// prices, following runs — is the Pipelines service's (Zetta3DEditor/Public/ZettaPipelines.h), which the Pipelines page
// uses too, so a pipeline the agent builds is the same thing the user sees and can run from the canvas.

#include "ZettaAgentTools.h"
#include "ZettaPipelines.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaPipelineToolsImpl
{
	FZettaToolResult FromResult(const ZettaPipelines::FResult& R)
	{
		return R.bOk ? FZettaToolResult::Ok(R.Text) : FZettaToolResult::Error(R.Text);
	}

	FZettaToolPreview FromApproval(const ZettaPipelines::FApproval& A)
	{
		FZettaToolPreview P;
		P.Title = A.Title;
		P.Body = A.Body;
		P.Credits = A.Credits;
		P.Error = A.Error;
		return P;
	}

	FString SessionId()
	{
		const int64 Id = ZettaAgentSession::CurrentSessionId();
		return Id > 0 ? FString::Printf(TEXT("%lld"), Id) : FString();
	}

	/** A preview for the simple writes: the service's own summary of the call. */
	void DescribeWrite(const FString& Tool, const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		FZettaToolPreview P;
		P.Title = ZettaPipelines::DescribeCall(Tool, In);
		P.Body = ZettaPipelines::SummarizeCall(Tool, In);
		if (P.Title.IsEmpty()) P.Title = Tool;
		Done(P);
	}

	void ListPipelines(const TSharedPtr<FJsonObject>&, FZettaToolDone Done)
	{
		ZettaPipelines::List([Done](const ZettaPipelines::FListResult& R) { Done(FromResult(R)); });
	}

	void GetPipeline(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		int64 Id = 0;
		FString Error;
		if (!ZettaPipelines::ParseId(In, TEXT("pipelineId"), Id, Error)) return Done(FZettaToolResult::Error(Error));
		ZettaPipelines::Get(Id, [Done](const ZettaPipelines::FPipelineDetails& R) { Done(FromResult(R)); });
	}

	void BuildPipeline(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		ZettaPipelines::FBuildRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseBuild(In, Request, Error)) return Done(FZettaToolResult::Error(Error));
		ZettaPipelines::Build(Request, [Done](const ZettaPipelines::FSaveResult& R) { Done(FromResult(R)); });
	}

	void EditPipeline(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		ZettaPipelines::FEditRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseEdit(In, Request, Error)) return Done(FZettaToolResult::Error(Error));
		ZettaPipelines::Edit(Request, [Done](const ZettaPipelines::FSaveResult& R) { Done(FromResult(R)); });
	}

	void DescribeRunPipeline(const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		ZettaPipelines::FRunRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseRun(In, Request, Error))
		{
			FZettaToolPreview P;
			P.Title = TEXT("Run a pipeline");
			P.Error = Error;
			return Done(P);
		}
		ZettaPipelines::DescribeRun(Request, [Done](const ZettaPipelines::FApproval& A) { Done(FromApproval(A)); });
	}

	void RunPipeline(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		ZettaPipelines::FRunRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseRun(In, Request, Error)) return Done(FZettaToolResult::Error(Error));
		// The run's end comes back to this conversation as pipeline_run_finished.
		Request.AgentSessionId = SessionId();
		ZettaPipelines::Run(Request, [Done](const ZettaPipelines::FRunResult& R)
		{
			for (const int64 RunId : R.RunIds) ZettaAgentSession::FollowPipelineRun(RunId);
			Done(FromResult(R));
		});
	}

	void RunStatus(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		int64 Id = 0;
		FString Error;
		if (!ZettaPipelines::ParseId(In, TEXT("runId"), Id, Error)) return Done(FZettaToolResult::Error(Error));
		ZettaPipelines::RunStatus(Id, [Done](const ZettaPipelines::FRunStatus& R) { Done(FromResult(R)); });
	}

	void DescribeManageRun(const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		ZettaPipelines::FManageRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseManage(In, Request, Error))
		{
			FZettaToolPreview P;
			P.Title = TEXT("Manage a pipeline run");
			P.Error = Error;
			return Done(P);
		}
		const TSharedPtr<FJsonObject> Input = In;
		ZettaPipelines::DescribeManage(Request, [Done, Input](const ZettaPipelines::FApproval& A)
		{
			if (A.bNeeded || !A.Error.IsEmpty()) return Done(FromApproval(A));
			DescribeWrite(TEXT("manage_pipeline_run"), Input, Done);
		});
	}

	void ManageRun(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		ZettaPipelines::FManageRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseManage(In, Request, Error)) return Done(FZettaToolResult::Error(Error));
		Request.AgentSessionId = SessionId();
		const int64 RunId = Request.RunId;
		ZettaPipelines::Manage(Request, [Done, RunId](const ZettaPipelines::FRunStatus& R)
		{
			if (R.bOk && R.bLive) ZettaAgentSession::FollowPipelineRun(RunId);
			Done(FromResult(R));
		});
	}

	void ImportResults(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		ZettaPipelines::FImportRequest Request;
		FString Error;
		if (!ZettaPipelines::ParseImport(In, Request, Error)) return Done(FZettaToolResult::Error(Error));

		// place { parent?, position? } is the agent's to resolve: a target in the open level and a world location.
		if (const TSharedPtr<FJsonObject> Place = ZettaTool::Obj(In, TEXT("place")))
		{
			Request.bPlace = true;
			Request.Place.bPlace = true;
			const FString Parent = ZettaTool::Str(Place, TEXT("parent"));
			if (!Parent.IsEmpty())
			{
				AActor* Actor = ZettaTargets::FindActor(Parent, Error);
				if (!Actor) return Done(FZettaToolResult::Error(Error));
				Request.Place.Parent = Actor;
			}
			TOptional<FVector> Location;
			if (!ZettaTool::OptVector(Place, TEXT("position"), Location, Error)) return Done(FZettaToolResult::Error(Error));
			if (!Location.IsSet() && !ZettaTool::OptVector(Place, TEXT("location"), Location, Error)) return Done(FZettaToolResult::Error(Error));
			Request.Place.Location = Location;
		}

		ZettaPipelines::ImportResults(Request, [Done](const ZettaPipelines::FImportResult& R)
		{
			for (const FString& Path : R.AssetPaths) ZettaAgentChanges::CreatedAsset(Path);
			Done(FromResult(R));
		});
	}
}

namespace ZettaToolAreas
{
	void RegisterPipelineTools(FZettaAgentTools& Tools)
	{
		using namespace ZettaPipelineToolsImpl;
		auto Write = [](const TCHAR* Name)
		{
			const FString Tool = Name;
			return [Tool](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done) { DescribeWrite(Tool, In, MoveTemp(Done)); };
		};
		Tools.RegisterAsync(TEXT("list_pipelines"), &ListPipelines);
		Tools.RegisterAsync(TEXT("get_pipeline"), &GetPipeline);
		Tools.RegisterAsync(TEXT("build_pipeline"), &BuildPipeline, Write(TEXT("build_pipeline")));
		Tools.RegisterAsync(TEXT("edit_pipeline"), &EditPipeline, Write(TEXT("edit_pipeline")));
		Tools.RegisterAsync(TEXT("run_pipeline"), &RunPipeline, &DescribeRunPipeline);
		Tools.RegisterAsync(TEXT("pipeline_run_status"), &RunStatus);
		Tools.RegisterAsync(TEXT("manage_pipeline_run"), &ManageRun, &DescribeManageRun);
		Tools.RegisterAsync(TEXT("import_pipeline_results"), &ImportResults, Write(TEXT("import_pipeline_results")));
	}
}
