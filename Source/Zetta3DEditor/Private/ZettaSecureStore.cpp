// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaSecureStore.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

static const TCHAR* ServiceName = TEXT("ai.zetta3d.unreal");

#if PLATFORM_MAC
#include "Apple/PreAppleSystemHeaders.h"
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include "Apple/PostAppleSystemHeaders.h"

namespace
{
	CFStringRef ToCF(const FString& S)
	{
		const FTCHARToUTF8 Utf8(*S);
		return CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)Utf8.Get(), Utf8.Length(), kCFStringEncodingUTF8, false);
	}

	CFMutableDictionaryRef Query(const FString& Name)
	{
		CFMutableDictionaryRef Q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFStringRef Service = ToCF(ServiceName);
		CFStringRef Account = ToCF(Name);
		CFDictionarySetValue(Q, kSecClass, kSecClassGenericPassword);
		CFDictionarySetValue(Q, kSecAttrService, Service);
		CFDictionarySetValue(Q, kSecAttrAccount, Account);
		CFRelease(Service);
		CFRelease(Account);
		return Q;
	}
}

bool ZettaSecureStore::Set(const FString& Name, const FString& Secret)
{
	if (Secret.IsEmpty()) return Remove(Name);
	const FTCHARToUTF8 Utf8(*Secret);
	CFDataRef Data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)Utf8.Get(), Utf8.Length());

	CFMutableDictionaryRef Q = Query(Name);
	CFMutableDictionaryRef Update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(Update, kSecValueData, Data);
	OSStatus Status = SecItemUpdate(Q, Update);
	if (Status == errSecItemNotFound)
	{
		CFDictionarySetValue(Q, kSecValueData, Data);
		CFDictionarySetValue(Q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
		Status = SecItemAdd(Q, nullptr);
	}
	CFRelease(Update);
	CFRelease(Q);
	CFRelease(Data);
	return Status == errSecSuccess;
}

FString ZettaSecureStore::Get(const FString& Name)
{
	CFMutableDictionaryRef Q = Query(Name);
	CFDictionarySetValue(Q, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(Q, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef Result = nullptr;
	const OSStatus Status = SecItemCopyMatching(Q, &Result);
	CFRelease(Q);
	if (Status != errSecSuccess || Result == nullptr) return FString();
	CFDataRef Data = (CFDataRef)Result;
	const FUTF8ToTCHAR Text((const ANSICHAR*)CFDataGetBytePtr(Data), (int32)CFDataGetLength(Data));
	FString Out(Text.Length(), Text.Get());
	CFRelease(Result);
	return Out;
}

bool ZettaSecureStore::Remove(const FString& Name)
{
	CFMutableDictionaryRef Q = Query(Name);
	const OSStatus Status = SecItemDelete(Q);
	CFRelease(Q);
	return Status == errSecSuccess || Status == errSecItemNotFound;
}

FText ZettaSecureStore::Describe() { return LOCTEXT("StoreMac", "the macOS Keychain"); }

#elif PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"

static FString Target(const FString& Name) { return FString::Printf(TEXT("%s/%s"), ServiceName, *Name); }

bool ZettaSecureStore::Set(const FString& Name, const FString& Secret)
{
	if (Secret.IsEmpty()) return Remove(Name);
	const FTCHARToUTF8 Utf8(*Secret);
	FString TargetName = Target(Name);
	CREDENTIALW Cred = {};
	Cred.Type = CRED_TYPE_GENERIC;
	Cred.TargetName = (LPWSTR)*TargetName;
	Cred.CredentialBlobSize = (DWORD)Utf8.Length();
	Cred.CredentialBlob = (LPBYTE)Utf8.Get();
	Cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	Cred.UserName = (LPWSTR)TEXT("Zetta 3D");
	return CredWriteW(&Cred, 0) == TRUE;
}

FString ZettaSecureStore::Get(const FString& Name)
{
	PCREDENTIALW Cred = nullptr;
	FString TargetName = Target(Name);
	if (!CredReadW(*TargetName, CRED_TYPE_GENERIC, 0, &Cred) || Cred == nullptr) return FString();
	const FUTF8ToTCHAR Text((const ANSICHAR*)Cred->CredentialBlob, (int32)Cred->CredentialBlobSize);
	FString Out(Text.Length(), Text.Get());
	CredFree(Cred);
	return Out;
}

bool ZettaSecureStore::Remove(const FString& Name)
{
	FString TargetName = Target(Name);
	return CredDeleteW(*TargetName, CRED_TYPE_GENERIC, 0) == TRUE || GetLastError() == ERROR_NOT_FOUND;
}

FText ZettaSecureStore::Describe() { return LOCTEXT("StoreWin", "Windows Credential Manager"); }

#else
#include <sys/stat.h>

static FString SecretPath(const FString& Name)
{
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Zetta3D"), TEXT("secrets"), Name + TEXT(".key"));
}

bool ZettaSecureStore::Set(const FString& Name, const FString& Secret)
{
	if (Secret.IsEmpty()) return Remove(Name);
	const FString Path = SecretPath(Name);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Secret, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
	chmod(TCHAR_TO_UTF8(*Path), S_IRUSR | S_IWUSR);
	chmod(TCHAR_TO_UTF8(*FPaths::GetPath(Path)), S_IRWXU);
	return true;
}

FString ZettaSecureStore::Get(const FString& Name)
{
	FString Out;
	FFileHelper::LoadFileToString(Out, *SecretPath(Name));
	return Out.TrimStartAndEnd();
}

bool ZettaSecureStore::Remove(const FString& Name)
{
	const FString Path = SecretPath(Name);
	return !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path);
}

FText ZettaSecureStore::Describe() { return LOCTEXT("StoreLinux", "a private file in your user folder"); }
#endif

#undef LOCTEXT_NAMESPACE
