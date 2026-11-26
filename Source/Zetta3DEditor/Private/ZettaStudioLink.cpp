// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudioLink.h"

#include "Zetta3DEditorModule.h"

namespace ZettaStudioLink
{
	static TOptional<FRequest> Pending;

	FOnRequest& OnRequest()
	{
		static FOnRequest Delegate;
		return Delegate;
	}

	static void Post(const FRequest& Request)
	{
		Pending = Request;
		FZetta3DEditorModule::Get().OpenWindow(TEXT("Studio"));
		OnRequest().Broadcast();
	}

	void OpenTool(FName Tool, const FZettaGenerationPtr& Source) { Post({ Tool, Source, false }); }

	void Fork(const FZettaGenerationPtr& Generation)
	{
		const bool bSmart = Generation.IsValid() && (Generation->Model == TEXT("np1") || Generation->Model == TEXT("np15"));
		Post({ bSmart ? FName(TEXT("Smart")) : FName(TEXT("Generate")), Generation, true });
	}

	TOptional<FRequest> TakePending()
	{
		TOptional<FRequest> Out = Pending;
		Pending.Reset();
		return Out;
	}
}
