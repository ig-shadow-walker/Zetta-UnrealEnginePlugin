// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAuth.h"

#include "Async/Async.h"
#include "Common/TcpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Misc/Base64.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "ZettaApi.h"
#include "ZettaConfig.h"
#include "ZettaHttp.h"
#include "ZettaJson.h"
#include "ZettaSecureStore.h"
#include "ZettaSettings.h"
#include "Zetta3DEditorModule.h"

static const TCHAR* TokenName = TEXT("session");
static const FString ExpiredMessage = TEXT("Your Zetta 3D sign-in has expired. Sign in again.");

/**
 * A one-request web server on 127.0.0.1 for the sign-in page's POST. It answers the CORS preflight (with
 * Private Network Access, which Chrome asks for when a public page calls 127.0.0.1) and the callback, and
 * nothing else; it stops after a valid token or ten minutes.
 */
class FZettaCallbackServer : public FRunnable, public TSharedFromThis<FZettaCallbackServer, ESPMode::ThreadSafe>
{
public:
	FString State;
	FString AllowedOrigin;
	TFunction<void(const FString&)> OnToken;
	TFunction<void()> OnTimeout;

	bool Start(int32& OutPort)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets) return false;
		TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
		bool bValid = false;
		Addr->SetIp(TEXT("127.0.0.1"), bValid);
		Addr->SetPort(0);
		Listener = Sockets->CreateSocket(NAME_Stream, TEXT("Zetta3D sign-in"), Addr->GetProtocolType());
		if (!Listener) return false;
		Listener->SetReuseAddr(false);
		Listener->SetNonBlocking(true);
		if (!Listener->Bind(*Addr) || !Listener->Listen(4))
		{
			Sockets->DestroySocket(Listener);
			Listener = nullptr;
			return false;
		}
		OutPort = Listener->GetPortNo();
		Deadline = FPlatformTime::Seconds() + 600.0;
		Thread = FRunnableThread::Create(this, TEXT("Zetta3DSignIn"));
		return Thread != nullptr;
	}

	void Stop() override { bStop = true; }

	void StopAndWait()
	{
		bStop = true;
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
			Thread = nullptr;
		}
		if (Listener)
		{
			Listener->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener);
			Listener = nullptr;
		}
	}

	virtual ~FZettaCallbackServer() override { StopAndWait(); }

	uint32 Run() override
	{
		while (!bStop)
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				TFunction<void()> Timeout = OnTimeout;
				AsyncTask(ENamedThreads::GameThread, [Timeout]() { if (Timeout) Timeout(); });
				return 0;
			}
			bool bPending = false;
			if (Listener && Listener->HasPendingConnection(bPending) && bPending)
			{
				FSocket* Client = Listener->Accept(TEXT("Zetta3D sign-in client"));
				if (Client)
				{
					const bool bDone = Handle(Client);
					Client->Close();
					ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Client);
					if (bDone) return 0;
				}
			}
			FPlatformProcess::Sleep(0.05f);
		}
		return 0;
	}

private:
	static FString Header(const FString& Head, const FString& Name)
	{
		TArray<FString> Lines;
		Head.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FString K, V;
			if (Line.Split(TEXT(":"), &K, &V) && K.TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase)) return V.TrimStartAndEnd();
		}
		return FString();
	}

	static void Reply(FSocket* Client, int32 Status, const FString& StatusText, const FString& Origin, const FString& Body, bool bPreflight)
	{
		FString Response = FString::Printf(TEXT("HTTP/1.1 %d %s\r\n"), Status, *StatusText);
		Response += FString::Printf(TEXT("Access-Control-Allow-Origin: %s\r\nVary: Origin\r\n"), *Origin);
		if (bPreflight)
		{
			Response += TEXT("Access-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n");
		}
		const FTCHARToUTF8 BodyUtf8(*Body);
		Response += FString::Printf(TEXT("Content-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"), BodyUtf8.Length());
		const FTCHARToUTF8 HeadUtf8(*Response);
		int32 Sent = 0;
		Client->Send((const uint8*)HeadUtf8.Get(), HeadUtf8.Length(), Sent);
		if (BodyUtf8.Length() > 0) Client->Send((const uint8*)BodyUtf8.Get(), BodyUtf8.Length(), Sent);
	}

	/** Reads one request; returns true once a valid token has arrived. */
	bool Handle(FSocket* Client)
	{
		Client->SetNonBlocking(false);
		TArray<uint8> Data;
		const double Until = FPlatformTime::Seconds() + 5.0;
		int32 HeaderEnd = INDEX_NONE;
		int64 ContentLength = 0;
		while (FPlatformTime::Seconds() < Until && Data.Num() < 256 * 1024)
		{
			if (!Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(200))) continue;
			uint8 Buffer[4096];
			int32 Read = 0;
			if (!Client->Recv(Buffer, sizeof(Buffer), Read) || Read <= 0) break;
			Data.Append(Buffer, Read);
			if (HeaderEnd == INDEX_NONE)
			{
				for (int32 i = 3; i < Data.Num(); ++i)
				{
					if (Data[i - 3] == '\r' && Data[i - 2] == '\n' && Data[i - 1] == '\r' && Data[i] == '\n')
					{
						HeaderEnd = i + 1;
						break;
					}
				}
				if (HeaderEnd != INDEX_NONE)
				{
					const FUTF8ToTCHAR Head((const ANSICHAR*)Data.GetData(), HeaderEnd);
					ContentLength = FCString::Atoi64(*Header(FString(Head.Length(), Head.Get()), TEXT("Content-Length")));
				}
			}
			if (HeaderEnd != INDEX_NONE && Data.Num() - HeaderEnd >= ContentLength) break;
		}
		if (HeaderEnd == INDEX_NONE) return false;

		const FUTF8ToTCHAR HeadText((const ANSICHAR*)Data.GetData(), HeaderEnd);
		const FString Head(HeadText.Length(), HeadText.Get());
		FString RequestLine;
		Head.Split(TEXT("\r\n"), &RequestLine, nullptr);
		TArray<FString> Parts;
		RequestLine.ParseIntoArrayWS(Parts);
		const FString Method = Parts.Num() > 0 ? Parts[0] : FString();
		FString Path = Parts.Num() > 1 ? Parts[1] : FString();
		while (Path.EndsWith(TEXT("/"))) Path.LeftChopInline(1);
		// The page is the only caller; echo its origin rather than '*' so nothing else can read the reply.
		FString Origin = Header(Head, TEXT("Origin"));
		if (Origin.IsEmpty()) Origin = AllowedOrigin;

		if (Method == TEXT("OPTIONS"))
		{
			Reply(Client, 204, TEXT("No Content"), Origin, FString(), true);
			return false;
		}
		if (Method != TEXT("POST") || Path != TEXT("/callback"))
		{
			Reply(Client, 404, TEXT("Not Found"), Origin, TEXT("{\"ok\":false}"), false);
			return false;
		}

		const FUTF8ToTCHAR BodyText((const ANSICHAR*)Data.GetData() + HeaderEnd, Data.Num() - HeaderEnd);
		const TSharedPtr<FJsonObject> Body = ZettaJson::Parse(FString(BodyText.Length(), BodyText.Get()));
		const FString Token = ZettaJson::Str(Body, TEXT("token"));
		const FString GotState = ZettaJson::Str(Body, TEXT("state"));
		if (Token.IsEmpty() || GotState != State)
		{
			Reply(Client, 400, TEXT("Bad Request"), Origin, TEXT("{\"ok\":false,\"message\":\"State mismatch.\"}"), false);
			return false;
		}
		Reply(Client, 200, TEXT("OK"), Origin, TEXT("{\"ok\":true}"), false);
		TFunction<void(const FString&)> Callback = OnToken;
		AsyncTask(ENamedThreads::GameThread, [Callback, Token]() { if (Callback) Callback(Token); });
		return true;
	}

	FSocket* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStop { false };
	double Deadline = 0;
};

FZettaAuth& FZettaAuth::Get()
{
	static FZettaAuth Instance;
	return Instance;
}

void FZettaAuth::Initialize()
{
	UnauthorizedHandle = FZettaHttp::OnUnauthorized.AddLambda([this]()
	{
		if (State == EState::SignedIn || State == EState::Checking) SignOut(ExpiredMessage);
	});
	if (!GetToken().IsEmpty())
	{
		State = EState::Checking;
		Refresh();
	}
}

void FZettaAuth::Shutdown()
{
	FZettaHttp::OnUnauthorized.Remove(UnauthorizedHandle);
	if (Server.IsValid())
	{
		Server->StopAndWait();
		Server.Reset();
	}
}

FString FZettaAuth::GetToken() const
{
	if (!bTokenLoaded)
	{
		CachedToken = ZettaSecureStore::Get(TokenName);
		bTokenLoaded = true;
	}
	return CachedToken;
}

void FZettaAuth::Set(EState NewState)
{
	State = NewState;
	if (State == EState::SignedIn) LastError.Reset();
	OnChanged.Broadcast();
}

bool FZettaAuth::TokenExpired() const
{
	const int64 Expiry = UZettaUserSettings::Get()->TokenExpiry;
	return Expiry > 0 && FDateTime::UtcNow().ToUnixTimestamp() > Expiry;
}

void FZettaAuth::Refresh()
{
	if (GetToken().IsEmpty())
	{
		User = FZettaUser();
		Set(EState::SignedOut);
		return;
	}
	if (TokenExpired())
	{
		SignOut(ExpiredMessage);
		return;
	}
	ZettaApi::Me([this](const FZettaResponse& R, const FZettaUser& Me)
	{
		if (R.bOk)
		{
			User = Me;
			Set(EState::SignedIn);
		}
		else if (R.IsUnauthorized())
		{
			SignOut(ExpiredMessage);
		}
		else
		{
			// Offline or the API is down: keep the session and stay usable later.
			LastError = R.Message;
			Set(User.Id != 0 ? EState::SignedIn : EState::Checking);
		}
	});
}

void FZettaAuth::BeginSignIn()
{
	CancelSignIn();
	LastError.Reset();

	FGuid A = FGuid::NewGuid(), Bg = FGuid::NewGuid();
	const FString StateValue = (A.ToString(EGuidFormats::Digits) + Bg.ToString(EGuidFormats::Digits)).ToLower();

	Server = MakeShared<FZettaCallbackServer>();
	Server->State = StateValue;
	Server->AllowedOrigin = ZettaConfig::SiteBase();
	Server->OnToken = [this](const FString& Token) { Complete(Token); };
	Server->OnTimeout = [this]()
	{
		if (State == EState::WaitingForBrowser)
		{
			LastError = TEXT("Sign-in timed out. Try again.");
			CancelSignIn();
		}
	};

	int32 Port = 0;
	if (!Server->Start(Port))
	{
		Server.Reset();
		LastError = TEXT("Could not start the sign-in listener on 127.0.0.1.");
		Set(EState::SignedOut);
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/plugin-auth?port=%d&state=%s&client=%s"), *ZettaConfig::SiteBase(), Port, *StateValue, ZettaConfig::ClientName);
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	Set(EState::WaitingForBrowser);
}

void FZettaAuth::CancelSignIn()
{
	if (Server.IsValid())
	{
		TSharedPtr<FZettaCallbackServer> Old = Server;
		Server.Reset();
		// Stopping joins the listener thread; do it after this call stack so a callback it queued can't re-enter.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Old](float) mutable
		{
			Old->StopAndWait();
			Old.Reset();
			return false;
		}));
	}
	if (State == EState::WaitingForBrowser) Set(EState::SignedOut);
}

void FZettaAuth::Complete(const FString& Token)
{
	ZettaSecureStore::Set(TokenName, Token);
	CachedToken = Token;
	bTokenLoaded = true;
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	Settings->TokenExpiry = ReadExpiry(Token);
	Settings->Save();
	CancelSignIn();
	Set(EState::Checking);
	Refresh();
}

void FZettaAuth::SignOut(const FString& Reason)
{
	CancelSignIn();
	ZettaSecureStore::Remove(TokenName);
	CachedToken.Reset();
	bTokenLoaded = true;
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	Settings->TokenExpiry = 0;
	Settings->Save();
	User = FZettaUser();
	LastError = Reason;
	Set(EState::SignedOut);
}

int64 FZettaAuth::ReadExpiry(const FString& Jwt)
{
	TArray<FString> Parts;
	Jwt.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() < 2) return 0;
	FString Payload = Parts[1].Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
	while (Payload.Len() % 4 != 0) Payload += TEXT("=");
	TArray<uint8> Bytes;
	if (!FBase64::Decode(Payload, Bytes)) return 0;
	const FUTF8ToTCHAR Text((const ANSICHAR*)Bytes.GetData(), Bytes.Num());
	return ZettaJson::Int(ZettaJson::Parse(FString(Text.Length(), Text.Get())), TEXT("exp"));
}
