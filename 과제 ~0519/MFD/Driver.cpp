#include <fltKernel.h>
#include <ntstrsafe.h>

#define COMM_PORT_NAME L"\\MFDPort"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// ��ſ� ����� ����ü
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    WCHAR FileName[ 260 ];
    NTSTATUS ResultStatus;  // Post���� ���
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

//�ν��Ͻ� ���� �Լ�
NTSTATUS InstanceSetupCallback( PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags,  DEVICE_TYPE VolumeDeviceType, FLT_FILESYSTEM_TYPE VolumeFilesystemType )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );
    
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] �ν��Ͻ� �����\n" );
    
    return STATUS_SUCCESS;
}

// ��Ʈ ���� �ݹ�
NTSTATUS PortConnect( PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext, ULONG SizeOfContext, PVOID* ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ServerPortCookie ); //UNREFERENCED_PARAMETER: ���ڰ��̳� ���� ������ ���� ������� �ʾ����� �����Ϸ� ��� �߻���Ű�� �ʰ� �ϱ� ���� ��ũ��
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext );
    UNREFERENCED_PARAMETER( ConnectionCookie );

    gClientPort = ClientPort;
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] ��Ʈ �����\n" );
    return STATUS_SUCCESS;
}

VOID PortDisconnect( PVOID ConnectionCookie )
{
    UNREFERENCED_PARAMETER( ConnectionCookie );
    if( gClientPort ) {
        FltCloseClientPort( gFilterHandle, &gClientPort );
        gClientPort = NULL;
    }
    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "[+] ��Ʈ ���� ����\n" );
}

// IRP_MJ_CREATE PreCallback
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
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },
    { IRP_MJ_OPERATION_END }
};

// Unload
NTSTATUS DriverUnload( FLT_FILTER_UNLOAD_FLAGS Flags )
{
    UNREFERENCED_PARAMETER( Flags );

    if( gServerPort )
        FltCloseCommunicationPort( gServerPort );

    if( gFilterHandle )
        FltUnregisterFilter( gFilterHandle );

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "����̹� ��ε� �Ϸ�\n" );
    return STATUS_SUCCESS;
}

// Entry Point
extern "C"
NTSTATUS DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
    UNREFERENCED_PARAMETER( RegistryPath );
    NTSTATUS status;

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "DriverEntry ����\n" );

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

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FLT_REGISTRATION ���� �Ϸ�\n" );

    status = FltRegisterFilter( DriverObject, &filterRegistration, &gFilterHandle );
    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltRegisterFilter ����: 0x%X\n", status );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltRegisterFilter ����\n" );

    UNICODE_STRING uniName = RTL_CONSTANT_STRING( COMM_PORT_NAME );
    OBJECT_ATTRIBUTES oa;
    PSECURITY_DESCRIPTOR sd = NULL;

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );
    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "SecurityDescriptor ���� ����: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    InitializeObjectAttributes( &oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd );

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

    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltCreateCommunicationPort ����: 0x%X\n", status );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltCreateCommunicationPort ����\n" );

    status = FltStartFiltering( gFilterHandle );
    if( !NT_SUCCESS( status ) ) {
        DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "FltStartFiltering ����: 0x%X\n", status );
        FltCloseCommunicationPort( gServerPort );
        FltUnregisterFilter( gFilterHandle );
        return status;
    }

    DbgPrintEx( DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, "FltStartFiltering ���� - ����̹� �ε� �Ϸ�\n" );

    return STATUS_SUCCESS;
}