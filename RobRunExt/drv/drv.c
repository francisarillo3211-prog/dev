#include <ntifs.h>

#define DEVICE_NAME  L"\\Device\\RobRunDrv"
#define SYMLINK_NAME L"\\DosDevices\\RobRunDrv"

#define IOCTL_READ  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _MEM_REQ {
    ULONG   pid;
    ULONG   pad;
    ULONG64 src;   /* target address */
    ULONG64 dst;   /* our buffer      */
    ULONG64 size;
} MEM_REQ;

static NTSTATUS copy_req(PIRP irp, int is_write) {
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(irp);
    if (sl->Parameters.DeviceIoControl.InputBufferLength < sizeof(MEM_REQ))
        return STATUS_BUFFER_TOO_SMALL;

    MEM_REQ* r = (MEM_REQ*)irp->AssociatedIrp.SystemBuffer;
    PEPROCESS p = NULL;
    NTSTATUS s = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)r->pid, &p);
    if (!NT_SUCCESS(s)) return s;

    SIZE_T done = 0;
    if (is_write)
        s = MmCopyVirtualMemory(PsGetCurrentProcess(), (PVOID)r->dst,
                                p, (PVOID)r->src, (SIZE_T)r->size, KernelMode, &done);
    else
        s = MmCopyVirtualMemory(p, (PVOID)r->src,
                                PsGetCurrentProcess(), (PVOID)r->dst, (SIZE_T)r->size, KernelMode, &done);

    ObDereferenceObject(p);
    if (NT_SUCCESS(s)) irp->IoStatus.Information = sizeof(MEM_REQ);
    return s;
}

static NTSTATUS dispatch(PDEVICE_OBJECT dev, PIRP irp) {
    UNREFERENCED_PARAMETER(dev);
    PIO_STACK_LOCATION sl = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;

    switch (sl->MajorFunction) {
    case IRP_MJ_CREATE:
    case IRP_MJ_CLOSE:
        break;
    case IRP_MJ_DEVICE_CONTROL:
        switch (sl->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_READ:  status = copy_req(irp, 0); break;
        case IOCTL_WRITE: status = copy_req(irp, 1); break;
        default:          status = STATUS_INVALID_DEVICE_REQUEST;
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    UNREFERENCED_PARAMETER(reg);
    PDEVICE_OBJECT dev = NULL;
    UNICODE_STRING name, sym;
    RtlInitUnicodeString(&name, DEVICE_NAME);
    RtlInitUnicodeString(&sym, SYMLINK_NAME);

    NTSTATUS s = IoCreateDevice(drv, 0, &name, FILE_DEVICE_UNKNOWN,
                                FILE_DEVICE_SECURE_OPEN, FALSE, &dev);
    if (!NT_SUCCESS(s)) return s;

    s = IoCreateSymbolicLink(&sym, &name);
    if (!NT_SUCCESS(s)) { IoDeleteDevice(dev); return s; }

    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) drv->MajorFunction[i] = dispatch;
    drv->Flags |= DO_BUFFERED_IO;
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}