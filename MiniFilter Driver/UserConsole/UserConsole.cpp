#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <fltuser.h>
#include <locale.h>
#pragma comment(lib, "FltLib.lib")

#define COMM_PORT_NAME L"\\MFDPort"

// ����̹��� ������ ����ü ����
typedef struct _IRP_CREATE_INFO {
    BOOLEAN IsPost;  // FALSE: Pre, TRUE: Post
    NTSTATUS ResultStatus;  // Post���� ���
    WCHAR FileName[ 260 ];
} IRP_CREATE_INFO, * PIRP_CREATE_INFO;

typedef struct _PROC_EVENT_INFO {
    BOOLEAN IsCreate; // TRUE = ����, FALSE = ����
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

typedef struct _MESSAGE_BUFFER {
    FILTER_MESSAGE_HEADER MessageHeader;
    GENERIC_MESSAGE       MessageBody;
} MESSAGE_BUFFER, * PMESSAGE_BUFFER;

//////////////////////////////////////////////////

int main() {
    _wsetlocale(LC_ALL, L"Korean"); //UserConsole���� �ѱ��� Ȯ���� �� ����
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
        wprintf( L"[!] ��Ʈ ���ῡ �����߽��ϴ�: 0x%08X\n", hr );
        return 1;
    }
    wprintf( L"[*] ��Ʈ ���ῡ �����߽��ϴ�.\n", hr );

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
            wprintf( L"[!] ����̹��κ��� �޼��� �޾ƿ��� ����: 0x%08X\n", hr );
            break;
        }

        const auto IrpInfo = &messageBuffer.MessageBody.IrpInfo;
        const auto ProcpInfo = &messageBuffer.MessageBody.ProcInfo;

        switch( messageBuffer.MessageBody.Type )
        {
			case MessageTypeIrpCreate:
			{
				if( IrpInfo->IsPost ) 
                {
					//                wprintf( L"IRP : IRP_MJ_CREATE, Type : Post, File : %s, Result : %s\n",
					//                    IrpInfo->FileName,
					//                    NT_SUCCESS( IrpInfo->ResultStatus ) ? L"Success" : L"Failure" );
					//            }
					//            else {
					//                wprintf( L"IRP : IRP_MJ_CREATE, Type : Pre, File : %s\n", IrpInfo->FileName );
					//            }
				}break;
			case MessageTypeProcEvent:
			{
			    if( ProcpInfo->IsCreate ) {
			        wprintf( L"IRP : Proc Created, PID : %lu, ParentPID : %lu, Name : %ws\n",
			            ProcpInfo->ProcessId, ProcpInfo->ParentProcessId, ProcpInfo->ImageName );
			    }
			    else {
			        wprintf( L"IRP : Proc Terminated, PID : %lu, Name : %ws\n",
			            ProcpInfo->ProcessId, ProcpInfo->ImageName );
			    }
			}break;
			}
        }
    }
    CloseHandle( hPort );
    return 0;
}
