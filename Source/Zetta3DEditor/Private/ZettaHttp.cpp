// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaHttp.h"

#include "Async/Async.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "ZettaAuth.h"
#include "ZettaConfig.h"
#include "ZettaJson.h"
#include "Zetta3DEditorModule.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

FSimpleMulticastDelegate FZettaHttp::OnUnauthorized;

/** Above this, an upload goes up in pieces: the edge in front of the API ends any request after 100s. */
static constexpr int64 UploadPieceBytes = 8 * 1024 * 1024;

TSharedPtr<FJsonObject> FZettaResponse::Object() const
{
	return Json.IsValid() && Json->Type == EJson::Object ? Json->AsObject() : nullptr;
}

FString FZettaHttp::Url(const FString& Path)
{
	if (Path.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || Path.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)) return Path;
	return ZettaConfig::ApiBase() + Path;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> FZettaHttp::Build(const FString& Verb, const FString& Url, bool bAuth)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(Url);
	Request->SetHeader(TEXT("User-Agent"), FString::Printf(TEXT("Zetta3D-Unreal/%s"), *ZettaConfig::Version()));
	Request->SetHeader(TEXT("X-Zetta-Client"), ZettaConfig::ClientName);
	Request->SetHeader(TEXT("X-Zetta-Client-Version"), ZettaConfig::Version());
	Request->SetHeader(TEXT("X-Zetta-Host-Version"), ZettaConfig::EngineVersion());
	if (bAuth)
	{
		const FString Token = FZettaAuth::Get().GetToken();
		if (!Token.IsEmpty()) Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	}
	return Request;
}

FString FZettaHttp::DefaultMessage(int32 Status)
{
	switch (Status)
	{
	case 0: return TEXT("Could not reach Zetta 3D. Check your connection and try again.");
	case 401: return TEXT("Sign in to Zetta 3D to carry on.");
	case 403: return TEXT("Your plan does not include this.");
	case 404: return TEXT("Not found.");
	case 413: return TEXT("That is too large to send.");
	case 429: return TEXT("Too many requests — wait a moment and try again.");
	default:
		return Status >= 500 ? TEXT("Zetta 3D had a problem. Try again in a moment.") : FString::Printf(TEXT("Request failed (%d)."), Status);
	}
}

FZettaResponse FZettaHttp::Read(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected, bool bIsApi)
{
	FZettaResponse Out;
	if (Request.IsValid() && Request->GetStatus() == EHttpRequestStatus::Failed && !bConnected)
	{
		Out.bCancelled = Request->GetFailureReason() == EHttpFailureReason::Cancelled;
	}
	if (!bConnected || !Response.IsValid())
	{
		Out.Status = 0;
		Out.Message = Out.bCancelled ? TEXT("Cancelled.") : DefaultMessage(0);
		Out.Code = Out.bCancelled ? TEXT("CANCELLED") : TEXT("NETWORK");
		return Out;
	}

	Out.Status = Response->GetResponseCode();
	Out.Text = Response->GetContentAsString();
	Out.Json = Out.Text.IsEmpty() ? nullptr : ZettaJson::ParseValue(Out.Text);
	Out.bOk = EHttpResponseCodes::IsOk(Out.Status);
	if (Out.bOk) return Out;

	const TSharedPtr<FJsonObject> Body = Out.Object();
	if (Body.IsValid())
	{
		const TSharedPtr<FJsonValue> Message = Body->TryGetField(TEXT("message"));
		if (Message.IsValid() && Message->Type == EJson::Array)
		{
			TArray<FString> Lines;
			for (const TSharedPtr<FJsonValue>& Line : Message->AsArray()) Lines.Add(ZettaJson::AsText(Line));
			Out.Message = FString::Join(Lines, TEXT(" "));
		}
		else if (Message.IsValid() && Message->Type == EJson::String)
		{
			Out.Message = Message->AsString();
		}
		Out.Code = ZettaJson::Str(Body, TEXT("code"));
	}
	if (Out.Message.IsEmpty()) Out.Message = DefaultMessage(Out.Status);
	if (Out.Status == 401 && bIsApi) OnUnauthorized.Broadcast();
	return Out;
}

FHttpRequestPtr FZettaHttp::Send(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done,
	const TMap<FString, FString>& Headers, float TimeoutSeconds)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Build(Verb, Url(Path), true);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (Body.IsValid())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(ZettaJson::Write(Body));
	}
	for (const TPair<FString, FString>& Pair : Headers) Request->SetHeader(Pair.Key, Pair.Value);
	Request->SetTimeout(TimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
	{
		const FZettaResponse Response = Read(Req, Res, bConnected, true);
		if (Done) Done(Response);
	});
	Request->ProcessRequest();
	return Request;
}

FHttpRequestPtr FZettaHttp::Get(const FString& Path, FZettaCallback Done, float TimeoutSeconds)
{
	return Send(TEXT("GET"), Path, nullptr, MoveTemp(Done), {}, TimeoutSeconds);
}

FHttpRequestPtr FZettaHttp::Post(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done,
	const TMap<FString, FString>& Headers, float TimeoutSeconds)
{
	return Send(TEXT("POST"), Path, Body.IsValid() ? Body : MakeShared<FJsonObject>(), MoveTemp(Done), Headers, TimeoutSeconds);
}

FHttpRequestPtr FZettaHttp::Patch(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done)
{
	return Send(TEXT("PATCH"), Path, Body, MoveTemp(Done));
}

FHttpRequestPtr FZettaHttp::Put(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done)
{
	return Send(TEXT("PUT"), Path, Body, MoveTemp(Done));
}

FHttpRequestPtr FZettaHttp::Delete(const FString& Path, FZettaCallback Done)
{
	return Send(TEXT("DELETE"), Path, nullptr, MoveTemp(Done));
}

// ─── Uploads ──────────────────────────────────────────────────────────────────

namespace
{
	FString QueryValue(const FString& Url, const FString& Key)
	{
		int32 Q = INDEX_NONE;
		if (!Url.FindChar(TEXT('?'), Q)) return FString();
		TArray<FString> Pairs;
		Url.Mid(Q + 1).ParseIntoArray(Pairs, TEXT("&"));
		for (const FString& Pair : Pairs)
		{
			FString K, V;
			if (Pair.Split(TEXT("="), &K, &V) && K == Key) return FGenericPlatformHttp::UrlDecode(V);
		}
		return FString();
	}

	FZettaResponse UploadResult(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
	{
		FZettaResponse R = FZettaHttp::Read(Req, Res, bConnected, false);
		if (R.Status == 410)
		{
			R.Code = TEXT("UPLOAD_TICKET_EXPIRED");
			R.Message = TEXT("The upload link expired.");
		}
		else if (R.Status == 413)
		{
			R.Message = TEXT("That file is too large to upload (pictures up to 32 MB, models up to 512 MB).");
		}
		return R;
	}

	/** One request, retried twice on a dropped connection or a 5xx. */
	void SendWithRetry(TFunction<TSharedRef<IHttpRequest, ESPMode::ThreadSafe>()> Make, FZettaCallback Done, int32 Attempt = 0)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Make();
		Request->OnProcessRequestComplete().BindLambda([Make, Done, Attempt](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
		{
			FZettaResponse R = UploadResult(Req, Res, bConnected);
			const bool bRetry = !R.bOk && !R.bCancelled && (R.Status == 0 || R.Status >= 500) && Attempt < 2;
			if (!bRetry)
			{
				Done(R);
				return;
			}
			const float Delay = Attempt == 0 ? 0.6f : 1.8f;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Make, Done, Attempt](float)
			{
				SendWithRetry(Make, Done, Attempt + 1);
				return false;
			}), Delay);
		});
		Request->ProcessRequest();
	}
}

void FZettaHttp::PutBytes(const FString& InUrl, const TArray<uint8>& Bytes, const TMap<FString, FString>& Headers, FZettaCallback Done,
	FZettaProgress Progress)
{
	const FString TargetUrl = Url(InUrl);
	const FString ContentType = Headers.Contains(TEXT("Content-Type")) ? Headers[TEXT("Content-Type")] : TEXT("application/octet-stream");
	const FString Ticket = QueryValue(TargetUrl, TEXT("ticket"));
	const TSharedRef<TArray<uint8>> Data = MakeShared<TArray<uint8>>(Bytes);

	auto MakePut = [ContentType, Headers](const FString& PutUrl, TSharedRef<TArray<uint8>> Payload, int64 Offset, int64 Length, bool bWithHeaders)
	{
		return [=]()
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Build(TEXT("PUT"), PutUrl, false);
			Request->SetHeader(TEXT("Content-Type"), ContentType);
			if (bWithHeaders)
			{
				for (const TPair<FString, FString>& Pair : Headers) Request->SetHeader(Pair.Key, Pair.Value);
			}
			TArray<uint8> Slice;
			Slice.Append(Payload->GetData() + Offset, Length);
			Request->SetContent(MoveTemp(Slice));
			Request->SetTimeout(300.f);
			return Request;
		};
	};

	if (Bytes.Num() <= UploadPieceBytes || Ticket.IsEmpty())
	{
		SendWithRetry(MakePut(TargetUrl, Data, 0, Data->Num(), true), [Done, Progress](const FZettaResponse& R)
		{
			if (R.bOk && Progress) Progress(1.f);
			Done(R);
		});
		return;
	}

	// The relay's multipart protocol: start → 8 MB parts → complete (aborted on failure).
	int32 Q = INDEX_NONE;
	TargetUrl.FindChar(TEXT('?'), Q);
	const FString BaseUrl = TargetUrl.Left(Q);
	const FString T = FGenericPlatformHttp::UrlEncode(Ticket);
	auto MakePost = [](const FString& PostUrl)
	{
		return [PostUrl]()
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Build(TEXT("POST"), PostUrl, false);
			Request->SetTimeout(120.f);
			return Request;
		};
	};

	SendWithRetry(MakePost(FString::Printf(TEXT("%s/multipart?ticket=%s"), *BaseUrl, *T)),
		[=](const FZettaResponse& Start)
		{
			if (!Start.bOk)
			{
				Done(Start);
				return;
			}
			const FString UploadId = FGenericPlatformHttp::UrlEncode(ZettaJson::Str(Start.Object(), TEXT("uploadId")));
			const int32 Parts = (int32)((Data->Num() + UploadPieceBytes - 1) / UploadPieceBytes);
			auto Abort = [=]()
			{
				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
					Build(TEXT("DELETE"), FString::Printf(TEXT("%s/multipart?ticket=%s&uploadId=%s"), *BaseUrl, *T, *UploadId), false);
				Request->ProcessRequest();
			};
			TSharedRef<TFunction<void(int32)>> SendPart = MakeShared<TFunction<void(int32)>>();
			*SendPart = [=](int32 Part)
			{
				if (Part > Parts)
				{
					SendWithRetry(MakePost(FString::Printf(TEXT("%s/multipart/complete?ticket=%s&uploadId=%s&parts=%d&bytes=%d"),
						*BaseUrl, *T, *UploadId, Parts, Data->Num())), [=](const FZettaResponse& Complete)
					{
						if (!Complete.bOk) Abort();
						Done(Complete);
						*SendPart = nullptr; // break the cycle
					});
					return;
				}
				const int64 Offset = (int64)(Part - 1) * UploadPieceBytes;
				const int64 Length = FMath::Min<int64>(UploadPieceBytes, Data->Num() - Offset);
				const FString PartUrl = FString::Printf(TEXT("%s/multipart/part?ticket=%s&uploadId=%s&part=%d"), *BaseUrl, *T, *UploadId, Part);
				SendWithRetry(MakePut(PartUrl, Data, Offset, Length, false), [=](const FZettaResponse& R)
				{
					if (!R.bOk)
					{
						Abort();
						Done(R);
						*SendPart = nullptr;
						return;
					}
					if (Progress) Progress((float)Part / Parts);
					(*SendPart)(Part + 1);
				});
			};
			(*SendPart)(1);
		});
}

FHttpRequestPtr FZettaHttp::Download(const FString& InUrl, FZettaCallback Done, FZettaProgress Progress, float TimeoutSeconds)
{
	const FString Target = Url(InUrl);
	// Our own API needs the session; signed storage links must not get it.
	const bool bApi = Target.StartsWith(ZettaConfig::ApiBase());
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Build(TEXT("GET"), Target, bApi);
	Request->SetTimeout(TimeoutSeconds);
	if (Progress)
	{
		Request->OnRequestProgress64().BindLambda([Progress](FHttpRequestPtr Req, uint64 /*Sent*/, uint64 Received)
		{
			const FHttpResponsePtr Res = Req->GetResponse();
			const int64 Total = Res.IsValid() ? Res->GetContentLength() : 0;
			if (Total > 0) Progress(FMath::Clamp((float)Received / Total, 0.f, 1.f));
		});
	}
	Request->OnProcessRequestComplete().BindLambda([Done, bApi](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
	{
		FZettaResponse R;
		R.Status = Res.IsValid() ? Res->GetResponseCode() : 0;
		R.bOk = bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(R.Status);
		if (R.bOk)
		{
			R.Bytes = Res->GetContent();
		}
		else
		{
			R = Read(Req, Res, bConnected, bApi);
			if (R.Status == 403 || R.Status == 410)
			{
				R.Code = TEXT("DOWNLOAD_EXPIRED");
				R.Message = TEXT("The download link expired. Try again.");
			}
			else if (R.Code.IsEmpty())
			{
				R.Code = TEXT("DOWNLOAD_FAILED");
			}
		}
		Done(R);
	});
	Request->ProcessRequest();
	return Request;
}

// ─── Server-sent events ───────────────────────────────────────────────────────

void FZettaStream::Cancel()
{
	{
		FScopeLock Guard(&Lock);
		if (bDone) return;
		bCancelled = true;
	}
	if (Request.IsValid()) Request->CancelRequest();
}

void FZettaStream::Feed(const uint8* Data, int64 Length)
{
	bool bQueued = false;
	{
		FScopeLock Guard(&Lock);
		if (Everything.Num() < 64 * 1024) Everything.Append(Data, FMath::Min<int64>(Length, 64 * 1024 - Everything.Num()));
		for (int64 i = 0; i < Length; ++i)
		{
			const uint8 Byte = Data[i];
			if (Byte != '\n')
			{
				LineBuffer.Add(Byte);
				continue;
			}
			if (LineBuffer.Num() > 0 && LineBuffer.Last() == '\r') LineBuffer.Pop();
			const FUTF8ToTCHAR Line((const ANSICHAR*)LineBuffer.GetData(), LineBuffer.Num());
			FString Text(Line.Length(), Line.Get());
			LineBuffer.Reset();
			if (Text.StartsWith(TEXT("data:")))
			{
				Queue.Add(Text.Mid(5).TrimStart());
				bQueued = true;
			}
		}
	}
	if (bQueued)
	{
		TWeakPtr<FZettaStream, ESPMode::ThreadSafe> Weak = AsShared();
		AsyncTask(ENamedThreads::GameThread, [Weak]()
		{
			if (const TSharedPtr<FZettaStream, ESPMode::ThreadSafe> Self = Weak.Pin()) Self->Pump();
		});
	}
}

void FZettaStream::Pump()
{
	check(IsInGameThread());
	TArray<FString> Lines;
	{
		FScopeLock Guard(&Lock);
		Lines = MoveTemp(Queue);
		Queue.Reset();
	}
	for (const FString& Line : Lines)
	{
		const TSharedPtr<FJsonObject> Event = ZettaJson::Parse(Line);
		if (Event.IsValid() && OnEvent && !bCancelled) OnEvent(Event);
	}
}

void FZettaStream::Finish(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
{
	Pump();
	FZettaResponse R;
	R.Status = Res.IsValid() ? Res->GetResponseCode() : 0;
	R.bOk = bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(R.Status);
	if (!R.bOk)
	{
		R = FZettaHttp::Read(Req, Res, bConnected, true);
		// The streamed body never reaches GetContent: the error is in what was fed to us.
		FString Body;
		{
			FScopeLock Guard(&Lock);
			const FUTF8ToTCHAR Text((const ANSICHAR*)Everything.GetData(), Everything.Num());
			Body = FString(Text.Length(), Text.Get());
		}
		if (const TSharedPtr<FJsonObject> Json = ZettaJson::Parse(Body))
		{
			R.Json = MakeShared<FJsonValueObject>(Json);
			const FString Message = ZettaJson::Str(Json, TEXT("message"));
			if (!Message.IsEmpty()) R.Message = Message;
			const TArray<FString> Lines = ZettaJson::Strings(Json, TEXT("message"));
			if (Lines.Num() > 0) R.Message = FString::Join(Lines, TEXT(" "));
			R.Code = ZettaJson::Str(Json, TEXT("code"), R.Code);
		}
	}
	R.bCancelled = R.bCancelled || bCancelled;
	bDone = true;
	if (OnDone) OnDone(R);
	OnEvent = nullptr;
	OnDone = nullptr;
	Request.Reset(); // the request's delegate holds this stream: let both go
}

TSharedRef<FZettaStream, ESPMode::ThreadSafe> FZettaHttp::Stream(const FString& Path, const TSharedPtr<FJsonObject>& Body,
	TFunction<void(const TSharedPtr<FJsonObject>&)> OnEvent, FZettaCallback OnDone, const TMap<FString, FString>& Headers)
{
	TSharedRef<FZettaStream, ESPMode::ThreadSafe> Stream = MakeShared<FZettaStream, ESPMode::ThreadSafe>();
	Stream->OnEvent = MoveTemp(OnEvent);
	Stream->OnDone = MoveTemp(OnDone);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Build(TEXT("POST"), Url(Path), true);
	Request->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	for (const TPair<FString, FString>& Pair : Headers) Request->SetHeader(Pair.Key, Pair.Value);
	Request->SetContentAsString(ZettaJson::Write(Body));
	// A model can think for minutes between frames: only a long silence ends the stream.
	Request->SetActivityTimeout(600.f);
	Request->SetTimeout(3600.f);

	TWeakPtr<FZettaStream, ESPMode::ThreadSafe> Weak = Stream;
	Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Weak](void* Ptr, int64& Length)
	{
		if (const TSharedPtr<FZettaStream, ESPMode::ThreadSafe> Self = Weak.Pin()) Self->Feed((const uint8*)Ptr, Length);
	}));
	Request->OnProcessRequestComplete().BindLambda([Stream](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
	{
		Stream->Finish(Req, Res, bConnected);
	});
	Stream->Request = Request;
	Request->ProcessRequest();
	return Stream;
}

#undef LOCTEXT_NAMESPACE
