[Code]
// Various service related functions
// Source: http://tinyurl.com/ybwjpuq
type
	SERVICE_STATUS = record
    	dwServiceType				: cardinal;
    	dwCurrentState				: cardinal;
    	dwControlsAccepted			: cardinal;
    	dwWin32ExitCode				: cardinal;
    	dwServiceSpecificExitCode	: cardinal;
    	dwCheckPoint				: cardinal;
    	dwWaitHint					: cardinal;
	end;
	HANDLE = cardinal;

const
	SERVICE_QUERY_CONFIG		= $1;
	SERVICE_CHANGE_CONFIG		= $2;
	SERVICE_QUERY_STATUS		= $4;
	SERVICE_START				= $10;
	SERVICE_STOP				= $20;
	SERVICE_ALL_ACCESS			= $f01ff;
	SC_MANAGER_ALL_ACCESS		= $f003f;
	SERVICE_KERNEL_DRIVER		= $1;
	SERVICE_WIN32_OWN_PROCESS	= $10;
	SERVICE_WIN32_SHARE_PROCESS	= $20;
	SERVICE_WIN32				= $30;
	SERVICE_INTERACTIVE_PROCESS = $100;
	SERVICE_BOOT_START          = $0;
	SERVICE_SYSTEM_START        = $1;
	SERVICE_AUTO_START          = $2;
	SERVICE_DEMAND_START        = $3;
	SERVICE_DISABLED            = $4;
	SERVICE_DELETE              = $10000;
	SERVICE_CONTROL_STOP		= $1;
	SERVICE_CONTROL_PAUSE		= $2;
	SERVICE_CONTROL_CONTINUE	= $3;
	SERVICE_CONTROL_INTERROGATE = $4;
	SERVICE_STOPPED				= $1;
	SERVICE_START_PENDING       = $2;
	SERVICE_STOP_PENDING        = $3;
	SERVICE_RUNNING             = $4;
	SERVICE_CONTINUE_PENDING    = $5;
	SERVICE_PAUSE_PENDING       = $6;
	SERVICE_PAUSED              = $7;

// #######################################################################################
// nt based service utilities
// #######################################################################################
function OpenSCManager(lpMachineName, lpDatabaseName: String; dwDesiredAccess :cardinal): HANDLE;
external 'OpenSCManagerA@advapi32.dll stdcall';

function OpenService(hSCManager :HANDLE;lpServiceName: String; dwDesiredAccess :cardinal): HANDLE;
external 'OpenServiceA@advapi32.dll stdcall';

function CloseServiceHandle(hSCObject :HANDLE): Boolean;
external 'CloseServiceHandle@advapi32.dll stdcall';

function CreateService(hSCManager :HANDLE;lpServiceName, lpDisplayName: String;dwDesiredAccess,dwServiceType,dwStartType,dwErrorControl: cardinal;lpBinaryPathName,lpLoadOrderGroup: String; lpdwTagId : cardinal;lpDependencies,lpServiceStartName,lpPassword :String): cardinal;
external 'CreateServiceA@advapi32.dll stdcall';

function DeleteService(hService :HANDLE): Boolean;
external 'DeleteService@advapi32.dll stdcall';

function StartNTService(hService :HANDLE;dwNumServiceArgs : cardinal;lpServiceArgVectors : cardinal) : Boolean;
external 'StartServiceA@advapi32.dll stdcall';

function ControlService(hService :HANDLE; dwControl :cardinal;var ServiceStatus :SERVICE_STATUS) : Boolean;
external 'ControlService@advapi32.dll stdcall';

function QueryServiceStatus(hService :HANDLE;var ServiceStatus :SERVICE_STATUS) : Boolean;
external 'QueryServiceStatus@advapi32.dll stdcall';

function QueryServiceStatusEx(hService :HANDLE;ServiceStatus :SERVICE_STATUS) : Boolean;
external 'QueryServiceStatus@advapi32.dll stdcall';


function OpenServiceManager() : HANDLE;
begin
	if UsingWinNT() = true then begin
		Result := OpenSCManager('','ServicesActive',SC_MANAGER_ALL_ACCESS);
		if Result = 0 then
			MsgBox(ExpandConstant('{cm:msg_servicemanager}'), mbError, MB_OK)
	end
	else begin
			MsgBox(ExpandConstant('{cm:msg_servicemanager2}'), mbError, MB_OK)
			Result := 0;
	end
end;


function RemoveService(ServiceName: String) : Boolean;
var
	hSCM	: HANDLE;
	hService: HANDLE;
begin
	hSCM := OpenServiceManager();
	Result := False;
	if hSCM <> 0 then begin
		hService := OpenService(hSCM,ServiceName,SERVICE_DELETE);
        if hService <> 0 then begin
            Result := DeleteService(hService);
            CloseServiceHandle(hService)
		end;
        CloseServiceHandle(hSCM)
	end
end;


function StopService(ServiceName: String) : Boolean;
var
	hSCM	: HANDLE;
	hService: HANDLE;
	Status	: SERVICE_STATUS;
begin
	hSCM := OpenServiceManager();
	Result := False;
	if hSCM <> 0 then begin
		hService := OpenService(hSCM,ServiceName,SERVICE_STOP);
        if hService <> 0 then begin
        	Result := ControlService(hService,SERVICE_CONTROL_STOP,Status);
            CloseServiceHandle(hService)
		end;
        CloseServiceHandle(hSCM)
	end;
end;


function IsServiceRunning(ServiceName: String) : Boolean;
var
	hSCM	: HANDLE;
	hService: HANDLE;
	Status	: SERVICE_STATUS;
begin
	hSCM := OpenServiceManager();
	Result := False;
	if hSCM <> 0 then begin
		hService := OpenService(hSCM,ServiceName,SERVICE_QUERY_STATUS);
    	if hService <> 0 then begin
			if QueryServiceStatus(hService,Status) then begin
				Result :=(Status.dwCurrentState = SERVICE_RUNNING)
        	end;
            CloseServiceHandle(hService)
		    end;
        CloseServiceHandle(hSCM)
	end
end;