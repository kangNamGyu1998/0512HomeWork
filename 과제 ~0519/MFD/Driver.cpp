#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\FilterPort\\ProcessMonitorPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// ��ſ� ����� ����ü
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;  // Post���� ���
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

// ��Ʈ ���� �ݹ�
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext,
    ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ServerPortCookie );
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext );
    UNREFERENCED_PARAMETER( ConnectionCookie );

    gClientPort = ClientPort;
    return STATUS_SUCCESS;
}

//��Ʈ ���� ���� �ݹ�
VOID PortDisconnect( PVOID ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ConnectionCookie );
    if( gClientPort ) {
        FltCloseClientPort( gFilterHandle, &gClientPort );
        gClientPort = NULL;
    }
}

// IRP_MJ_CREATE PreCallback/ó���ϱ� �� ���� �̸��� ���� ���μ����� ����
FLT_PREOP_CALLBACK_STATUS PreCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    if( !gClientPort ) return FLT_PREOP_SUCCESS_WITH_CALLBACK;

    IRP_CREATE_INFO info = { 0 };
    info.IsPost = FALSE;

    PFLT_FILE_NAME_INFORMATION nameInfo;
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo );
        RtlStringCchCopyW( info.FileName, 260, nameInfo->Name.Buffer );
        FltReleaseFileNameInformation( nameInfo );
    }
    else {
        RtlStringCchCopyW( info.FileName, 260, L"<Unknown>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &info, sizeof( info ), NULL, NULL, NULL );

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

// IRP_MJ_CREATE PostCallback/ó���� �� ���� �̸��� ���� ���θ� ���� ���μ����� ����
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback( PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    if( !gClientPort )
        return FLT_POSTOP_FINISHED_PROCESSING;

    IRP_CREATE_INFO info = { 0 };
    info.IsPost = TRUE;
    info.ResultStatus = Data->IoStatus.Status;

    PFLT_FILE_NAME_INFORMATION nameInfo;
    if( NT_SUCCESS( FltGetFileNameInformation( Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo ) ) ) {
        FltParseFileNameInformation( nameInfo );
        RtlStringCchCopyW( info.FileName, 260, nameInfo->Name.Buffer );
        FltReleaseFileNameInformation( nameInfo );
    }
    else {
        RtlStringCchCopyW( info.FileName, 260, L"<Unknown>" );
    }

    FltSendMessage( gFilterHandle, &gClientPort, &info, sizeof( info ), NULL, NULL, NULL );

    return FLT_POSTOP_FINISHED_PROCESSING;
}

// Operation Registration
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback, NULL },
    { IRP_MJ_OPERATION_END }
};

// Driver Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    UNREFERENCED_PARAMETER( Flags );

    if( gServerPort )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle )
        FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}

// Entry Point
extern "C"
NTSTATUS DriverEntry( _In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath )
{
    DbgPrint( "DriverEntry ����\n" );
    UNREFERENCED_PARAMETER( RegistryPath );
    NTSTATUS status;

    FLT_REGISTRATION filterRegistration = {
    .Size = sizeof( FLT_REGISTRATION ),
    .Version = FLT_REGISTRATION_VERSION,
    .Flags = 0,
    .ContextRegistration = NULL,
    .OperationRegistration = Callbacks,
    .FilterUnloadCallback = DriverUnload,
    .InstanceSetupCallback = NULL,
    .InstanceQueryTeardownCallback = NULL,
    .InstanceTeardownStartCallback = NULL,
    .InstanceTeardownCompleteCallback = NULL,
    .GenerateFileNameCallback = NULL,
    .NormalizeNameComponentCallback = NULL,
    .NormalizeContextCleanupCallback = NULL,
    .TransactionNotificationCallback = NULL,
    .NormalizeNameComponentExCallback = NULL
    };

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes( &oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL ); // p, n, a, r, s

    status = FltRegisterFilter( DriverObject, &filterRegistration, &gFilterHandle );
    DbgPrint( "FltRegisterFilter ���: 0x%x\n", status );
    if( !NT_SUCCESS( status ) ) 
        return status;

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
    
    
    DbgPrint( "FltCreateCommunicationPort ���: 0x%x\n", status );
    if( !NT_SUCCESS( status ) ) {
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    return FltStartFiltering( gFilterHandle );
}
