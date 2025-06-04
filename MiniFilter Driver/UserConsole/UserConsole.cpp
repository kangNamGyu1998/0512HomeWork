#include "UserConsole.hpp"

//////////////////////////////////////////////////

void DescribeCreateOptions( ULONG options, WCHAR* buffer, size_t bufferLen )
{
    buffer[0] = L'\0';

    bool first = true;
    for ( int i = 0; i < sizeof( flags ) / sizeof( flags[0] ); ++i )
    {
        if ( options & flags[i].Flag )
        {
            if ( first == false ) wcsncat_s( buffer, bufferLen, L" | ", 3 );
            wcsncat_s( buffer, bufferLen, flags[i].Name, _TRUNCATE );
            first = false;
        }
    }

    if ( first == true )
        wcsncpy_s( buffer, bufferLen, L"NONE", _TRUNCATE );
}

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
            if ( IrpInfo->IsPost )
            {
                WCHAR errMsg[128] = L"";
                DWORD winErr = RtlNtStatusToDosError( IrpInfo->ResultStatus );
                FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, winErr, 0, errMsg, 128, NULL );
                errMsg[wcslen( errMsg ) - 2] = L'\0';

                wprintf( L"IRP : IRP_MJ_CREATE( Post ), PID : %lu, ParentPID : %lu, Proc Name : %ws, File : %ws, Result : %ws\n",
                    IrpInfo->ProcessId,
                    IrpInfo->ParentProcessId,
                    IrpInfo->ProcName,
                    IrpInfo->FileName,
                    errMsg );
            }
            else
            {
                WCHAR optDesc[256] = L"";
                DescribeCreateOptions( IrpInfo->CreateOptions, optDesc, ARRAYSIZE( optDesc ) );

                wprintf( L"IRP : IRP_MJ_CREATE( Pre ), PID : %lu, ParentPID : %lu, Proc Name : %ws, File : %ws, CreateOptions : 0x%08X ( %s )\n",
                    IrpInfo->ProcessId,
                    IrpInfo->ParentProcessId,
                    IrpInfo->ProcName,
                    IrpInfo->FileName,
                    IrpInfo->CreateOptions,
                    optDesc );
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
