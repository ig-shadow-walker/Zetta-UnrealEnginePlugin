// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaImageCache.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "ZettaHttp.h"

FZettaImageCache& FZettaImageCache::Get()
{
	static FZettaImageCache Instance;
	return Instance;
}

bool FZettaImageCache::Decode(const TArray<uint8>& Bytes, TArray<uint8>& OutBgra, int32& OutWidth, int32& OutHeight)
{
	if (Bytes.Num() < 8) return false;
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (Format == EImageFormat::Invalid) return false;
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num())) return false;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutBgra)) return false;
	OutWidth = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();
	return OutWidth > 0 && OutHeight > 0;
}

TSharedPtr<FSlateBrush> FZettaImageCache::FromBytes(const FString& Key, const TArray<uint8>& Bytes)
{
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Key)) return *Found;
	TArray<uint8> Bgra;
	int32 W = 0, H = 0;
	if (!Decode(Bytes, Bgra, W, H)) return nullptr;
	const FName Name(*FString::Printf(TEXT("ZettaImage_%s"), *FMD5::HashAnsiString(*Key)));
	TSharedPtr<FSlateDynamicImageBrush> Brush = FSlateDynamicImageBrush::CreateWithImageData(Name, FVector2D(W, H), Bgra);
	if (!Brush.IsValid()) return nullptr;
	Brushes.Add(Key, Brush);
	return Brush;
}

TSharedPtr<FSlateBrush> FZettaImageCache::Find(const FString& Url)
{
	const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Url);
	return Found ? *Found : nullptr;
}

void FZettaImageCache::Request(const FString& Url, TFunction<void(TSharedPtr<FSlateBrush>)> OnLoaded)
{
	if (Url.IsEmpty() || Failed.Contains(Url))
	{
		if (OnLoaded) OnLoaded(nullptr);
		return;
	}
	if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Url))
	{
		if (OnLoaded) OnLoaded(*Found);
		return;
	}
	const bool bInFlight = Waiting.Contains(Url);
	TArray<TFunction<void(TSharedPtr<FSlateBrush>)>>& Queue = Waiting.FindOrAdd(Url);
	if (OnLoaded) Queue.Add(MoveTemp(OnLoaded));
	if (bInFlight) return;

	FZettaHttp::Download(Url, [this, Url](const FZettaResponse& R)
	{
		TSharedPtr<FSlateBrush> Brush = R.bOk ? FromBytes(Url, R.Bytes) : nullptr;
		if (!Brush.IsValid()) Failed.Add(Url);
		TArray<TFunction<void(TSharedPtr<FSlateBrush>)>> Callbacks;
		Waiting.RemoveAndCopyValue(Url, Callbacks);
		for (const TFunction<void(TSharedPtr<FSlateBrush>)>& Callback : Callbacks) Callback(Brush);
	}, nullptr, 60.f);
}

void FZettaImageCache::Clear()
{
	Brushes.Reset();
	Failed.Reset();
}
