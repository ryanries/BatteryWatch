#pragma once

#define SERVICE_NAME		L"BatteryWatch"

#define SERVICE_NAME_LISTEN	L"BatteryWatchListener"

#define SERVICE_NAME_SERVER	L"BatteryWatchServer"

#define SERVICE_VERSION		L"1.0"

#define SERVICE_BOTH_DESC	L"Server and Listener components for the battery/UPS LAN auto-shutdown utility by Joseph Ryan Ries <ryanries09@gmail.com>"

#define SERVICE_SERVER_DESC	L"Server component of the battery/UPS LAN auto-shutdown utility by Joseph Ryan Ries <ryanries09@gmail.com>"

#define SERVICE_LISTEN_DESC	L"Listener component of the battery/UPS LAN auto-shutdown utility by Joseph Ryan Ries <ryanries09@gmail.com>"

#define LOG_LEVEL_ERROR	0

#define LOG_LEVEL_WARN	1

#define LOG_LEVEL_INFO	2

#define LOG_LEVEL_DEBUG	3

#define MAX_LOG_FILE_SIZE 524288

#define LOG_ARCHIVE_TASK_FREQUENCY_MS 61333

#define KEY_LENGTH 64

typedef struct REGISTRYPARAMETERS
{
	DWORD LogLevel;

	unsigned char ThresholdBatteryPercentage;

	DWORD GracePeriodMinutes;

	unsigned short BroadcastPort;

	char Key[KEY_LENGTH + 1];

	#ifdef _DEBUG

	DWORD DebugShutdown;

	#endif

} REGISTRYPARAMETERS;


VOID WINAPI ServiceMainServer(_In_ DWORD dwArgc, _In_ LPTSTR* lpszArgv);

VOID WINAPI ServiceMainListener(_In_ DWORD dwArgc, _In_ LPTSTR* lpszArgv);

DWORD WINAPI ServiceControlHandlerEx(_In_ DWORD ControlCode, _In_ DWORD EventType, _In_ LPVOID EventData, _In_ LPVOID Context);

void PrintUsage(void);

void InstallService(_In_ BOOL Server);

void UninstallService(_In_ BOOL Server);

DWORD AddAceToObjectSecurityDescriptor(LPTSTR ObjectName, SE_OBJECT_TYPE ObjectType, LPTSTR Trustee, TRUSTEE_FORM TrusteeForm, DWORD AccessRights, ACCESS_MODE AccessMode, DWORD Inheritance);

void LogMessageW(_In_ DWORD LogLevel, _In_ wchar_t* Message, _In_ ...);

DWORD WINAPI ServerThreadProc(_In_ LPVOID lpParameter);

DWORD WINAPI ListenerThreadProc(_In_ LPVOID lpParameter);

DWORD LoadRegistryParameters(void);

DWORD WINAPI LogFileArchiverThreadProc(_In_ LPVOID lpParameter);