#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <fltuser.h>
#include <locale.h>
#pragma comment( lib, "FltLib.lib" )
#pragma comment( lib, "ntdll.lib" )

#define COMM_PORT_NAME L"\\MFDPort"

#pragma pack( push ,1 )
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

typedef struct _IRP_CONTEXT {
    BOOLEAN IsPost;
    ULONG ProcessId;
    ULONG ParentProcessId;
    WCHAR ProcName[260];
    WCHAR FileName[260];
    NTSTATUS CreateOptions;
    NTSTATUS ResultStatus;
} IRP_CONTEXT, * PIRP_CONTEXT;

typedef struct _GENERIC_MESSAGE {
    MESSAGE_TYPE Type;
    union {
        IRP_CONTEXT IrpInfo;
        PROC_EVENT_INFO ProcInfo;
    };
} GENERIC_MESSAGE, * PGENERIC_MESSAGE;

typedef struct _MESSAGE_BUFFER {
    FILTER_MESSAGE_HEADER MessageHeader;
    GENERIC_MESSAGE       MessageBody;
} MESSAGE_BUFFER, * PMESSAGE_BUFFER;
#pragma pack( pop )

//////////////////////////////////////////////////

int main( ) {
    _wsetlocale( LC_ALL, L"Korean" );
    HANDLE hPort = NULL;
    HRESULT hr = FilterConnectCommunicationPort( 
        COMM_PORT_NAME,
        0,
        NULL,
        0,
        NULL,
        &hPort
    );

    if( FAILED( hr ) ) {
        wprintf( L"[ ! ] ��Ʈ ���ῡ �����߽��ϴ�: 0x%08X\n", hr );
        return 1;
    }
    wprintf( L"[ + ] ��Ʈ ���ῡ �����߽��ϴ�.\n", hr );

    wprintf( L"��ġ: C:\\Dev\\UserConsole.exe\n" );
    wprintf( L"Connect MiniFilter...\n" );

    while( true ) {
        MESSAGE_BUFFER messageBuffer = { 0 };

        hr = FilterGetMessage( 
            hPort,
            &messageBuffer.MessageHeader,
            sizeof( messageBuffer ),
            NULL // Optional: Timeout
        );

        if( FAILED( hr ) ) {
            wprintf( L"[ ! ] ����̹��κ��� �޼��� �޾ƿ��� ����: 0x%08X\n", hr );
            break;
        }

        const auto IrpInfo = &messageBuffer.MessageBody.IrpInfo;
        const auto ProcpInfo = &messageBuffer.MessageBody.ProcInfo;

        switch( messageBuffer.MessageBody.Type )
        {
        case MessageTypeIrpCreate: {
            if (IrpInfo->IsPost)
            {
                WCHAR errMsg[128] = L"";
                DWORD winErr = RtlNtStatusToDosError(IrpInfo->ResultStatus);
                FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, winErr, 0, errMsg, 128, NULL);
                errMsg[wcslen(errMsg) - 2] = L'\0'; // \r\n ����

                wprintf(L"IRP : IRP_MJ_CREATE(Post), PID : %lu, ParentPID : %lu, Proc Name : %ws, File : %ws, Result : %ws\n",
                    IrpInfo->ProcessId,
                    IrpInfo->ParentProcessId,
                    IrpInfo->ProcName,
                    IrpInfo->FileName,
                    errMsg);
            }
            else
            {
                wprintf(L"IRP : IRP_MJ_CREATE(Pre), PID : %lu, ParentPID : %lu, Proc Name : %ws, File : %ws, CreateOptions : 0x%08X\n",
                    IrpInfo->ProcessId,
                    IrpInfo->ParentProcessId,
                    IrpInfo->ProcName,
                    IrpInfo->FileName,
                    IrpInfo->CreateOptions);
            }
        } break;
        default: {
            wprintf( L"[ ! ] �� �� ���� �޽��� Ÿ��: %d\n", messageBuffer.MessageBody.Type );
	        }
		}
    }
    CloseHandle( hPort );
    return 0;
}
