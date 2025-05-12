#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include <ntstrsafe.h>

// ���� �ڵ�, Ŀ�´����̼� ��Ʈ ����
PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

// ����ڿ� ����� ��Ʈ �̸� ����
#define COMM_PORT_NAME L"\\FilterPort\\ProcessMonitorPort"

// ���� �ֿܼ� ���� ���μ��� ���� ����ü
typedef struct _PROCESS_INFO {
    ULONG ProcessId;
    WCHAR ImageName[260];
} PROCESS_INFO, * PPROCESS_INFO;

// ��Ʈ ���� �ݹ� - ���� ��� ���ø����̼ǰ� ����� �� ȣ���
NTSTATUS PortConnect(PFLT_PORT ClientPort, PVOID ServerPortCookie, PVOID ConnectionContext,
    ULONG SizeOfContext, PVOID* ConnectionCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionCookie);

    gClientPort = ClientPort;
    DbgPrint("��Ʈ �����\n");
    return STATUS_SUCCESS;
}

// ��Ʈ ���� ���� �ݹ� - ���� ��� ���ø����̼��� ������ ���� �� ȣ���
VOID PortDisconnect(PVOID ConnectionCookie) {
    UNREFERENCED_PARAMETER(ConnectionCookie);

    if (gClientPort) {
        FltCloseClientPort(gFilterHandle, &gClientPort);
        DbgPrint("��Ʈ ���� ������\n");
    }
}

// ���μ��� ���� �Ǵ� ���� �� ȣ��Ǵ� �ݹ�
// ���� �ֿܼ� �޽����� ������
VOID ProcessNotifyRoutineEx(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    UNREFERENCED_PARAMETER(Process);

    if (!gClientPort) return;

    PROCESS_INFO info = { 0 };
    info.ProcessId = (ULONG)(ULONG_PTR)ProcessId;

    if (CreateInfo) {
        if (CreateInfo->ImageFileName) {
            RtlStringCchCopyW(info.ImageName, 260, CreateInfo->ImageFileName->Buffer);
        }
        else {
            RtlStringCchCopyW(info.ImageName, 260, L"<Unknown>");
        }
    }
    else {
        RtlStringCchCopyW(info.ImageName, 260, L"<Terminated>");
    }

    // ���� �ַܼ� �޽��� ����
    FltSendMessage(gFilterHandle, &gClientPort, &info, sizeof(info), NULL, NULL, NULL);
}

// ����̹��� ��ε�� �� ȣ���
// ��ϵ� �ݹ�� ��Ʈ�� ������
NTSTATUS DriverUnload(FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, TRUE);

    if (gServerPort) {
        FltCloseCommunicationPort(gServerPort);
    }

    if (gFilterHandle) {
        FltUnregisterFilter(gFilterHandle);
    }

    DbgPrint("����̹� ��ε� �Ϸ�\n");
    return STATUS_SUCCESS;
}

// ���Ͱ� ����� �۾� ���� (������ IRP_MJ_OPERATION_END�� �ݵ�� �ʿ���)
CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_OPERATION_END }
};

// ����̹� ������ - ����̹��� �ε�� �� ���� ���� �����
extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;

    // ���� ��� ���� ����ü �ʱ�ȭ
    FLT_REGISTRATION filterRegistration = {
        sizeof(FLT_REGISTRATION),        // ����ü ũ��
        FLT_REGISTRATION_VERSION,        // ����
        0,                               // Flags
        NULL,                            // Context
        Callbacks,                       // Operation callbacks
        DriverUnload,                    // Unload callback
        NULL, NULL, NULL, NULL,          // Instance callbacks
        NULL, NULL, NULL, NULL           // ��Ÿ �ݹ�
    };

    UNICODE_STRING uniName = RTL_CONSTANT_STRING(COMM_PORT_NAME);
    OBJECT_ATTRIBUTES oa;

    InitializeObjectAttributes(&oa, &uniName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    // ���� ���
    status = FltRegisterFilter(DriverObject, &filterRegistration, &gFilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrint("FltRegisterFilter ����: 0x%08X\n", status);
        return status;
    }

    // Ŀ�´����̼� ��Ʈ ����
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

    if (!NT_SUCCESS(status)) {
        DbgPrint("FltCreateCommunicationPort ����: 0x%08X\n", status);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }


    // ���μ��� ���� �˸� �ݹ� ���
    status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, FALSE);
    if (!NT_SUCCESS(status)) {
        DbgPrint("PsSetCreateProcessNotifyRoutineEx ����: 0x%08X\n", status);
        FltCloseCommunicationPort(gServerPort);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    // ���͸� ����
    status = FltStartFiltering(gFilterHandle);
    if (!NT_SUCCESS(status)) {
        DbgPrint("FltStartFiltering ����: 0x%08X\n", status);
        PsSetCreateProcessNotifyRoutineEx(ProcessNotifyRoutineEx, TRUE);
        FltCloseCommunicationPort(gServerPort);
        FltUnregisterFilter(gFilterHandle);
        return status;
    }

    DbgPrint("����̹� �ε� ����\n");
    return STATUS_SUCCESS;
}