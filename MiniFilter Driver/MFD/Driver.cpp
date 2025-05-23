#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

LARGE_INTEGER TimeOut;

//Terminate�� ���μ����� �̸��� PID�� �����ϱ� ���� ����ü
typedef struct _PROCESS_NAME_RECORD {
    ULONG Pid;
    LIST_ENTRY Entry;
    WCHAR ProcessName[ 260 ];
} PROCESS_NAME_RECORD;

LIST_ENTRY g_ProcessNameList;
FAST_MUTEX g_ProcessListLock;

// IRP ��ſ� ����� ����ü
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;
    NTSTATUS ResultStatus;
    WCHAR ImageName[ 260 ];
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

//Process Event ��ſ� ����� ����ü
typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ImageName[ 260 ];
} PROC_EVENT_INFO, * PPROC_EVENT_INFO;

typedef enum _MESSAGE_TYPE {
    MessageTypeIrpCreate,
    MessageTypeProcEvent
} MESSAGE_TYPE;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CREATE_INFO IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Ȯ���ڿ� ���μ��� �̸��� ����� �Լ�
VOID ExtractFileName( const WCHAR* fullPath, WCHAR* outFileName, SIZE_T outLen )
{
    if( fullPath == NULL || outFileName == NULL )
        return;

    const WCHAR* lastSlash = fullPath;
    const WCHAR* p = fullPath;

    while( *p != L'\0' ) {
        if( *p == L'\\' || *p == L'/' )
            lastSlash = p + 1;
        p++;
    }

    RtlStringCchCopyW( outFileName, outLen, lastSlash );
}

//CreateInfo�� Terminate�� ����ؼ� Pid�� ImangeName�� �����ϴ� �Լ�
VOID SaveProcessName( ULONG pid, const WCHAR* InName ) {
    //���� �Ҵ��� �ϸ� �Ҵ� ���� �� �Լ��� �ٷ� ������ �������� Ȯ���Ѵ�, NonPagePool: �׻� �޸𸮿� �����ϴ� Ŀ�� Ǯ, prnm: �޸� �±�
    PROCESS_NAME_RECORD* rec = (PROCESS_NAME_RECORD*)ExAllocatePoolWithTag( NonPagedPool, sizeof( PROCESS_NAME_RECORD ), 'prnm' );
	if( rec == NULL )
        return;

    rec->Pid = pid; //���� �Ҵ�� �޸𸮿� PID ����
    RtlStringCchCopyW( rec->ProcessName, 260, InName ); //���� �Ҵ�� �޸𸮿� ImageName ����

    ExAcquireFastMutex( &g_ProcessListLock ); //���ؽ��� ����ȭ�Ͽ� ���� ������ ���� �浹 ����
    InsertTailList( &g_ProcessNameList, &rec->Entry ); //����Ʈ ���� ���ο� �׸� �߰�
    ExReleaseFastMutex( &g_ProcessListLock ); //���ؽ� ����ȭ ����
}

BOOLEAN FindProcessName( ULONG pid, WCHAR* OutName ) {
    BOOLEAN found = FALSE;

    ExAcquireFastMutex( &g_ProcessListLock ); //�µ����� ����ȭ�Ͽ� ���� ������ ���� �浹 ����

    for( PLIST_ENTRY p = g_ProcessNameList.Flink; p != &g_ProcessNameList; p = p->Flink ) {
        PROCESS_NAME_RECORD* rec = CONTAINING_RECORD( p, PROCESS_NAME_RECORD, Entry );
        if( rec->Pid == pid ) {
            RtlStringCchCopyW( OutName, 260, rec->ProcessName ); //����Ʈ ��ȸ �� Pid�� ��ϵ� Pid�� ���ٸ� �� ����ü�� ProcessName�� OutName�� ����
            RemoveEntryList( p ); //����Ʈ���� �ش� ����ü ����
            ExFreePoolWithTag( rec, 'prnm' ); //�޸� ����
            found = TRUE;
            break;
        }
    }

    ExReleaseFastMutex( &g_ProcessListLock );
    return found;
}

//���μ��� ����/���Ḧ �˷��ִ� �Լ�
extern "C"
VOID ProcessNotifyEx( PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo )
{
    UNREFERENCED_PARAMETER( Process );

    if( gClientPort == NULL )
        return;

    GENERIC_MESSAGE msg = {};
    msg.Type = MessageTypeProcEvent;
    TimeOut.QuadPart = -10 * 1000 * 1000;
    if( CreateInfo != NULL ) {
        // ���μ��� ����
        msg.ProcInfo.IsCreate = TRUE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
        msg.ProcInfo.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;

        if( CreateInfo->ImageFileName != NULL ) {
            WCHAR ShortName[260] = L"<Unknown>";
            ExtractFileName( CreateInfo->ImageFileName->Buffer, ShortName, 260 );
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, ShortName );
            SaveProcessName( (ULONG)(ULONG_PTR)ProcessId, ShortName );
        }
        else {
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown>" );
        }
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "[Create] Type: %d, PID: %lu, ParentPID: %lu, ImageName: %ws\n",
            msg.Type,
            msg.ProcInfo.ProcessId,
            msg.ProcInfo.ParentProcessId,
            msg.ProcInfo.ImageName );

    }
    else {
        // ���μ��� ����
        msg.ProcInfo.IsCreate = FALSE;
        msg.ProcInfo.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

        WCHAR TName[ 260 ] = L"<Unknown>";
        if( FindProcessName( msg.ProcInfo.ProcessId, TName ) )
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, TName );
        else
            RtlStringCchCopyW( msg.ProcInfo.ImageName, 260, L"<Unknown Process>" );
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
            "[Terminate] Type: %d, PID: %lu, ImageName: %ws\n",
            msg.Type,
            msg.ProcInfo.ProcessId,
            msg.ProcInfo.ImageName );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &msg, sizeof( GENERIC_MESSAGE ), NULL, NULL, &TimeOut );
}

//�ν��Ͻ� ���� �Լ�
NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] �ν��Ͻ� �����\n" );

    return STATUS_SUCCESS;
}

// ��Ʈ ���� �ݹ�
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] ��Ʈ �����\n" );
    return STATUS_SUCCESS;
}

// ��Ʈ ���� ���� �ݹ�
VOID PortDisconnect( PVOID ConnectionCookie )
{
    //UserConsole�� ��� ��Ʈ�� ���� �Ǿ��ٸ� ����
    if( gClientPort != NULL ) {
        FltCloseClientPort( gFilterHandle, &gClientPort ); //����� ��Ʈ �ݱ�
        gClientPort = NULL; //��� ��Ʈ �ʱ�ȭ
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[-] ��Ʈ ���� ����\n" );
}

// IRP_MJ_CREATE PreCallback
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    if( gClientPort == NULL ) //UserConsole�� ��� ��Ʈ�� ������� ���� ��� �޼����� ���� �� �����Ƿ� ����
        return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    GENERIC_MESSAGE MSG = {};
    MSG.Type = MessageTypeIrpCreate;
    MSG.IrpInfo.IsPost = FALSE; //Pre��� ���� ǥ��

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //���� ��û�� ������ ��� ������ ������. FLT_FILE_NAME_NORMALIZED: ����ȭ�� ���� ���, FLT_FILE_NAME_QUERY_DEFAULT: �ý����� �Ǵ��� ���� �̸�.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo ����ü�� ������� �Ľ��ؼ� .Volume, .FinalComponent, .Extension ������ �и�.
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, nameInfo->Name.Buffer );
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation���� �Ҵ�� �޸� ����
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, L"<Unknown>" );
    }

    // ����̹����� UserConsole�� �޽����� ����. &info(IRP_CREATE_INFO): ������ ������ ����ü. UserConsole���� FilterGetMessage()�� ����
    FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK; //�� ��û�� ���� �߰� ó���� ���� �ʰڴٰ� ��ȯ
}

// IRP_MJ_CREATE PostCallback
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    if( gClientPort == NULL ) //UserConsole�� ��� ��Ʈ�� ������� ���� ��� �޼����� ���� �� �����Ƿ� ����
        return FLT_POSTOP_FINISHED_PROCESSING;

    GENERIC_MESSAGE MSG = {};
    MSG.Type = MessageTypeIrpCreate;
	MSG.IrpInfo.IsPost = TRUE;
    MSG.IrpInfo.ResultStatus = Data->IoStatus.Status; //ResultStatus�� IRPó�� ����� ���� (ex. STATUS_SUCCESS, STATUS_ACCESS_DENIED ��)

    PFLT_FILE_NAME_INFORMATION nameInfo;
    //���� ��û�� ������ ��� ������ ������. FLT_FILE_NAME_NORMALIZED: ����ȭ�� ���� ���, FLT_FILE_NAME_QUERY_DEFAULT: �ý����� �Ǵ��� ���� �̸�.
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo ); //nameInfo ����ü�� ������� �Ľ��ؼ� .Volume, .FinalComponent, .Extension ������ �и�.
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, nameInfo->Name.Buffer ); //�����ϰ� ���ڿ��� ����, 260�� �ִ� ��� ����(MAX_PATH)
        FltReleaseFileNameInformation( nameInfo ); //FltGetFilaNameInformation���� �Ҵ�� �޸� ����
    }
    else {
        RtlStringCchCopyW( MSG.IrpInfo.ImageName, 260, L"<Unknown>" );
    }

    // ����̹����� UserConsole�� �޽����� ����. &info(IRP_CREATE_INFO): ������ ������ ����ü. UserConsole���� FilterGetMessage()�� ����
    FltSendMessage( gFilterHandle, &gClientPort, &MSG, sizeof( MSG ), NULL, NULL, NULL );

    return FLT_POSTOP_FINISHED_PROCESSING; //�� ��û�� ���� �߰� ó���� ���� �ʰڴٰ� ��ȯ
}

// Operation Registration
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },//IRP_MJ_CREATE�δ� Pre�� Post �ݹ�
    { IRP_MJ_OPERATION_END }
};

// Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    if( gServerPort != NULL )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle != NULL )
        FltUnregisterFilter( gFilterHandle );

    PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, TRUE );
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[-] ����̹� ��ε� �Ϸ�\n" );
    return STATUS_SUCCESS;
}

// Entry Point
extern "C"
NTSTATUS DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
    NTSTATUS status;

    //���� ���
    FLT_REGISTRATION filterRegistration = {
        sizeof( FLT_REGISTRATION ),
        FLT_REGISTRATION_VERSION,
        0,
        NULL,
        Callbacks,
        DriverUnload,
        InstanceSetupCallback,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };

    status = FltRegisterFilter( DriverObject, &filterRegistration, &gFilterHandle );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltRegisterFilter ����: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltRegisterFilter ����\n" );

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "SecurityDescriptor ���� ����: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    InitializeObjectAttributes( &oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd );

    // UserConsole Ŀ�´����̼� ��Ʈ ���
    status = FltCreateCommunicationPort(
        gFilterHandle,
        &gServerPort,
        &oa,
        NULL,
        PortConnect,
        PortDisconnect,
        NULL,
        1
    );

    FltFreeSecurityDescriptor( sd ); //Driver�� UserMode ���� ����

    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltCreateCommunicationPort ����: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltCreateCommunicationPort ����\n" );

    status = FltStartFiltering( gFilterHandle );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltStartFiltering ����: 0x%X\n", status );
        FltCloseCommunicationPort( gServerPort );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    ExInitializeFastMutex( &g_ProcessListLock );
    InitializeListHead( &g_ProcessNameList );

    status = PsSetCreateProcessNotifyRoutineEx( ProcessNotifyEx, FALSE );
    if( !NT_SUCCESS( status ) ) 
    {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "ProcessNotify ��� ����: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltStartFiltering ���� - ����̹� �ε� �Ϸ�\n" );

    return STATUS_SUCCESS;
}