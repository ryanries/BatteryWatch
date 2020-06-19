#define WIN32_LEAN_AND_MEAN				// Don't include unnecessary stuff.

#define _CRT_RAND_S						// For rand_s()

#include <stdlib.h>						// For rand_s()

#include <stdio.h>						// String I/O.

#include <WinSock2.h>					// Windows sockets (precludes Windows.h)

#include <WS2tcpip.h>					// For inet_ntop()

#include <PathCch.h>					// For the use of PathCchRemoveFileSpec.

#include <AclAPI.h>						// For modifying the ACL to the binary during install.

#include "Main.h"

#pragma comment(lib, "Pathcch.lib")		// For the use of PathCchRemoveFileSpec.

#pragma comment(lib, "Ws2_32.lib")		// Windows sockets

wchar_t gLogFilePath[MAX_PATH];

SERVICE_STATUS_HANDLE gServiceStatusHandle;

SERVICE_STATUS gServiceStatus;

HANDLE gServiceStopEvent;

HANDLE gServerMainThread;

HANDLE gListenerMainThread;

HANDLE gLogArchiverThread;

SOCKET gListenSocket;

CRITICAL_SECTION gLogCritSec;

REGISTRYPARAMETERS gRegistryParams = { 0 };

__int64 gServiceStartTime;

const char gValidKeyChars[] = { 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, // No space, to avoid confusion.
								0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
								0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
								0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
								0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
								0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E };



int WINAPI wmain(_In_ int argc, _In_ wchar_t* argv[])
{
	DWORD CurrentPID = 0;

	DWORD CurrentSessionID = 0;

	BOOL StartServer = FALSE;

	BOOL StartListener = FALSE;

	wchar_t UserName[128] = { 0 };

	DWORD UserNameSize = _countof(UserName);

	if (argc > 1)
	{
		if (_wcsicmp(argv[1], L"-installserver") == 0)
		{
			InstallService(TRUE);

			return(0);
		}
		else if (_wcsicmp(argv[1], L"-uninstallserver") == 0)
		{
			UninstallService(TRUE);

			return(0);
		}
		else if (_wcsicmp(argv[1], L"-installlistener") == 0)
		{
			InstallService(FALSE);

			return(0);
		}
		else if (_wcsicmp(argv[1], L"-uninstalllistener") == 0)
		{
			UninstallService(FALSE);

			return(0);
		}
		else if (_wcsicmp(argv[1], L"-startserver") == 0)
		{
			StartServer = TRUE;
		}
		else if (_wcsicmp(argv[1], L"-startlistener") == 0)
		{
			StartListener = TRUE;
		}
		else
		{
			PrintUsage();

			wprintf(L"\nUnrecognized parameter.\n");

			return(0);
		}
	}
	else
	{
		PrintUsage();

		wprintf(L"\nNeed a command-line argument.\n");

		return(0);
	}

	CurrentPID = GetCurrentProcessId();

	if (ProcessIdToSessionId(CurrentPID, &CurrentSessionID) == 0)
	{
		wprintf(L"ERROR: ProcessIdToSessionId failed! Error 0x%08lx\n", GetLastError());

		return(0);
	}

	if (CurrentSessionID != 0)
	{
		PrintUsage();

		wprintf(L"\nThis program only runs when installed as a Windows service.\n");

		return(0);
	}

	GetUserNameW(UserName, &UserNameSize);

	if (_wcsicmp(UserName, L"LOCAL SERVICE") != 0)
	{
		wprintf(L"These services only run as the LOCAL SERVICE account.\n");

		return(0);
	}
	

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Current user: %s", __FUNCTIONW__, UserName);

#pragma warning(disable: 6031)

	InitializeCriticalSectionAndSpinCount(&gLogCritSec, 0xF00);

#pragma warning(default: 6031)

	if (StartServer)
	{
		SERVICE_TABLE_ENTRYW ServiceTable[] = { { SERVICE_NAME_SERVER, (LPSERVICE_MAIN_FUNCTIONW)ServiceMainServer }, { NULL, NULL } };

		if (StartServiceCtrlDispatcherW(ServiceTable) == 0)
		{
			OutputDebugStringW(L"[" SERVICE_NAME L"] wmain: StartServiceCtrlDispatcher failed!\n");

			return(0);
		}
	}
	else
	{
		SERVICE_TABLE_ENTRYW ServiceTable[] = { { SERVICE_NAME_LISTEN, (LPSERVICE_MAIN_FUNCTIONW)ServiceMainListener }, { NULL, NULL } };

		if (StartServiceCtrlDispatcherW(ServiceTable) == 0)
		{
			OutputDebugStringW(L"[" SERVICE_NAME L"] wmain: StartServiceCtrlDispatcher failed!\n");

			return(0);
		}
	}

	return(0);
}

VOID WINAPI ServiceMainServer(_In_ DWORD dwArgc, _In_ LPTSTR* lpszArgv)
{
	UNREFERENCED_PARAMETER(dwArgc);

	UNREFERENCED_PARAMETER(lpszArgv);

	wchar_t ServiceDirectory[MAX_PATH] = { 0 };

	DWORD CurrentModulePathLength = 0;	

	CurrentModulePathLength = GetModuleFileNameW(NULL, ServiceDirectory, MAX_PATH);

	if (CurrentModulePathLength == 0 || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		OutputDebugStringW(L"[ServiceMainServer] ERROR: Failed to get the path to the current image file. Path too long?\n");

		goto ServiceStop;
	}

	PathCchRemoveFileSpec(ServiceDirectory, _countof(ServiceDirectory));

	if (SetCurrentDirectoryW(ServiceDirectory) == 0)
	{
		OutputDebugStringW(L"[ServiceMainServer] ERROR: Failed to set current working directory!\n");

		goto ServiceStop;
	}

	wcscat_s(gLogFilePath, _countof(gLogFilePath), SERVICE_NAME_SERVER L".log");

	// Log file is set up now - all logging from here on can be written to log
	// instead of using wprintf or OutputDebugString.	

	gRegistryParams.LogLevel = LOG_LEVEL_DEBUG;

	LogMessageW(LOG_LEVEL_INFO, L"==================================");

	LogMessageW(LOG_LEVEL_INFO, L"[%s] %s %s is starting.", __FUNCTIONW__, SERVICE_NAME_SERVER, SERVICE_VERSION);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Current working directory: %s", __FUNCTIONW__, ServiceDirectory);

	if (LoadRegistryParameters() != ERROR_SUCCESS)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] LoadRegistryParameters failed!", __FUNCTIONW__);

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] LoadRegistryParameters succeeded.", __FUNCTIONW__);

	gServiceStatusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME_SERVER, ServiceControlHandlerEx, NULL);

	if (gServiceStatusHandle == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] RegisterServiceCtrlHandlerEx failed! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service control handler registered.", __FUNCTIONW__);

	gServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

	gServiceStatus.dwCurrentState = SERVICE_START_PENDING;

	gServiceStatus.dwControlsAccepted = 0;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwServiceSpecificExitCode = NO_ERROR;

	gServiceStatus.dwWaitHint = 3000;

	if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to set service status to START_PENDING! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service status set to START_PENDING.", __FUNCTIONW__);

	gServiceStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	if (gServiceStopEvent == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateEvent failed! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		gServiceStatus.dwControlsAccepted = 0;

		gServiceStatus.dwCurrentState = SERVICE_STOPPED;

		gServiceStatus.dwWin32ExitCode = GetLastError();

		gServiceStatus.dwCheckPoint = 1;

		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

		goto ServiceStop;
	}

	gServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

	gServiceStatus.dwCurrentState = SERVICE_RUNNING;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to set service status to RUNNING! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service status set to RUNNING.", __FUNCTIONW__);

	if ((gServerMainThread = CreateThread(NULL, 0, ServerThreadProc, NULL, 0, NULL)) == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateThread ServerThreadProc failed! Error 0x%08lx!", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] CreateThread ServerThreadProc succeeded.", __FUNCTIONW__);

	if ((gLogArchiverThread = CreateThread(NULL, 0, LogFileArchiverThreadProc, NULL, 0, NULL)) == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateThread LogFileArchiverThreadProc failed with 0x%08lx!", __FUNCTIONW__, GetLastError());

		return;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] CreateThread LogFileArchiverThreadProc succeeded.", __FUNCTIONW__);

	GetSystemTimeAsFileTime((FILETIME*)&gServiceStartTime);	

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service initialization done. All systems go.", __FUNCTIONW__);

	WaitForSingleObject(gServiceStopEvent, INFINITE);

ServiceStop:

	SetEvent(gServiceStopEvent);

	gServiceStatus.dwControlsAccepted = 0;

	gServiceStatus.dwCurrentState = SERVICE_STOPPED;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwCheckPoint = 3;

	if (gServiceStatusHandle)
	{
		if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Failed to set service status to STOPPED! Error 0x%08lx", __FUNCTIONW__, GetLastError());
		}
		else
		{
			LogMessageW(LOG_LEVEL_INFO, L"[%s] Service received the STOP control. Goodbye.", __FUNCTIONW__);
		}
	}
}

VOID WINAPI ServiceMainListener(_In_ DWORD dwArgc, _In_ LPTSTR* lpszArgv)
{
	UNREFERENCED_PARAMETER(dwArgc);

	UNREFERENCED_PARAMETER(lpszArgv);

	wchar_t ServiceDirectory[MAX_PATH] = { 0 };

	DWORD CurrentModulePathLength = 0;

	CurrentModulePathLength = GetModuleFileNameW(NULL, ServiceDirectory, MAX_PATH);

	if (CurrentModulePathLength == 0 || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		OutputDebugStringW(L"[ServiceMainListener] ERROR: Failed to get the path to the current image file. Path too long?\n");

		goto ServiceStop;
	}

	PathCchRemoveFileSpec(ServiceDirectory, _countof(ServiceDirectory));

	if (SetCurrentDirectoryW(ServiceDirectory) == 0)
	{
		OutputDebugStringW(L"[ServiceMainListener] ERROR: Failed to set current working directory!\n");

		goto ServiceStop;
	}

	wcscat_s(gLogFilePath, _countof(gLogFilePath), SERVICE_NAME_LISTEN L".log");

	// Log file is set up now - all logging from here on can be written to log
	// instead of using wprintf or OutputDebugString.	

	gRegistryParams.LogLevel = LOG_LEVEL_DEBUG;

	LogMessageW(LOG_LEVEL_INFO, L"==================================");

	LogMessageW(LOG_LEVEL_INFO, L"[%s] %s %s is starting.", __FUNCTIONW__, SERVICE_NAME_LISTEN, SERVICE_VERSION);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Current working directory: %s", __FUNCTIONW__, ServiceDirectory);

	if (LoadRegistryParameters() != ERROR_SUCCESS)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] LoadRegistryParameters failed!", __FUNCTIONW__);

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] LoadRegistryParameters succeeded.", __FUNCTIONW__);

	gServiceStatusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME_LISTEN, ServiceControlHandlerEx, NULL);

	if (gServiceStatusHandle == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] RegisterServiceCtrlHandlerEx failed! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service control handler registered.", __FUNCTIONW__);

	gServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;

	gServiceStatus.dwCurrentState = SERVICE_START_PENDING;

	gServiceStatus.dwControlsAccepted = 0;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwServiceSpecificExitCode = NO_ERROR;

	gServiceStatus.dwWaitHint = 3000;

	if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to set service status to START_PENDING! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service status set to START_PENDING.", __FUNCTIONW__);

	gServiceStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	if (gServiceStopEvent == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateEvent failed! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		gServiceStatus.dwControlsAccepted = 0;

		gServiceStatus.dwCurrentState = SERVICE_STOPPED;

		gServiceStatus.dwWin32ExitCode = GetLastError();

		gServiceStatus.dwCheckPoint = 1;

		SetServiceStatus(gServiceStatusHandle, &gServiceStatus);

		goto ServiceStop;
	}

	gServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

	gServiceStatus.dwCurrentState = SERVICE_RUNNING;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to set service status to RUNNING! Error 0x%08lx", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service status set to RUNNING.", __FUNCTIONW__);

	if ((gListenerMainThread = CreateThread(NULL, 0, ListenerThreadProc, NULL, 0, NULL)) == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateThread ListenerThreadProc failed! Error 0x%08lx!", __FUNCTIONW__, GetLastError());

		goto ServiceStop;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] CreateThread ListenerThreadProc succeeded.", __FUNCTIONW__);

	if ((gLogArchiverThread = CreateThread(NULL, 0, LogFileArchiverThreadProc, NULL, 0, NULL)) == NULL)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] CreateThread LogFileArchiverThreadProc failed with 0x%08lx!", __FUNCTIONW__, GetLastError());

		return;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] CreateThread LogFileArchiverThreadProc succeeded.", __FUNCTIONW__);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Service initialization done. All systems go.", __FUNCTIONW__);

	WaitForSingleObject(gServiceStopEvent, INFINITE);

ServiceStop:

	SetEvent(gServiceStopEvent);

	gServiceStatus.dwControlsAccepted = 0;

	gServiceStatus.dwCurrentState = SERVICE_STOPPED;

	gServiceStatus.dwWin32ExitCode = NO_ERROR;

	gServiceStatus.dwCheckPoint = 3;

	if (gServiceStatusHandle)
	{
		if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Failed to set service status to STOPPED! Error 0x%08lx", __FUNCTIONW__, GetLastError());
		}
		else
		{
			LogMessageW(LOG_LEVEL_INFO, L"[%s] Service received the STOP control. Goodbye.", __FUNCTIONW__);
		}
	}
}

DWORD WINAPI ServiceControlHandlerEx(_In_ DWORD ControlCode, _In_ DWORD EventType, _In_ LPVOID EventData, _In_ LPVOID Context)
{
	UNREFERENCED_PARAMETER(Context);

	UNREFERENCED_PARAMETER(EventData);

	UNREFERENCED_PARAMETER(EventType);

	switch (ControlCode)
	{
		case SERVICE_CONTROL_SHUTDOWN:
		case SERVICE_CONTROL_STOP:
		{
			LogMessageW(LOG_LEVEL_INFO, L"[%s] Service is stopping.", __FUNCTIONW__);

			if (gServiceStatus.dwCurrentState != SERVICE_RUNNING)
			{
				LogMessageW(LOG_LEVEL_WARN, L"[%s] Service received the stop or shutdown control, but was already in a non-running state.", __FUNCTIONW__);

				return(NO_ERROR);
			}

			gServiceStatus.dwControlsAccepted = 0;

			gServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;

			gServiceStatus.dwWin32ExitCode = 0;

			gServiceStatus.dwCheckPoint = 4;

			if (SetServiceStatus(gServiceStatusHandle, &gServiceStatus) == 0)
			{
				LogMessageW(LOG_LEVEL_WARN, L"[%s] Failed to set service status to STOP_PENDING! Error 0x%08lx", __FUNCTIONW__, GetLastError());
			}

			SetEvent(gServiceStopEvent);

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Set service stop event.", __FUNCTIONW__);

			if (gListenSocket)
			{
				LogMessageW(LOG_LEVEL_INFO, L"[%s] Closing listen socket.", __FUNCTIONW__);

				closesocket(gListenSocket);

				gListenSocket = 0;

				WSACleanup();
			}		

			return(NO_ERROR);
		}
		default:
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Service received an unimplemented control.", __FUNCTIONW__);

			return(ERROR_CALL_NOT_IMPLEMENTED);
		}
	}
}

void PrintUsage(void)
{
	wprintf(L"\n%s %s\n", SERVICE_NAME, SERVICE_VERSION);

	wprintf(L"%s\n", SERVICE_BOTH_DESC);

	wprintf(L"\nUsage:\n");

	wprintf(L"  %s -installserver\n", SERVICE_NAME);

	wprintf(L"  %s -uninstallserver\n", SERVICE_NAME);

	wprintf(L"  %s -installlistener\n", SERVICE_NAME);

	wprintf(L"  %s -uninstalllistener\n", SERVICE_NAME);
}

void InstallService(_In_ BOOL Server)
{
	SC_HANDLE ServiceController = NULL;

	wchar_t ImageFilePath[MAX_PATH] = { 0 };

	DWORD CurrentModulePathLength = 0;

	SC_HANDLE ServiceHandle = NULL;

	wchar_t InstallDirectory[MAX_PATH] = { 0 };

	SERVICE_DESCRIPTION ServiceDescription = { 0 };

	DWORD AddACEResult = 0;

	if (Server)
	{
		ServiceDescription.lpDescription = SERVICE_SERVER_DESC;
	}
	else
	{
		ServiceDescription.lpDescription = SERVICE_LISTEN_DESC;
	}


	if ((ServiceController = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS)) == NULL)
	{
		DWORD Error = GetLastError();

		wprintf(L"ERROR: Failed to open the service controller! Code 0x%08lx\n", Error);

		if (Error == ERROR_ACCESS_DENIED)
		{
			wprintf(L"Administrator privileges and UAC elevation required if installing or uninstalling.\n");
		}

		return;
	}

	wprintf(L"Service Controller opened.\n");

	CurrentModulePathLength = GetModuleFileNameW(NULL, ImageFilePath, MAX_PATH / sizeof(wchar_t));

	if (CurrentModulePathLength == 0 || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		wprintf(L"ERROR: Failed to get the path to the current image file. Path too long?\n");

		return;
	}

	wprintf(L"Executable: %s\n", ImageFilePath);

	wcscpy_s(InstallDirectory, sizeof(InstallDirectory) / sizeof(wchar_t), ImageFilePath);

	PathCchRemoveFileSpec(InstallDirectory, sizeof(InstallDirectory) / sizeof(wchar_t));

	wprintf(L"Install Directory: %s\n", InstallDirectory);

	if (Server)
	{
		wcscat_s(ImageFilePath, _countof(ImageFilePath), L" -startserver");

		ServiceHandle = CreateServiceW(
			ServiceController,
			SERVICE_NAME_SERVER,
			SERVICE_NAME_SERVER,
			SERVICE_ALL_ACCESS,
			SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START,
			SERVICE_ERROR_NORMAL,
			ImageFilePath,
			NULL,
			NULL,
			NULL,
			L"NT AUTHORITY\\LocalService",
			NULL);
	}
	else
	{
		wcscat_s(ImageFilePath, _countof(ImageFilePath), L" -startlistener");

		ServiceHandle = CreateServiceW(
			ServiceController,
			SERVICE_NAME_LISTEN,
			SERVICE_NAME_LISTEN,
			SERVICE_ALL_ACCESS,
			SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START,
			SERVICE_ERROR_NORMAL,
			ImageFilePath,
			NULL,
			NULL,
			NULL,
			L"NT AUTHORITY\\LocalService",
			NULL);
	}

	if (ServiceHandle == NULL)
	{
		wprintf(L"ERROR: Failed to install service! Code 0x%08lx\n", GetLastError());

		return;
	}

	if (ChangeServiceConfig2W(ServiceHandle, SERVICE_CONFIG_DESCRIPTION, &ServiceDescription) == 0)
	{
		wprintf(L"WARNING: Service was installed, but failed to set service description! Code: 0x%08lx\n", GetLastError());
	}

	wprintf(L"Service installed.\n");

	// Need to make sure 'Local Service' has read permissions to the service install directory.

	AddACEResult = AddAceToObjectSecurityDescriptor(InstallDirectory, SE_FILE_OBJECT, L"NT AUTHORITY\\LocalService", TRUSTEE_IS_NAME, GENERIC_ALL, GRANT_ACCESS, SUB_CONTAINERS_AND_OBJECTS_INHERIT);

	if (AddACEResult != ERROR_SUCCESS)
	{
		wprintf(L"WARNING: Failed to grant Local Service access to the service install directory! Code: 0x%08lx\n", AddACEResult);
	}
	else
	{
		wprintf(L"Access to %s granted to LOCAL SERVICE.\n", InstallDirectory);
	}

	if (StartServiceW(ServiceHandle, 0, NULL) == 0)
	{
		wprintf(L"WARNING: Failed to start service! Code 0x%08lx\n", GetLastError());
	}
	else
	{
		wprintf(L"Service started.\n");
	}
}

void UninstallService(_In_ BOOL Server)
{
	SC_HANDLE ServiceController = NULL;

	SC_HANDLE ServiceHandle = NULL;

	SERVICE_STATUS_PROCESS ServiceStatus = { 0 };

	DWORD BytesNeeded = 0;


	if ((ServiceController = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS)) == NULL)
	{
		DWORD Error = GetLastError();

		wprintf(L"ERROR: Failed to open the service controller! Code 0x%08lx\n", Error);

		if (Error == ERROR_ACCESS_DENIED)
		{
			wprintf(L"Administrator privileges and UAC elevation required if installing or uninstalling.\n");
		}

		return;
	}

	if (Server)
	{
		if ((ServiceHandle = OpenServiceW(ServiceController, SERVICE_NAME_SERVER, SERVICE_ALL_ACCESS)) == NULL)
		{
			wprintf(L"ERROR: Failed to open the service! Code 0x%08lx\n", GetLastError());

			return;
		}
	}
	else
	{
		if ((ServiceHandle = OpenServiceW(ServiceController, SERVICE_NAME_LISTEN, SERVICE_ALL_ACCESS)) == NULL)
		{
			wprintf(L"ERROR: Failed to open the service! Code 0x%08lx\n", GetLastError());

			return;
		}
	}

	if (QueryServiceStatusEx(ServiceHandle, SC_STATUS_PROCESS_INFO, (LPBYTE)&ServiceStatus, sizeof(SERVICE_STATUS_PROCESS), &BytesNeeded) == 0)
	{
		wprintf(L"ERROR: Failed to query service status! Code 0x%08lx\n", GetLastError());

		return;
	}

	if (ServiceStatus.dwCurrentState == SERVICE_RUNNING)
	{
		if (ControlService(ServiceHandle, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ServiceStatus) == 0)
		{
			wprintf(L"ERROR: Failed to send stop control to the service! Code 0x%08lx\n", GetLastError());

			return;
		}

		wprintf(L"Waiting for service to stop...\n");

		DWORD StopTimeout = 0;

		while (ServiceStatus.dwCurrentState != SERVICE_STOPPED && StopTimeout < 6)
		{
			if (QueryServiceStatusEx(ServiceHandle, SC_STATUS_PROCESS_INFO, (LPBYTE)&ServiceStatus, sizeof(SERVICE_STATUS_PROCESS), &BytesNeeded) == 0)
			{
				wprintf(L"ERROR: Failed to query service status! Code 0x%08lx\n", GetLastError());

				return;
			}

			Sleep(2000);

			StopTimeout++;
		}

		if (StopTimeout >= 6)
		{
			wprintf(L"WARNING: Service did not stop in a timely manner!\n");
		}
	}

	if (DeleteService(ServiceHandle) == 0)
	{
		wprintf(L"ERROR: Failed to delete service! Code 0x%08lx\n", GetLastError());

		return;
	}

	wprintf(L"Service uninstalled.\n");
}

DWORD AddAceToObjectSecurityDescriptor(LPTSTR ObjectName, SE_OBJECT_TYPE ObjectType, LPTSTR Trustee, TRUSTEE_FORM TrusteeForm, DWORD AccessRights, ACCESS_MODE AccessMode, DWORD Inheritance)
{
	DWORD Result = ERROR_SUCCESS;

	PACL OldDACL = NULL;

	PACL NewDACL = NULL;

	PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;

	EXPLICIT_ACCESSW ExplicitAccess = { 0 };


	if (ObjectName == NULL)
	{
		return(ERROR_INVALID_PARAMETER);
	}

	// Get a pointer to the existing DACL.
	Result = GetNamedSecurityInfoW(ObjectName, ObjectType, DACL_SECURITY_INFORMATION, NULL, NULL, &OldDACL, NULL, &SecurityDescriptor);

	if (Result != ERROR_SUCCESS)
	{
		goto Cleanup;
	}

	// Initialize an EXPLICIT_ACCESS structure for the new ACE. 

	RtlZeroMemory(&ExplicitAccess, sizeof(EXPLICIT_ACCESS));

	ExplicitAccess.grfAccessPermissions = AccessRights;

	ExplicitAccess.grfAccessMode = AccessMode;

	ExplicitAccess.grfInheritance = Inheritance;

	ExplicitAccess.Trustee.TrusteeForm = TrusteeForm;

	ExplicitAccess.Trustee.ptstrName = Trustee;

	// Create a new ACL that merges the new ACE into the existing DACL.

	Result = SetEntriesInAclW(1, &ExplicitAccess, OldDACL, &NewDACL);

	if (Result != ERROR_SUCCESS)
	{
		goto Cleanup;
	}

	// Attach the new ACL as the object's DACL.

	Result = SetNamedSecurityInfoW(ObjectName, ObjectType, DACL_SECURITY_INFORMATION, NULL, NULL, NewDACL, NULL);

	if (Result != ERROR_SUCCESS)
	{
		goto Cleanup;
	}

Cleanup:

	if (SecurityDescriptor != NULL)
	{
		LocalFree((HLOCAL)SecurityDescriptor);
	}
	if (NewDACL != NULL)
	{
		LocalFree((HLOCAL)NewDACL);
	}
	//if (OldDACL != NULL)
	//{
	//	LocalFree((HLOCAL)OldDACL);
	//}

	return Result;
}

void LogMessageW(_In_ DWORD LogLevel, _In_ wchar_t* Message, _In_ ...)
{
	size_t MessageLength = wcslen(Message);

	SYSTEMTIME Time = { 0 };

	HANDLE LogFileHandle = INVALID_HANDLE_VALUE;

	DWORD EndOfFile = 0;

	DWORD NumberOfBytesWritten = 0;

	wchar_t DateTimeString[96] = { 0 };

	wchar_t SeverityTag[8] = { 0 };

	wchar_t FormattedMessage[4096] = { 0 };

	int Error = 0;

	BOOL CritSecOwned = FALSE;

	if (gRegistryParams.LogLevel < LogLevel)
	{
		return;
	}

	if (MessageLength < 1 || MessageLength > 4096)
	{
		OutputDebugStringW(L"[" SERVICE_NAME L"] LogMessageW: Unable to log, MessageLength was < 1 or > 4096.\n");

		return;
	}

	if (wcslen(gLogFilePath) < 2)
	{
		OutputDebugStringW(L"[" SERVICE_NAME L"] LogMessageW: Attempted to log a message, but the log file was not yet initialized! \n");

		return;
	}

	switch (LogLevel)
	{
		case LOG_LEVEL_INFO:
		{
			wcscpy_s(SeverityTag, _countof(SeverityTag), L"[INFO ]");

			break;
		}
		case LOG_LEVEL_WARN:
		{
			wcscpy_s(SeverityTag, _countof(SeverityTag), L"[WARN ]");

			break;
		}
		case LOG_LEVEL_ERROR:
		{
			wcscpy_s(SeverityTag, _countof(SeverityTag), L"[ERROR]");

			break;
		}
		case LOG_LEVEL_DEBUG:
		{
			wcscpy_s(SeverityTag, _countof(SeverityTag), L"[DEBUG]");

			break;
		}
		default:
		{
			wcscpy_s(SeverityTag, _countof(SeverityTag), L"[?????]");
		}
	}

	GetLocalTime(&Time);

	va_list ArgPointer = NULL;

	va_start(ArgPointer, Message);

	_vsnwprintf_s(FormattedMessage, _countof(FormattedMessage), _TRUNCATE, Message, ArgPointer);

	va_end(ArgPointer);

	Error = _snwprintf_s(DateTimeString, _countof(DateTimeString), _TRUNCATE, L"\r\n[%02u/%02u/%u %02u:%02u:%02u.%03u]", Time.wMonth, Time.wDay, Time.wYear, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);

	if (Error < 1)
	{
		OutputDebugStringW(L"[" SERVICE_NAME L"] LogMessageW: _snwprintf_s returned -1. Buffer too small?\n");

		goto Exit;
	}

	// Synchronize file access, hold the crit sec for as little time as possible.

	EnterCriticalSection(&gLogCritSec);

	CritSecOwned = TRUE;

	if ((LogFileHandle = CreateFileW(gLogFilePath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{
		OutputDebugStringW(L"[" SERVICE_NAME L"] LogMessageW: CreateFileW returned INVALID_HANDLE_VALUE.\n");

		goto Exit;
	}

	EndOfFile = SetFilePointer(LogFileHandle, 0, NULL, FILE_END);

	if (EndOfFile == INVALID_SET_FILE_POINTER)
	{
		OutputDebugStringW(L"[" SERVICE_NAME L"] LogMessageW: SetFilePointer returned INVALID_SET_FILE_POINTER.\n");

		goto Exit;
	}

	WriteFile(LogFileHandle, DateTimeString, (DWORD)wcslen(DateTimeString) * sizeof(wchar_t), &NumberOfBytesWritten, NULL);

	WriteFile(LogFileHandle, SeverityTag, (DWORD)wcslen(SeverityTag) * sizeof(wchar_t), &NumberOfBytesWritten, NULL);

	WriteFile(LogFileHandle, FormattedMessage, (DWORD)wcslen(FormattedMessage) * sizeof(wchar_t), &NumberOfBytesWritten, NULL);

Exit:

	if (LogFileHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(LogFileHandle);
	}

	if (CritSecOwned)
	{
		LeaveCriticalSection(&gLogCritSec);
	}
}

DWORD WINAPI ServerThreadProc(_In_ LPVOID lpParameter)
{
	UNREFERENCED_PARAMETER(lpParameter);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Thread start.", __FUNCTIONW__);

	wchar_t UserName[128] = { 0 };

	DWORD UserNameSize = _countof(UserName);

	GetUserNameW(UserName, &UserNameSize);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Current user: %s", __FUNCTIONW__, UserName);

	while (WaitForSingleObject(gServiceStopEvent, 0) != WAIT_OBJECT_0)
	{
		SYSTEM_POWER_STATUS PowerStatus = { 0 };

		if (GetSystemPowerStatus(&PowerStatus) == 0)
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] GetSystemPowerStatus failed with 0x%08lx!", __FUNCTIONW__, GetLastError());

			goto Sleep;
		}

		if (PowerStatus.BatteryLifePercent == 255)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Battery charge percentage is unknown!", __FUNCTIONW__);

			goto Sleep;
		}

		if (PowerStatus.ACLineStatus != 1)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] AC power state is offline or unknown!", __FUNCTIONW__);
		}

		LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Battery charge percentage is %d%%.", __FUNCTIONW__, PowerStatus.BatteryLifePercent);

		#ifdef _DEBUG

		if (gRegistryParams.DebugShutdown)
		{
			goto DebugShutdown;
		}

		#endif

		if (PowerStatus.BatteryLifePercent < gRegistryParams.ThresholdBatteryPercentage)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Battery charge percentage %d%% is less than configured threshold %d%%!", __FUNCTIONW__, PowerStatus.BatteryLifePercent, gRegistryParams.ThresholdBatteryPercentage);

			__int64 ServiceUptimeMinutes = 0;

			__int64 Now = { 0 };

			GetSystemTimeAsFileTime((FILETIME*)&Now);

			ServiceUptimeMinutes = (Now - gServiceStartTime) / 10000000ULL / 60;

			LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Service uptime: %d minutes.", __FUNCTIONW__, ServiceUptimeMinutes);

			if (ServiceUptimeMinutes > gRegistryParams.GracePeriodMinutes)
			{
				#ifdef _DEBUG

				DebugShutdown:

				#endif

				LogMessageW(LOG_LEVEL_WARN, L"[%s] Grace period has expired and battery charge percent is below threshold. Broadcasting shutdown signal to local network now!", __FUNCTIONW__);

				WSADATA WSAData = { 0 };

				SOCKET Socket = 0;

				int WSAResult = 0;

				char BroadcastOption = 1;

				char BroadcastMessage[128] = { 0 };

				struct sockaddr_in Address = { 0 };			


				Address.sin_family = AF_INET;

				Address.sin_port = htons(gRegistryParams.BroadcastPort);

				Address.sin_addr.s_addr = INADDR_BROADCAST;				

				if ((WSAResult = WSAStartup(MAKEWORD(2, 2), &WSAData)) != NO_ERROR)
				{	
					LogMessageW(LOG_LEVEL_ERROR, L"[%s] WSAStartup failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);	
	
					goto Sleep;
				}

				LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Winsock initialized.", __FUNCTIONW__);

				if ((Socket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
				{
					WSAResult = WSAGetLastError();

					LogMessageW(LOG_LEVEL_ERROR, L"[%s] Socket creation failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

					goto Sleep;
				}

				LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Socket created.", __FUNCTIONW__);

				if (setsockopt(Socket, SOL_SOCKET, SO_BROADCAST, &BroadcastOption, sizeof(BroadcastOption)) == SOCKET_ERROR)
				{
					WSAResult = WSAGetLastError();

					closesocket(Socket);

					LogMessageW(LOG_LEVEL_ERROR, L"[%s] Set socket option broadcast failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

					goto Sleep;
				}

				LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Socket broadcast option set.", __FUNCTIONW__);

				sprintf_s(BroadcastMessage, sizeof(BroadcastMessage), "%S_SHUTDOWN_%s", SERVICE_NAME, gRegistryParams.Key);

				LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Full network message to be sent: %S", __FUNCTIONW__, BroadcastMessage);				

				for (int Count = 0; Count < 3; Count++)
				{
					sendto(Socket, BroadcastMessage, (int)strlen(BroadcastMessage) + 1, 0, (struct sockaddr*)&Address, (int)sizeof(Address));

					Sleep(500);
				}

				closesocket(Socket);

				WSACleanup();

				LogMessageW(LOG_LEVEL_INFO, L"[%s] The shutdown broadcast was sent three times.", __FUNCTIONW__);

				LogMessageW(LOG_LEVEL_INFO, L"[%s] Time to shut down the server service now.", __FUNCTIONW__);

				SetEvent(gServiceStopEvent);

				return(0);
			}

		}

	Sleep:

		Sleep(7000);
	}

	return(0);
}

DWORD WINAPI ListenerThreadProc(_In_ LPVOID lpParameter)
{
	UNREFERENCED_PARAMETER(lpParameter);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Thread start.", __FUNCTIONW__);

	HANDLE ProcessToken = NULL;					// For enabling SE_SHUTDOWN_NAME privilege.

	LUID PrivilegeNameLuid = { 0 };				// For enabling SE_SHUTDOWN_NAME privilege.

	TOKEN_PRIVILEGES TokenPrivileges = { 0 };	// For enabling SE_SHUTDOWN_NAME privilege.

	wchar_t UserName[128] = { 0 };

	DWORD UserNameSize = _countof(UserName);

	WSADATA WSAData = { 0 };	

	char ReceiveBuffer[256] = { 0 };

	int WSAResult = 0;

	char BroadcastOption = 1;

	struct sockaddr_in ReceiverAddress = { 0 };

	struct sockaddr_in SenderAddress = { 0 };

	int Length = sizeof(struct sockaddr_in);

	char IPAddressString[64] = { 0 };

	GetUserNameW(UserName, &UserNameSize);

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Current user: %s", __FUNCTIONW__, UserName);


	ReceiverAddress.sin_family = AF_INET;

	ReceiverAddress.sin_port = htons(gRegistryParams.BroadcastPort);

	ReceiverAddress.sin_addr.s_addr = INADDR_ANY;

	if ((WSAResult = WSAStartup(MAKEWORD(2, 2), &WSAData)) != NO_ERROR)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] WSAStartup failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

		goto Exit;
	}

	LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Winsock initialized.", __FUNCTIONW__);

	if ((gListenSocket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		WSAResult = WSAGetLastError();

		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Socket creation failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

		goto Exit;
	}

	LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Socket created.", __FUNCTIONW__);

	if (setsockopt(gListenSocket, SOL_SOCKET, SO_BROADCAST, &BroadcastOption, sizeof(BroadcastOption)) == SOCKET_ERROR)
	{
		WSAResult = WSAGetLastError();		

		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Set socket option broadcast failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

		goto Exit;
	}

	LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Socket broadcast option set.", __FUNCTIONW__);

	if (bind(gListenSocket, (struct sockaddr*)&ReceiverAddress, sizeof(ReceiverAddress)) == SOCKET_ERROR)
	{
		WSAResult = WSAGetLastError();

		LogMessageW(LOG_LEVEL_ERROR, L"[%s] bind failed! Error 0x%08lx!", __FUNCTIONW__, WSAResult);

		goto Exit;
	}

	while (WaitForSingleObject(gServiceStopEvent, 0) != WAIT_OBJECT_0)
	{
		LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Listening...", __FUNCTIONW__);

		if (recvfrom(gListenSocket, ReceiveBuffer, sizeof(ReceiveBuffer), 0, (struct sockaddr*)&SenderAddress, &Length) == SOCKET_ERROR)
		{
			if (WaitForSingleObject(gServiceStopEvent, 0) != WAIT_OBJECT_0)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] recvfrom failed with 0x%08lx!", __FUNCTIONW__, WSAGetLastError());
			}

			continue;
		}

		inet_ntop(SenderAddress.sin_family, &SenderAddress.sin_addr, IPAddressString, sizeof(IPAddressString));

		LogMessageW(LOG_LEVEL_INFO, L"[%s] Broadcast received from %S: %S", __FUNCTIONW__, IPAddressString, ReceiveBuffer);

		if (strlen(ReceiveBuffer) != (wcslen(SERVICE_NAME) + strlen("_SHUTDOWN_") + KEY_LENGTH))
		{
			LogMessageW(LOG_LEVEL_WARN, 
				L"[%s] Broadcast was not of the correct length! (Received: %d Expected: %d)", 
				__FUNCTIONW__, 
				strlen(ReceiveBuffer),
				(wcslen(SERVICE_NAME) + strlen("_SHUTDOWN_") + KEY_LENGTH));

			continue;
		}

		if (strcmp(ReceiveBuffer + wcslen(SERVICE_NAME) + strlen("_SHUTDOWN_"), gRegistryParams.Key) != 0)
		{
			LogMessageW(LOG_LEVEL_WARN, L"[%s] Authentication key was incorrect! (Received: %S Expected: %S)", 
				__FUNCTIONW__,
				ReceiveBuffer + (wcslen(SERVICE_NAME) + strlen("_SHUTDOWN_")),					
				gRegistryParams.Key);

			continue;
		}

		LogMessageW(LOG_LEVEL_INFO, L"[%s] Authentication key matches. Attempting to power off the system.", __FUNCTIONW__);

		OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &ProcessToken);

		LookupPrivilegeValueA("", "SeShutdownPrivilege", &PrivilegeNameLuid);	// SE_SHUTDOWN_NAME;

		TokenPrivileges.PrivilegeCount = 1;

		TokenPrivileges.Privileges[0].Luid = PrivilegeNameLuid;

		TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		AdjustTokenPrivileges(ProcessToken, FALSE, &TokenPrivileges, sizeof(TokenPrivileges), NULL, 0);

		if (ExitWindowsEx(EWX_POWEROFF | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_POWER) == 0)
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] ExitWindowsEx failed with 0x%08lx!", __FUNCTIONW__, GetLastError());
		}
	}

Exit:

	SetEvent(gServiceStopEvent);

	return(0);
}

DWORD LoadRegistryParameters(void)
{
	DWORD Result = ERROR_SUCCESS;

	HKEY RegKey = NULL;

	DWORD RegDisposition = 0;

	DWORD RegBytesRead = sizeof(DWORD);

	Result = RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\" SERVICE_NAME, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &RegKey, &RegDisposition);

	if (Result != ERROR_SUCCESS)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] RegCreateKeyExW failed attempting to access HKCU\\SOFTWARE\\%s! Error 0x%08lx", __FUNCTIONW__, SERVICE_NAME, Result);

		goto Cleanup;
	}

	if (RegDisposition == REG_CREATED_NEW_KEY)
	{
		LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry key did not exist; created new key HKCU\\SOFTWARE\\%s", __FUNCTIONW__, SERVICE_NAME);
	}
	else
	{
		LogMessageW(LOG_LEVEL_INFO, L"[%s] Opened existing registry key HKCU\\SOFTWARE\\%s", __FUNCTIONW__, SERVICE_NAME);
	}

	Result = RegGetValueA(RegKey, NULL, "LogLevel", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.LogLevel, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'LogLevel' not found. Using default log level of 0. (Errors only.)", __FUNCTIONW__);

			gRegistryParams.LogLevel = LOG_LEVEL_ERROR;
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'LogLevel' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	if (gRegistryParams.LogLevel > LOG_LEVEL_DEBUG)
	{
		LogMessageW(LOG_LEVEL_WARN, L"[%s] The 'LogLevel' registry value was set to an invalid value of %d. Using value of %d. (Log errors, warnings and informational.)" __FUNCTIONW__, gRegistryParams.LogLevel, LOG_LEVEL_INFO);

		gRegistryParams.LogLevel = LOG_LEVEL_INFO;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] LogLevel is %d.", __FUNCTIONW__, gRegistryParams.LogLevel);

	// ///////////////////////////////////////////////////////

	Result = RegGetValueA(RegKey, NULL, "ThresholdBatteryPercentage", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.ThresholdBatteryPercentage, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'ThresholdBatteryPercentage' not found. Using default ThresholdBatteryPercentage of 50%%.", __FUNCTIONW__);

			gRegistryParams.ThresholdBatteryPercentage = 50;
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'ThresholdBatteryPercentage' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	if (gRegistryParams.ThresholdBatteryPercentage > 99)
	{
		LogMessageW(LOG_LEVEL_WARN, L"[%s] Registry value 'ThresholdBatteryPercentage' was too high. Using default percentage of 50%%.", __FUNCTIONW__);

		gRegistryParams.ThresholdBatteryPercentage = 50;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] ThresholdBatteryPercentage is %d.", __FUNCTIONW__, gRegistryParams.ThresholdBatteryPercentage);	

	// ////////////////////////////////////////////////////////////////

	Result = RegGetValueA(RegKey, NULL, "GracePeriodMinutes", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.GracePeriodMinutes, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'GracePeriodMinutes' not found. Using default GracePeriodMinutes of 15.", __FUNCTIONW__);

			gRegistryParams.GracePeriodMinutes = 15;
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'GracePeriodMinutes' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] GracePeriodMinutes is %d.", __FUNCTIONW__, gRegistryParams.GracePeriodMinutes);

	// ////////////////////////////////////////////////////////////////

	Result = RegGetValueA(RegKey, NULL, "BroadcastPort", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.BroadcastPort, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'BroadcastPort' not found. Using default BroadcastPort of 31008.", __FUNCTIONW__);

			gRegistryParams.BroadcastPort = 31008;
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'BroadcastPort' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] BroadcastPort is %d.", __FUNCTIONW__, gRegistryParams.BroadcastPort);

	// ////////////////////////////////////////////////////////////////

	RegBytesRead = sizeof(gRegistryParams.Key);

	Result = RegGetValueA(RegKey, NULL, "Key", RRF_RT_REG_SZ, NULL, (BYTE*)&gRegistryParams.Key, &RegBytesRead);	

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;
			// Generate random key if one is not found
			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'Key' not found. Generating a random key.", __FUNCTIONW__);

			for (unsigned char Index = 0; Index < KEY_LENGTH; Index++)
			{
				unsigned int Random = 0;

				rand_s(&Random);

				gRegistryParams.Key[Index] = gValidKeyChars[Random % sizeof(gValidKeyChars)];				
			}

			Result = RegSetValueExA(RegKey, "Key", 0, REG_SZ, (BYTE*)gRegistryParams.Key, KEY_LENGTH + 1);

			if (Result != ERROR_SUCCESS)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to set the 'Key' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

				goto Cleanup;
			}
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'Key' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	if (strlen(gRegistryParams.Key) != KEY_LENGTH)
	{
		LogMessageW(LOG_LEVEL_ERROR, L"[%s] Key was read from the registry but it was not the proper length. It should be %d characters.", __FUNCTIONW__, KEY_LENGTH);

		Result = ERROR_PWD_TOO_SHORT;

		goto Cleanup;
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] Key is %S.", __FUNCTIONW__, gRegistryParams.Key);

	// ////////////////////////////////////////////////////////////////

	#ifdef _DEBUG
	
	// Note: We MUST delete the 'DebugShutdown' registry value as soon as we find that it is set to 1, in order to avoid a 
	// denial-of-service situation. If DebugShutdown = 1 were to persist in the registry, the server service would immediately
	// shut the computer down as soon as it booted up!

	Result = RegGetValueA(RegKey, NULL, "DebugShutdown", RRF_RT_DWORD, NULL, (BYTE*)&gRegistryParams.DebugShutdown, &RegBytesRead);

	if (Result != ERROR_SUCCESS)
	{
		if (Result == ERROR_FILE_NOT_FOUND)
		{
			Result = ERROR_SUCCESS;

			LogMessageW(LOG_LEVEL_INFO, L"[%s] Registry value 'DebugShutdown' not found. Using default DebugShutdown of 0.", __FUNCTIONW__);

			gRegistryParams.DebugShutdown = 0;
		}
		else
		{
			LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to read the 'DebugShutdown' registry value! Error 0x%08lx", __FUNCTIONW__, Result);

			goto Cleanup;
		}
	}

	LogMessageW(LOG_LEVEL_INFO, L"[%s] DebugShutdown is %d.", __FUNCTIONW__, gRegistryParams.DebugShutdown);

	if (gRegistryParams.DebugShutdown > 0)
	{
		RegDeleteValueA(RegKey, "DebugShutdown");
	}

	#endif	


Cleanup:

	if (RegKey)
	{
		RegCloseKey(RegKey);
	}

	return(Result);
}

DWORD WINAPI LogFileArchiverThreadProc(_In_ LPVOID lpParameter)
{
	UNREFERENCED_PARAMETER(lpParameter);

	LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Thread start.", __FUNCTIONW__);

	BOOL CritSecOwned = FALSE;

	HANDLE Mutex = CreateMutexW(NULL, FALSE, SERVICE_NAME L"_LogArchiver");	

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Log archiver thread has already been started. This thread will now exit. This will likely happen if both server and listener are installed simultaneously.", __FUNCTIONW__);

		return(0);
	}

	UNREFERENCED_PARAMETER(Mutex);

	while (WaitForSingleObject(gServiceStopEvent, 0) != WAIT_OBJECT_0)
	{
		DWORD FileSize = 0;

		HANDLE ServerLogFileHandle = INVALID_HANDLE_VALUE;

		HANDLE ListenerLogFileHandle = INVALID_HANDLE_VALUE;

		LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Checking to see if current server log file needs to be archived.", __FUNCTIONW__);

		ServerLogFileHandle = CreateFileW(SERVICE_NAME_SERVER L".log", FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if ((ServerLogFileHandle == INVALID_HANDLE_VALUE) || (ServerLogFileHandle == NULL))
		{
			DWORD Error = GetLastError();

			// Ignore file not found error, because file not found is expected when we only have one role installed, server or listener, but not both installed.
			if (Error != ERROR_FILE_NOT_FOUND)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to open %s. Failed with error 0x%08lx. This is expected if only one component is installed, either listener or server, but not both.", __FUNCTIONW__, SERVICE_NAME_SERVER L".log", Error);
			}
		}
		else
		{
			if ((FileSize = GetFileSize(ServerLogFileHandle, NULL)) == INVALID_FILE_SIZE)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] GetFileSize failed with error 0x%08lx", __FUNCTIONW__, GetLastError());
			}
			else
			{
				if (FileSize > MAX_LOG_FILE_SIZE)
				{
					LogMessageW(LOG_LEVEL_INFO, L"[%s] Current server log file is %d bytes. Archiving...", __FUNCTIONW__, FileSize);

					// NO MORE LOGGING UNTIL LEAVING THIS CRIT SEC
					EnterCriticalSection(&gLogCritSec);

					CritSecOwned = TRUE;

					if (ServerLogFileHandle && (ServerLogFileHandle != INVALID_HANDLE_VALUE))
					{
						CloseHandle(ServerLogFileHandle);
					}

					SYSTEMTIME Time = { 0 };

					wchar_t ArchivedLogFileName[64] = { 0 };

					GetLocalTime(&Time);

					_snwprintf_s(ArchivedLogFileName, _countof(ArchivedLogFileName), _TRUNCATE, SERVICE_NAME_SERVER L"_%u_%02u_%02u_%02u.%02u.%02u.log", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);

					if (MoveFileW(SERVICE_NAME_SERVER L".log", ArchivedLogFileName) == 0)
					{
						// we cannot log this failure to file, because the logging function needs the critical section that we current own.
						OutputDebugStringW(L"[" SERVICE_NAME_SERVER L"] MoveFile failed during server log archive!\n");
					}
				}
			}

			if (ServerLogFileHandle && (ServerLogFileHandle != INVALID_HANDLE_VALUE))
			{
				CloseHandle(ServerLogFileHandle);
			}

			if (CritSecOwned)
			{
				LeaveCriticalSection(&gLogCritSec);

				CritSecOwned = FALSE;

				LogMessageW(LOG_LEVEL_INFO, L"[%s] Server Log file was archived. A new log file was started. Critical section released.", __FUNCTIONW__);
			}
		}

		LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Checking to see if current listener log file needs to be archived.", __FUNCTIONW__);

		ListenerLogFileHandle = CreateFileW(SERVICE_NAME_LISTEN L".log", FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if ((ListenerLogFileHandle == INVALID_HANDLE_VALUE) || (ListenerLogFileHandle == NULL))
		{
			DWORD Error = GetLastError();

			// Ignore file not found error, because file not found is expected when we only have one role installed, server or listener, but not both installed.
			if (Error != ERROR_FILE_NOT_FOUND)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] Failed to open %s. Failed with error 0x%08lx. This is expected if only one component is installed, either listener or server, but not both.", __FUNCTIONW__, SERVICE_NAME_SERVER L".log", Error);
			}			
		}
		else
		{
			if ((FileSize = GetFileSize(ListenerLogFileHandle, NULL)) == INVALID_FILE_SIZE)
			{
				LogMessageW(LOG_LEVEL_ERROR, L"[%s] GetFileSize failed with error 0x%08lx", __FUNCTIONW__, GetLastError());
			}
			else
			{
				if (FileSize > MAX_LOG_FILE_SIZE)
				{
					LogMessageW(LOG_LEVEL_INFO, L"[%s] Current listener log file is %d bytes. Archiving...", __FUNCTIONW__, FileSize);

					// NO MORE LOGGING UNTIL LEAVING THIS CRIT SEC
					EnterCriticalSection(&gLogCritSec);

					CritSecOwned = TRUE;

					if (ListenerLogFileHandle && (ListenerLogFileHandle != INVALID_HANDLE_VALUE))
					{
						CloseHandle(ListenerLogFileHandle);
					}

					SYSTEMTIME Time = { 0 };

					wchar_t ArchivedLogFileName[64] = { 0 };

					GetLocalTime(&Time);

					_snwprintf_s(ArchivedLogFileName, _countof(ArchivedLogFileName), _TRUNCATE, SERVICE_NAME_SERVER L"_%u_%02u_%02u_%02u.%02u.%02u.log", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);

					if (MoveFileW(SERVICE_NAME_LISTEN L".log", ArchivedLogFileName) == 0)
					{
						// we cannot log this failure to file, because the logging function needs the critical section that we current own.
						OutputDebugStringW(L"[" SERVICE_NAME_LISTEN L"] MoveFile failed during listener log archive!\n");
					}
				}
			}

			if (ListenerLogFileHandle && (ListenerLogFileHandle != INVALID_HANDLE_VALUE))
			{
				CloseHandle(ListenerLogFileHandle);
			}

			if (CritSecOwned)
			{
				LeaveCriticalSection(&gLogCritSec);

				CritSecOwned = FALSE;

				LogMessageW(LOG_LEVEL_INFO, L"[%s] Listener Log file was archived. A new log file was started. Critical section released.", __FUNCTIONW__);
			}
		}


		int ThreadTimer = LOG_ARCHIVE_TASK_FREQUENCY_MS; // This task runs every x milliseconds.

		// An "alertable" sleep...
		while (WaitForSingleObject(gServiceStopEvent, 0) != WAIT_OBJECT_0)
		{
			if (ThreadTimer <= 0)
			{
				break;
			}

			Sleep(250);

			ThreadTimer -= 250;
		}
	}

	if (CritSecOwned == TRUE)
	{
		LeaveCriticalSection(&gLogCritSec);
	}

	LogMessageW(LOG_LEVEL_DEBUG, L"[%s] Terminating.", __FUNCTIONW__);

	return(0);
}