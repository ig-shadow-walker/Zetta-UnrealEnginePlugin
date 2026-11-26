// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"

/** What a call to the API came back with: the parsed body on success, or the error the API answered with. */
struct ZETTA3DEDITOR_API FZettaResponse
{
	bool bOk = false;
	/** 0 when the request never reached the server (offline, cancelled, timed out). */
	int32 Status = 0;
	FString Text;
	TSharedPtr<FJsonValue> Json;
	/** Raw body of a download. */
	TArray<uint8> Bytes;
	/** The API's machine-readable error code (INSUFFICIENT_CREDITS, JOB_LIMIT_REACHED, …). */
	FString Code;
	/** One line, safe to show the user. */
	FString Message;
	bool bCancelled = false;

	TSharedPtr<FJsonObject> Object() const;
	bool IsUnauthorized() const { return Status == 401; }
};

using FZettaCallback = TFunction<void(const FZettaResponse&)>;
using FZettaProgress = TFunction<void(float)>;

/** A server-sent-event stream in flight (the agent's chat). Events arrive on the game thread, in order. */
class ZETTA3DEDITOR_API FZettaStream : public TSharedFromThis<FZettaStream, ESPMode::ThreadSafe>
{
public:
	void Cancel();
	bool IsDone() const { return bDone; }

private:
	friend class FZettaHttp;
	void Feed(const uint8* Data, int64 Length);
	void Pump();
	void Finish(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected);

	FHttpRequestPtr Request;
	TFunction<void(const TSharedPtr<FJsonObject>&)> OnEvent;
	FZettaCallback OnDone;

	FCriticalSection Lock;
	TArray<uint8> LineBuffer;
	TArray<uint8> Everything;
	TArray<FString> Queue;
	bool bDone = false;
	bool bCancelled = false;
};

/**
 * The HTTP client for the Zetta 3D API. Every request names the plugin (X-Zetta-Client: unreal) and carries
 * the session; callbacks run on the game thread. Bodies are built with only the fields that are set — the API
 * rejects unknown fields.
 */
class ZETTA3DEDITOR_API FZettaHttp
{
public:
	static FString Url(const FString& Path);

	static FHttpRequestPtr Send(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done,
		const TMap<FString, FString>& Headers = {}, float TimeoutSeconds = 60.f);
	static FHttpRequestPtr Get(const FString& Path, FZettaCallback Done, float TimeoutSeconds = 60.f);
	static FHttpRequestPtr Post(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done,
		const TMap<FString, FString>& Headers = {}, float TimeoutSeconds = 60.f);
	static FHttpRequestPtr Patch(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done);
	static FHttpRequestPtr Put(const FString& Path, const TSharedPtr<FJsonObject>& Body, FZettaCallback Done);
	static FHttpRequestPtr Delete(const FString& Path, FZettaCallback Done);

	/**
	 * Uploads bytes to an upload ticket's URL (the API's relay). The ticket authorises it, so no session is sent.
	 * Up to 8 MB is one PUT; larger files go up in 8 MB parts. A 410 comes back as UPLOAD_TICKET_EXPIRED.
	 */
	static void PutBytes(const FString& Url, const TArray<uint8>& Bytes, const TMap<FString, FString>& Headers, FZettaCallback Done,
		FZettaProgress Progress = nullptr);

	/** Downloads a file (signed URL, no session) into Response.Bytes. */
	static FHttpRequestPtr Download(const FString& Url, FZettaCallback Done, FZettaProgress Progress = nullptr, float TimeoutSeconds = 600.f);

	/** POSTs and reads a text/event-stream of `data: {json}` frames. */
	static TSharedRef<FZettaStream, ESPMode::ThreadSafe> Stream(const FString& Path, const TSharedPtr<FJsonObject>& Body,
		TFunction<void(const TSharedPtr<FJsonObject>&)> OnEvent, FZettaCallback OnDone, const TMap<FString, FString>& Headers = {});

	/** Raised whenever the API says the session is no good. */
	static FSimpleMulticastDelegate OnUnauthorized;

	/** Builds the response for a finished request, error message included. */
	static FZettaResponse Read(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected, bool bIsApi);
	static FString DefaultMessage(int32 Status);

private:
	static TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Build(const FString& Verb, const FString& Url, bool bAuth);
};
