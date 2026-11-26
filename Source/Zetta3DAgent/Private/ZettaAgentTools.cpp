// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentTools.h"

#include "ZettaJson.h"

void IZettaAgentTool::Describe(const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done)
{
	FZettaToolPreview Preview;
	Preview.Title = GetName().ToString();
	Preview.Body = ZettaJson::Write(Input, true).Left(2000);
	Done(Preview);
}

namespace
{
	class FLambdaTool final : public IZettaAgentTool
	{
	public:
		FName Name;
		TFunction<void(const TSharedPtr<FJsonObject>&, FZettaToolDone)> RunFn;
		TFunction<void(const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)>)> DescribeFn;

		virtual FName GetName() const override { return Name; }
		virtual void Run(const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done) override { RunFn(Input, MoveTemp(Done)); }
		virtual void Describe(const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done) override
		{
			if (DescribeFn) DescribeFn(Input, MoveTemp(Done));
			else IZettaAgentTool::Describe(Input, MoveTemp(Done));
		}
	};
}

FZettaAgentTools& FZettaAgentTools::Get()
{
	static FZettaAgentTools Instance;
	return Instance;
}

void FZettaAgentTools::Register(const TSharedRef<IZettaAgentTool>& Tool)
{
	Tools.Add(Tool->GetName(), Tool);
}

void FZettaAgentTools::Register(FName Name, TFunction<FZettaToolResult(const TSharedPtr<FJsonObject>&)> Run,
	TFunction<FZettaToolPreview(const TSharedPtr<FJsonObject>&)> Describe)
{
	TSharedRef<FLambdaTool> Tool = MakeShared<FLambdaTool>();
	Tool->Name = Name;
	Tool->RunFn = [Run](const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done) { Done(Run(Input)); };
	if (Describe)
	{
		Tool->DescribeFn = [Describe](const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done) { Done(Describe(Input)); };
	}
	Register(Tool);
}

void FZettaAgentTools::RegisterAsync(FName Name, TFunction<void(const TSharedPtr<FJsonObject>&, FZettaToolDone)> Run,
	TFunction<void(const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)>)> Describe)
{
	TSharedRef<FLambdaTool> Tool = MakeShared<FLambdaTool>();
	Tool->Name = Name;
	Tool->RunFn = MoveTemp(Run);
	Tool->DescribeFn = MoveTemp(Describe);
	Register(Tool);
}

bool FZettaAgentTools::Has(FName Name) const { return Tools.Contains(Name); }

TSharedPtr<IZettaAgentTool> FZettaAgentTools::Find(FName Name) const
{
	const TSharedRef<IZettaAgentTool>* Tool = Tools.Find(Name);
	return Tool ? TSharedPtr<IZettaAgentTool>(*Tool) : nullptr;
}

void FZettaAgentTools::Run(FName Name, const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done)
{
	const TSharedPtr<IZettaAgentTool> Tool = Find(Name);
	if (!Tool.IsValid())
	{
		Done(FZettaToolResult::Error(FString::Printf(TEXT("This version of the Zetta 3D plugin does not have the tool \"%s\". Carry on without it."), *Name.ToString())));
		return;
	}
	Tool->Run(Input.IsValid() ? Input : MakeShared<FJsonObject>(), MoveTemp(Done));
}

void FZettaAgentTools::Describe(FName Name, const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done)
{
	const TSharedPtr<IZettaAgentTool> Tool = Find(Name);
	if (!Tool.IsValid())
	{
		FZettaToolPreview Preview;
		Preview.Title = Name.ToString();
		Done(Preview);
		return;
	}
	Tool->Describe(Input.IsValid() ? Input : MakeShared<FJsonObject>(), MoveTemp(Done));
}

TArray<FName> FZettaAgentTools::Names() const
{
	TArray<FName> Out;
	Tools.GetKeys(Out);
	return Out;
}
