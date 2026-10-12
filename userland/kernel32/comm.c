/*
 * comm.c — serial ports: the communications functions
 *
 * A COM port is a device a Windows serial driver made (serial.sys's
 * interface, as FTDI's ftser2k.sys and every USB serial driver offer it);
 * a program opens it as "COM3" or "\\.\COM3" and sets it up through these
 * calls.  Each is the IOCTL_SERIAL_* request Windows' kernelbase sends
 * (ntddser.h), so the driver sees the same requests it does on Windows:
 * a DCB becomes the baud rate, line control, special characters and
 * handshake flow it describes, and comes back from them again.
 */

#define NOVA_BUILD_KERNEL32
#include <winternl.h>
#include "k32.h"

void *memset(void *d, int c, size_t n);

#define K32 __declspec(dllexport)

/* ntddser.h: CTL_CODE(FILE_DEVICE_SERIAL_PORT, n, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define SER(n) (0x001B0000u | ((n) << 2))
#define IOCTL_SERIAL_SET_BAUD_RATE     SER(1)
#define IOCTL_SERIAL_SET_QUEUE_SIZE    SER(2)
#define IOCTL_SERIAL_SET_LINE_CONTROL  SER(3)
#define IOCTL_SERIAL_SET_BREAK_ON      SER(4)
#define IOCTL_SERIAL_SET_BREAK_OFF     SER(5)
#define IOCTL_SERIAL_IMMEDIATE_CHAR    SER(6)
#define IOCTL_SERIAL_SET_TIMEOUTS      SER(7)
#define IOCTL_SERIAL_GET_TIMEOUTS      SER(8)
#define IOCTL_SERIAL_SET_DTR           SER(9)
#define IOCTL_SERIAL_CLR_DTR           SER(10)
#define IOCTL_SERIAL_RESET_DEVICE      SER(11)
#define IOCTL_SERIAL_SET_RTS           SER(12)
#define IOCTL_SERIAL_CLR_RTS           SER(13)
#define IOCTL_SERIAL_SET_XOFF          SER(14)
#define IOCTL_SERIAL_SET_XON           SER(15)
#define IOCTL_SERIAL_GET_WAIT_MASK     SER(16)
#define IOCTL_SERIAL_SET_WAIT_MASK     SER(17)
#define IOCTL_SERIAL_WAIT_ON_MASK      SER(18)
#define IOCTL_SERIAL_PURGE             SER(19)
#define IOCTL_SERIAL_GET_BAUD_RATE     SER(20)
#define IOCTL_SERIAL_GET_LINE_CONTROL  SER(21)
#define IOCTL_SERIAL_GET_CHARS         SER(22)
#define IOCTL_SERIAL_SET_CHARS         SER(23)
#define IOCTL_SERIAL_GET_HANDFLOW      SER(24)
#define IOCTL_SERIAL_SET_HANDFLOW      SER(25)
#define IOCTL_SERIAL_GET_MODEMSTATUS   SER(26)
#define IOCTL_SERIAL_GET_COMMSTATUS    SER(27)
#define IOCTL_SERIAL_GET_PROPERTIES    SER(29)

typedef struct { UCHAR StopBits, Parity, WordLength; } SerLineControl;
typedef struct { UCHAR EofChar, ErrorChar, BreakChar, EventChar, XonChar, XoffChar; } SerChars;
typedef struct { ULONG ControlHandShake, FlowReplace; LONG XonLimit, XoffLimit; } SerHandflow;
typedef struct { ULONG InSize, OutSize; } SerQueueSize;
typedef struct { ULONG Errors, HoldReasons, AmountInInQueue, AmountInOutQueue; BOOLEAN EofReceived, WaitForImmediate; } SerStatus;

/* SERIAL_HANDFLOW.ControlHandShake */
#define SERIAL_DTR_CONTROL      0x01
#define SERIAL_DTR_HANDSHAKE    0x02
#define SERIAL_CTS_HANDSHAKE    0x08
#define SERIAL_DSR_HANDSHAKE    0x10
#define SERIAL_DSR_SENSITIVITY  0x40
#define SERIAL_ERROR_ABORT      0x80000000u
/* .FlowReplace */
#define SERIAL_AUTO_TRANSMIT    0x01
#define SERIAL_AUTO_RECEIVE     0x02
#define SERIAL_ERROR_CHAR       0x04
#define SERIAL_NULL_STRIPPING   0x08
#define SERIAL_RTS_CONTROL      0x40
#define SERIAL_RTS_HANDSHAKE    0x80
#define SERIAL_TRANSMIT_TOGGLE  0xC0
#define SERIAL_XOFF_CONTINUE    0x80000000u

/* The request, synchronously (a serial driver may pend it) */
static BOOL ser(HANDLE h, ULONG code, void *in, ULONG in_n, void *out, ULONG out_n, ULONG *got)
{
    IO_STATUS_BLOCK io = { 0 };
    NTSTATUS s = NtDeviceIoControlFile(h, 0, 0, 0, &io, code, in, in_n, out, out_n);
    if (s == STATUS_PENDING) {
        NtWaitForSingleObject(h, FALSE, NULL);
        s = io.Status;
    }
    if (got) *got = NT_SUCCESS(s) ? (ULONG)io.Information : 0;
    return NT_SUCCESS(s) ? TRUE : fail_status(s);
}

K32 BOOL WINAPI SetCommState(HANDLE h, LPDCB dcb)
{
    if (!dcb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    ULONG baud = dcb->BaudRate;
    SerLineControl lc = { dcb->StopBits, dcb->Parity, dcb->ByteSize };
    SerChars ch = { (UCHAR)dcb->EofChar, (UCHAR)dcb->ErrorChar, 0, (UCHAR)dcb->EvtChar, (UCHAR)dcb->XonChar,
                    (UCHAR)dcb->XoffChar };
    SerHandflow hf = { 0, 0, dcb->XonLim, dcb->XoffLim };
    if (dcb->fDtrControl == DTR_CONTROL_ENABLE) hf.ControlHandShake |= SERIAL_DTR_CONTROL;
    if (dcb->fDtrControl == DTR_CONTROL_HANDSHAKE) hf.ControlHandShake |= SERIAL_DTR_HANDSHAKE;
    if (dcb->fOutxCtsFlow) hf.ControlHandShake |= SERIAL_CTS_HANDSHAKE;
    if (dcb->fOutxDsrFlow) hf.ControlHandShake |= SERIAL_DSR_HANDSHAKE;
    if (dcb->fDsrSensitivity) hf.ControlHandShake |= SERIAL_DSR_SENSITIVITY;
    if (dcb->fAbortOnError) hf.ControlHandShake |= SERIAL_ERROR_ABORT;
    if (dcb->fRtsControl == RTS_CONTROL_ENABLE) hf.FlowReplace |= SERIAL_RTS_CONTROL;
    if (dcb->fRtsControl == RTS_CONTROL_HANDSHAKE) hf.FlowReplace |= SERIAL_RTS_HANDSHAKE;
    if (dcb->fRtsControl == RTS_CONTROL_TOGGLE) hf.FlowReplace |= SERIAL_TRANSMIT_TOGGLE;
    if (dcb->fOutX) hf.FlowReplace |= SERIAL_AUTO_TRANSMIT;
    if (dcb->fInX) hf.FlowReplace |= SERIAL_AUTO_RECEIVE;
    if (dcb->fErrorChar) hf.FlowReplace |= SERIAL_ERROR_CHAR;
    if (dcb->fNull) hf.FlowReplace |= SERIAL_NULL_STRIPPING;
    if (dcb->fTXContinueOnXoff) hf.FlowReplace |= SERIAL_XOFF_CONTINUE;
    if (!ser(h, IOCTL_SERIAL_SET_BAUD_RATE, &baud, sizeof(baud), 0, 0, 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_SET_LINE_CONTROL, &lc, sizeof(lc), 0, 0, 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_SET_HANDFLOW, &hf, sizeof(hf), 0, 0, 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_SET_CHARS, &ch, sizeof(ch), 0, 0, 0)) return FALSE;
    /* DTR and RTS a handshake does not drive are set as the DCB says */
    if (dcb->fDtrControl != DTR_CONTROL_HANDSHAKE &&
        !ser(h, dcb->fDtrControl == DTR_CONTROL_ENABLE ? IOCTL_SERIAL_SET_DTR : IOCTL_SERIAL_CLR_DTR, 0, 0, 0, 0, 0))
        return FALSE;
    if (dcb->fRtsControl != RTS_CONTROL_HANDSHAKE && dcb->fRtsControl != RTS_CONTROL_TOGGLE &&
        !ser(h, dcb->fRtsControl == RTS_CONTROL_ENABLE ? IOCTL_SERIAL_SET_RTS : IOCTL_SERIAL_CLR_RTS, 0, 0, 0, 0, 0))
        return FALSE;
    return TRUE;
}

K32 BOOL WINAPI GetCommState(HANDLE h, LPDCB dcb)
{
    if (!dcb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    ULONG baud = 0;
    SerLineControl lc;
    SerChars ch;
    SerHandflow hf;
    if (!ser(h, IOCTL_SERIAL_GET_BAUD_RATE, 0, 0, &baud, sizeof(baud), 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_GET_LINE_CONTROL, 0, 0, &lc, sizeof(lc), 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_GET_HANDFLOW, 0, 0, &hf, sizeof(hf), 0)) return FALSE;
    if (!ser(h, IOCTL_SERIAL_GET_CHARS, 0, 0, &ch, sizeof(ch), 0)) return FALSE;
    memset(dcb, 0, sizeof(*dcb));
    dcb->DCBlength = sizeof(*dcb);
    dcb->BaudRate = baud;
    dcb->fBinary = 1;
    dcb->fParity = lc.Parity != NOPARITY;
    dcb->ByteSize = lc.WordLength;
    dcb->Parity = lc.Parity;
    dcb->StopBits = lc.StopBits;
    dcb->fDtrControl = hf.ControlHandShake & SERIAL_DTR_HANDSHAKE ? DTR_CONTROL_HANDSHAKE
                       : hf.ControlHandShake & SERIAL_DTR_CONTROL ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb->fOutxCtsFlow = !!(hf.ControlHandShake & SERIAL_CTS_HANDSHAKE);
    dcb->fOutxDsrFlow = !!(hf.ControlHandShake & SERIAL_DSR_HANDSHAKE);
    dcb->fDsrSensitivity = !!(hf.ControlHandShake & SERIAL_DSR_SENSITIVITY);
    dcb->fAbortOnError = !!(hf.ControlHandShake & SERIAL_ERROR_ABORT);
    switch (hf.FlowReplace & SERIAL_TRANSMIT_TOGGLE) {
    case SERIAL_RTS_CONTROL:     dcb->fRtsControl = RTS_CONTROL_ENABLE; break;
    case SERIAL_RTS_HANDSHAKE:   dcb->fRtsControl = RTS_CONTROL_HANDSHAKE; break;
    case SERIAL_TRANSMIT_TOGGLE: dcb->fRtsControl = RTS_CONTROL_TOGGLE; break;
    default:                     dcb->fRtsControl = RTS_CONTROL_DISABLE; break;
    }
    dcb->fOutX = !!(hf.FlowReplace & SERIAL_AUTO_TRANSMIT);
    dcb->fInX = !!(hf.FlowReplace & SERIAL_AUTO_RECEIVE);
    dcb->fErrorChar = !!(hf.FlowReplace & SERIAL_ERROR_CHAR);
    dcb->fNull = !!(hf.FlowReplace & SERIAL_NULL_STRIPPING);
    dcb->fTXContinueOnXoff = !!(hf.FlowReplace & SERIAL_XOFF_CONTINUE);
    dcb->XonLim = (WORD)hf.XonLimit;
    dcb->XoffLim = (WORD)hf.XoffLimit;
    dcb->EofChar = (char)ch.EofChar;
    dcb->ErrorChar = (char)ch.ErrorChar;
    dcb->EvtChar = (char)ch.EventChar;
    dcb->XonChar = (char)ch.XonChar;
    dcb->XoffChar = (char)ch.XoffChar;
    return TRUE;
}

/* COMMTIMEOUTS is SERIAL_TIMEOUTS */
K32 BOOL WINAPI SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    if (!t) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return ser(h, IOCTL_SERIAL_SET_TIMEOUTS, t, sizeof(*t), 0, 0, 0);
}

K32 BOOL WINAPI GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    if (!t) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return ser(h, IOCTL_SERIAL_GET_TIMEOUTS, 0, 0, t, sizeof(*t), 0);
}

K32 BOOL WINAPI SetupComm(HANDLE h, DWORD in, DWORD out)
{
    SerQueueSize q = { in, out };
    return ser(h, IOCTL_SERIAL_SET_QUEUE_SIZE, &q, sizeof(q), 0, 0, 0);
}

/* The EV_* mask is SERIAL_EV_* */
K32 BOOL WINAPI SetCommMask(HANDLE h, DWORD m) { return ser(h, IOCTL_SERIAL_SET_WAIT_MASK, &m, sizeof(m), 0, 0, 0); }

K32 BOOL WINAPI GetCommMask(HANDLE h, LPDWORD m)
{
    if (!m) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return ser(h, IOCTL_SERIAL_GET_WAIT_MASK, 0, 0, m, sizeof(*m), 0);
}

/* (An OVERLAPPED caller gets the event at once: device requests finish
 * before the call returns on NovaOS) */
K32 BOOL WINAPI WaitCommEvent(HANDLE h, LPDWORD m, LPOVERLAPPED o)
{
    DWORD ev = 0;
    BOOL ok = ser(h, IOCTL_SERIAL_WAIT_ON_MASK, 0, 0, &ev, sizeof(ev), 0);
    if (m) *m = ok ? ev : 0;
    if (o) {
        o->Internal = ok ? 0 : (ULONG_PTR)0xC0000001u /* STATUS_UNSUCCESSFUL */;
        o->InternalHigh = ok ? sizeof(ev) : 0;
        if (o->hEvent) SetEvent((HANDLE)((ULONG_PTR)o->hEvent & ~(ULONG_PTR)1));
    }
    return ok;
}

K32 BOOL WINAPI ClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT st)
{
    SerStatus s;
    memset(&s, 0, sizeof(s));
    if (!ser(h, IOCTL_SERIAL_GET_COMMSTATUS, 0, 0, &s, sizeof(s), 0)) return FALSE;
    if (errors) {
        DWORD e = 0;                                     /* SERIAL_ERROR_* -> CE_* */
        if (s.Errors & 0x01) e |= CE_BREAK;
        if (s.Errors & 0x02) e |= CE_FRAME;
        if (s.Errors & 0x04) e |= CE_OVERRUN;
        if (s.Errors & 0x08) e |= CE_RXOVER;
        if (s.Errors & 0x10) e |= CE_RXPARITY;
        *errors = e;
    }
    if (st) {
        memset(st, 0, sizeof(*st));
        st->fCtsHold = !!(s.HoldReasons & 0x01);
        st->fDsrHold = !!(s.HoldReasons & 0x02);
        st->fRlsdHold = !!(s.HoldReasons & 0x04);
        st->fXoffHold = !!(s.HoldReasons & 0x08);
        st->fXoffSent = !!(s.HoldReasons & 0x10);
        st->fEof = !!s.EofReceived;
        st->fTxim = !!s.WaitForImmediate;
        st->cbInQue = s.AmountInInQueue;
        st->cbOutQue = s.AmountInOutQueue;
    }
    return TRUE;
}

K32 BOOL WINAPI SetCommBreak(HANDLE h) { return ser(h, IOCTL_SERIAL_SET_BREAK_ON, 0, 0, 0, 0, 0); }
K32 BOOL WINAPI ClearCommBreak(HANDLE h) { return ser(h, IOCTL_SERIAL_SET_BREAK_OFF, 0, 0, 0, 0, 0); }

K32 BOOL WINAPI EscapeCommFunction(HANDLE h, DWORD f)
{
    static const ULONG code[] = { 0, IOCTL_SERIAL_SET_XOFF, IOCTL_SERIAL_SET_XON, IOCTL_SERIAL_SET_RTS,
                                  IOCTL_SERIAL_CLR_RTS, IOCTL_SERIAL_SET_DTR, IOCTL_SERIAL_CLR_DTR,
                                  IOCTL_SERIAL_RESET_DEVICE, IOCTL_SERIAL_SET_BREAK_ON, IOCTL_SERIAL_SET_BREAK_OFF };
    if (!f || f >= sizeof(code) / sizeof(code[0])) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return ser(h, code[f], 0, 0, 0, 0, 0);
}

K32 BOOL WINAPI GetCommModemStatus(HANDLE h, LPDWORD s)
{
    if (!s) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DWORD msr = 0;
    if (!ser(h, IOCTL_SERIAL_GET_MODEMSTATUS, 0, 0, &msr, sizeof(msr), 0)) return FALSE;
    *s = msr & (MS_CTS_ON | MS_DSR_ON | MS_RING_ON | MS_RLSD_ON);   /* (the UART's modem status register) */
    return TRUE;
}

/* PURGE_* are SERIAL_PURGE_* */
K32 BOOL WINAPI PurgeComm(HANDLE h, DWORD f) { return ser(h, IOCTL_SERIAL_PURGE, &f, sizeof(f), 0, 0, 0); }

K32 BOOL WINAPI TransmitCommChar(HANDLE h, char c)
{
    UCHAR u = (UCHAR)c;
    return ser(h, IOCTL_SERIAL_IMMEDIATE_CHAR, &u, sizeof(u), 0, 0, 0);
}

/* COMMPROP is SERIAL_COMMPROP */
K32 BOOL WINAPI GetCommProperties(HANDLE h, LPCOMMPROP p)
{
    if (!p) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return ser(h, IOCTL_SERIAL_GET_PROPERTIES, 0, 0, p, sizeof(*p), 0);
}

/* ---- BuildCommDCB: "115200,n,8,1" or "baud=115200 parity=n data=8 stop=1" ---- */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int same(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

static const char *num(const char *s, DWORD *v)
{
    DWORD n = 0;
    int any = 0;
    for (; *s >= '0' && *s <= '9'; s++, any = 1) n = n * 10 + (DWORD)(*s - '0');
    if (!any) return 0;
    *v = n;
    return s;
}

static int parity(int c)
{
    switch (lower(c)) {
    case 'n': return NOPARITY;
    case 'o': return ODDPARITY;
    case 'e': return EVENPARITY;
    case 'm': return MARKPARITY;
    case 's': return SPACEPARITY;
    }
    return -1;
}

static int stopbits(const char **s)
{
    if ((*s)[0] == '1' && (*s)[1] == '.' && (*s)[2] == '5') { *s += 3; return ONE5STOPBITS; }
    if (**s == '1') { (*s)++; return ONESTOPBIT; }
    if (**s == '2') { (*s)++; return TWOSTOPBITS; }
    return -1;
}

static BOOL build_dcb(const char *s, LPDCB dcb, LPCOMMTIMEOUTS t)
{
    while (*s == ' ') s++;
    if (lower(s[0]) == 'c' && lower(s[1]) == 'o' && lower(s[2]) == 'm' && s[3] >= '1' && s[3] <= '9') {
        s += 4;                                          /* ("COM1:" in front) */
        while (*s >= '0' && *s <= '9') s++;
        if (*s == ':') s++;
        while (*s == ' ') s++;
    }
    DWORD v;
    if (*s >= '0' && *s <= '9') {                        /* old form: baud[,parity[,data[,stop]]] */
        if (!(s = num(s, &v))) return FALSE;
        dcb->BaudRate = v == 11 ? 110 : v == 15 ? 150 : v == 30 ? 300 : v == 60 ? 600 : v == 12 ? 1200
                      : v == 24 ? 2400 : v == 48 ? 4800 : v == 96 ? 9600 : v == 19 ? 19200 : v;
        if (*s == ',') {
            s++;
            int p = parity(*s);
            if (p >= 0) { dcb->Parity = (BYTE)p; dcb->fParity = p != NOPARITY; s++; }
        }
        if (*s == ',') { s++; if (num(s, &v)) { dcb->ByteSize = (BYTE)v; s = num(s, &v); } }
        if (*s == ',') { s++; int b = stopbits(&s); if (b >= 0) dcb->StopBits = (BYTE)b; }
        if (*s == ',') { s++; if (lower(*s) == 'x') { dcb->fOutX = dcb->fInX = 1; } else if (lower(*s) == 'p') {
            dcb->fOutxCtsFlow = dcb->fOutxDsrFlow = 1; dcb->fDtrControl = DTR_CONTROL_HANDSHAKE;
            dcb->fRtsControl = RTS_CONTROL_HANDSHAKE; } s++; }
        while (*s == ' ') s++;
        return !*s;
    }
    while (*s) {                                         /* new form: key=value ... */
        char key[8];
        int k = 0;
        while (*s && *s != '=' && *s != ' ' && k < 7) key[k++] = (char)lower(*s++);
        key[k] = 0;
        if (*s++ != '=') return FALSE;
        const char *val = s;
        BOOL on = lower(val[0]) == 'o' && lower(val[1]) == 'n';
        if (same(key, "baud")) { if (!(s = num(s, &v))) return FALSE; dcb->BaudRate = v; }
        else if (same(key, "parity")) { int p = parity(*s++); if (p < 0) return FALSE; dcb->Parity = (BYTE)p; dcb->fParity = p != NOPARITY; }
        else if (same(key, "data")) { if (!(s = num(s, &v))) return FALSE; dcb->ByteSize = (BYTE)v; }
        else if (same(key, "stop")) { int b = stopbits(&s); if (b < 0) return FALSE; dcb->StopBits = (BYTE)b; }
        else if (same(key, "to")) { if (t) { t->ReadTotalTimeoutConstant = on ? 60000 : 0; t->WriteTotalTimeoutConstant = on ? 60000 : 0; } }
        else if (same(key, "xon")) dcb->fOutX = dcb->fInX = on;
        else if (same(key, "odsr")) dcb->fOutxDsrFlow = on;
        else if (same(key, "octs")) dcb->fOutxCtsFlow = on;
        else if (same(key, "dtr")) dcb->fDtrControl = on ? DTR_CONTROL_ENABLE : lower(val[0]) == 'h' ? DTR_CONTROL_HANDSHAKE : DTR_CONTROL_DISABLE;
        else if (same(key, "rts")) dcb->fRtsControl = on ? RTS_CONTROL_ENABLE : lower(val[0]) == 'h' ? RTS_CONTROL_HANDSHAKE
                                                       : lower(val[0]) == 't' ? RTS_CONTROL_TOGGLE : RTS_CONTROL_DISABLE;
        else if (same(key, "idsr")) dcb->fDsrSensitivity = on;
        else return FALSE;
        while (*s && *s != ' ') s++;
        while (*s == ' ') s++;
    }
    return TRUE;
}

K32 BOOL WINAPI BuildCommDCBAndTimeoutsA(LPCSTR def, LPDCB dcb, LPCOMMTIMEOUTS t)
{
    if (!def || !dcb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    DCB d = *dcb;
    d.DCBlength = sizeof(d);
    if (!build_dcb(def, &d, t)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *dcb = d;
    return TRUE;
}

K32 BOOL WINAPI BuildCommDCBA(LPCSTR def, LPDCB dcb) { return BuildCommDCBAndTimeoutsA(def, dcb, NULL); }

K32 BOOL WINAPI BuildCommDCBAndTimeoutsW(LPCWSTR def, LPDCB dcb, LPCOMMTIMEOUTS t)
{
    char a[128];
    if (!wide_to_temp(def, a, sizeof(a))) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return BuildCommDCBAndTimeoutsA(a, dcb, t);
}

K32 BOOL WINAPI BuildCommDCBW(LPCWSTR def, LPDCB dcb) { return BuildCommDCBAndTimeoutsW(def, dcb, NULL); }
