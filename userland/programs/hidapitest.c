/*
 * hidapitest.exe — hidapi's Windows backend, built unchanged from
 * third_party/hidapi, on the devices self-test boot "hid"'s vendor-defined
 * USB HID device (tools/padpeer.py --kind vendor, 1209:0008: product
 * "NovaOS Test HID Device", manufacturer "NovaOS", serial "NOVA0002"; one
 * collection, usage page FF00 usage 1, input report 1, output report 2 and
 * feature report 3 of 8 bytes; output reports come back as input report 1)
 *
 * hidapi finds a device's bus the way it does on Windows: the HID
 * interface's DEVPKEY_Device_InstanceId (CM_Get_Device_Interface_PropertyW)
 * names the collection's device node, and the compatible IDs of that
 * node's parent (CM_Get_Parent, CM_Get_DevNode_PropertyW) say USB,
 * Bluetooth, Bluetooth LE, I2C or SPI; for USB it then reads the USB
 * device's hardware IDs (release and interface numbers) and instance ID.
 * The test checks:
 *  - cfgmgr32's device interface properties directly: the instance ID
 *    (sized with CR_BUFFER_SMALL first, as hidapi asks), the interface's
 *    class GUID and enabled state, CR_NO_SUCH_DEVICE_INTERFACE for a path
 *    no device has; the collection's parent is the USB device
 *    USB\VID_1209&PID_0008\NOVA0002 (its serial number is its instance ID,
 *    as on Windows), DEVPKEY_Device_BusTypeGuid (HID for the collection,
 *    USB for the device) and a ContainerId the two share;
 *  - hid_enumerate: the device once, as HID_API_BUS_USB, with its IDs,
 *    usage, interface number, release number, manufacturer, product and
 *    serial number, and every HID device on the system with a bus;
 *  - hid_open (by IDs and serial number) and hid_get_device_info, the
 *    string getters, a feature report set and read back, an output report
 *    written and its echo read with hid_read_timeout, a timed-out read.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <devpropdef.h>
#include <hidapi.h>

typedef DWORD CONFIGRET;
typedef DWORD DEVINST;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW(LPCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                                         PBYTE buf, PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_PropertyW(DEVINST dn, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buf,
                                                                PULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(DEVINST *dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Parent(DEVINST *parent, DEVINST dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_IDW(DEVINST dn, WCHAR *buf, ULONG len, ULONG flags);
#define CR_SUCCESS                  0x00
#define CR_BUFFER_SMALL             0x1A
#define CR_NO_SUCH_VALUE            0x25
#define CR_NO_SUCH_DEVICE_INTERFACE 0x37

DEFINE_DEVPROPKEY(DEVPKEY_Device_InstanceId, 0x78c34fc8, 0x104a, 0x4aca, 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57, 256);
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusTypeGuid, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 21);
DEFINE_DEVPROPKEY(DEVPKEY_Device_CompatibleIds, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 4);
DEFINE_DEVPROPKEY(DEVPKEY_Device_ContainerId, 0x8c7ed206, 0x3f8a, 0x4827, 0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c, 2);
DEFINE_DEVPROPKEY(DEVPKEY_DeviceInterface_Enabled, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 3);
DEFINE_DEVPROPKEY(DEVPKEY_DeviceInterface_ClassGuid, 0x026e516e, 0xb814, 0x414b, 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22, 4);
static const GUID k_hid_iface = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
static const GUID k_bus_usb = { 0x9d7debbc, 0xc85d, 0x11d1, { 0x9e, 0xb4, 0x00, 0x60, 0x08, 0xc3, 0xa1, 0x9a } };
static const GUID k_bus_hid = { 0xeeaf37d0, 0x1963, 0x47c4, { 0xaa, 0x48, 0x72, 0x47, 0x6d, 0xb7, 0xcf, 0x49 } };

#define VID 0x1209
#define PID 0x0008
#define REPORT_LEN 9

static int g_pass, g_fail;
static const char *g_name = "hidapitest";

static void check(int ok, const char *what, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, what);
    vsnprintf(buf, sizeof(buf), what, ap);
    va_end(ap);
    printf("%s %s\n", ok ? "ok  " : "FAIL", buf);
    fflush(stdout);
    if (ok) g_pass++; else g_fail++;
}

static const char *bus_name(hid_bus_type b)
{
    switch (b) {
    case HID_API_BUS_USB: return "USB";
    case HID_API_BUS_BLUETOOTH: return "Bluetooth";
    case HID_API_BUS_I2C: return "I2C";
    case HID_API_BUS_SPI: return "SPI";
    default: return "unknown";
    }
}

/* A GUID property of node @dn, or FALSE */
static BOOL node_guid(DEVINST dn, const DEVPROPKEY *key, GUID *g)
{
    DEVPROPTYPE t = 0;
    ULONG len = sizeof(*g);
    return !CM_Get_DevNode_PropertyW(dn, key, &t, (PBYTE)g, &len, 0) && t == DEVPROP_TYPE_GUID && len == sizeof(*g);
}

/* cfgmgr32's interface and device properties hidapi reads, for interface @path */
static void property_checks(const wchar_t *path)
{
    DEVPROPTYPE t = 0;
    ULONG len = 0;
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_InstanceId, &t, NULL, &len, 0);
    check(cr == CR_BUFFER_SMALL && t == DEVPROP_TYPE_STRING && len > 2,
          "DEVPKEY_Device_InstanceId sized: CR_BUFFER_SMALL, a string, %lu bytes (cr %lx)", len, cr);
    WCHAR id[200] = { 0 };
    len = sizeof(id);
    cr = CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_InstanceId, &t, (PBYTE)id, &len, 0);
    check(cr == CR_SUCCESS && !_wcsnicmp(id, L"HID\\VID_1209&PID_0008\\", 22) && len == (wcslen(id) + 1) * sizeof(WCHAR),
          "the interface's DEVPKEY_Device_InstanceId is the collection's: %ls", id);
    DEVINST dn = 0, usb = 0;
    WCHAR own[200] = { 0 }, parent[200] = { 0 };
    check(!CM_Locate_DevNodeW(&dn, id, 0) && !CM_Get_Device_IDW(dn, own, 200, 0) && !_wcsicmp(own, id),
          "CM_Locate_DevNodeW finds that node");
    check(!CM_Get_Parent(&usb, dn, 0) && !CM_Get_Device_IDW(usb, parent, 200, 0) &&
          !_wcsicmp(parent, L"USB\\VID_1209&PID_0008\\NOVA0002"),
          "its parent is the USB device, its serial number its instance ID: %ls", parent);
    WCHAR compat[200] = { 0 };
    len = sizeof(compat);
    check(!CM_Get_DevNode_PropertyW(usb, &DEVPKEY_Device_CompatibleIds, &t, (PBYTE)compat, &len, 0) &&
          t == DEVPROP_TYPE_STRING_LIST && !wcscmp(compat, L"USB\\Class_03&SubClass_00&Prot_00"),
          "the USB device's compatible IDs start %ls", compat);
    GUID g, c1, c2;
    check(node_guid(dn, &DEVPKEY_Device_BusTypeGuid, &g) && !memcmp(&g, &k_bus_hid, sizeof(g)),
          "the collection's DEVPKEY_Device_BusTypeGuid is GUID_BUS_TYPE_HID");
    check(node_guid(usb, &DEVPKEY_Device_BusTypeGuid, &g) && !memcmp(&g, &k_bus_usb, sizeof(g)),
          "the USB device's is GUID_BUS_TYPE_USB");
    check(node_guid(dn, &DEVPKEY_Device_ContainerId, &c1) && node_guid(usb, &DEVPKEY_Device_ContainerId, &c2) &&
          !memcmp(&c1, &c2, sizeof(c1)) && c1.Data1 != 0,
          "the collection and the USB device share a DEVPKEY_Device_ContainerId (%08lx-...)", c1.Data1);
    len = sizeof(g);
    check(!CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_DeviceInterface_ClassGuid, &t, (PBYTE)&g, &len, 0) &&
          t == DEVPROP_TYPE_GUID && !memcmp(&g, &k_hid_iface, sizeof(g)),
          "DEVPKEY_DeviceInterface_ClassGuid is GUID_DEVINTERFACE_HID");
    BYTE on = 0;
    len = 1;
    check(!CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_DeviceInterface_Enabled, &t, &on, &len, 0) &&
          t == DEVPROP_TYPE_BOOLEAN && on == 0xFF, "DEVPKEY_DeviceInterface_Enabled is true");
    len = sizeof(id);
    cr = CM_Get_Device_Interface_PropertyW(L"\\\\?\\HID#VID_1209&PID_FFFF#1&2&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
                                           &DEVPKEY_Device_InstanceId, &t, (PBYTE)id, &len, 0);
    check(cr == CR_NO_SUCH_DEVICE_INTERFACE, "a path no device has: CR_NO_SUCH_DEVICE_INTERFACE (cr %lx)", cr);
    len = sizeof(g);
    cr = CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_BusTypeGuid, &t, (PBYTE)&g, &len, 0);
    check(cr == CR_NO_SUCH_VALUE, "a device property asked of the interface: CR_NO_SUCH_VALUE (cr %lx)", cr);
}

static void info_checks(const struct hid_device_info *d, const char *how)
{
    check(d->bus_type == HID_API_BUS_USB, "%s: bus %s", how, bus_name(d->bus_type));
    check(d->vendor_id == VID && d->product_id == PID && d->release_number != 0,
          "%s: %04x:%04x release %04x", how, d->vendor_id, d->product_id, d->release_number);
    check(d->usage_page == 0xFF00 && d->usage == 1, "%s: usage %04x:%04x", how, d->usage_page, d->usage);
    check(d->interface_number == 0, "%s: interface %d", how, d->interface_number);
    check(d->product_string && !wcscmp(d->product_string, L"NovaOS Test HID Device"), "%s: product \"%ls\"", how,
          d->product_string ? d->product_string : L"");
    check(d->manufacturer_string && !wcscmp(d->manufacturer_string, L"NovaOS"), "%s: manufacturer \"%ls\"", how,
          d->manufacturer_string ? d->manufacturer_string : L"");
    check(d->serial_number && !wcscmp(d->serial_number, L"NOVA0002"), "%s: serial number \"%ls\"", how,
          d->serial_number ? d->serial_number : L"");
    check(d->path && !_strnicmp(d->path, "\\\\?\\HID#VID_1209&PID_0008#", 26), "%s: path %s", how, d->path ? d->path : "");
}

int main(void)
{
    printf("%s: hidapi %s\n", g_name, hid_version_str());
    check(hid_init() == 0, "hid_init");

    struct hid_device_info *all = hid_enumerate(0, 0), *ours = NULL;
    int n = 0, known = 0;
    for (struct hid_device_info *d = all; d; d = d->next, n++) {
        printf("     %04x:%04x %-9s usage %04x:%04x \"%ls\" %s\n", d->vendor_id, d->product_id, bus_name(d->bus_type),
               d->usage_page, d->usage, d->product_string ? d->product_string : L"", d->path);
        if (d->bus_type != HID_API_BUS_UNKNOWN) known++;
    }
    check(n >= 1 && known == n, "hid_enumerate(0, 0): %d HID devices, %d of them on a known bus", n, known);
    hid_free_enumeration(all);

    all = hid_enumerate(VID, PID);
    n = 0;
    for (struct hid_device_info *d = all; d; d = d->next) { n++; ours = d; }
    check(n == 1, "hid_enumerate(%04x, %04x): %d device(s) (%ls)", VID, PID, n, hid_error(NULL));
    if (!ours) {
        printf("%s: %d passed, %d failed\n", g_name, g_pass, g_fail + 1);
        return 1;
    }
    info_checks(ours, "hid_enumerate");
    wchar_t path[300];
    MultiByteToWideChar(CP_UTF8, 0, ours->path, -1, path, 300);
    property_checks(path);
    hid_free_enumeration(all);

    hid_device *h = hid_open(VID, PID, L"NOVA0002");
    check(h != NULL, "hid_open(%04x, %04x, \"NOVA0002\") (%ls)", VID, PID, h ? L"" : hid_error(NULL));
    if (h) {
        struct hid_device_info *d = hid_get_device_info(h);
        check(d != NULL, "hid_get_device_info");
        if (d) info_checks(d, "hid_get_device_info");
        wchar_t s[128];
        check(!hid_get_product_string(h, s, 128) && !wcscmp(s, L"NovaOS Test HID Device"), "hid_get_product_string \"%ls\"", s);
        check(!hid_get_serial_number_string(h, s, 128) && !wcscmp(s, L"NOVA0002"), "hid_get_serial_number_string \"%ls\"", s);

        unsigned char f[REPORT_LEN] = { 3, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58 }, back[REPORT_LEN] = { 3 };
        check(hid_send_feature_report(h, f, sizeof(f)) == (int)sizeof(f), "hid_send_feature_report (%ls)", hid_error(h));
        int got = hid_get_feature_report(h, back, sizeof(back));
        check(got == (int)sizeof(back) && !memcmp(f, back, sizeof(f)), "hid_get_feature_report reads it back (%d bytes)", got);

        unsigned char r[REPORT_LEN] = { 0 };
        got = hid_read_timeout(h, r, sizeof(r), 200);
        check(got == 0, "hid_read_timeout with no report: 0 after 200 ms (%d)", got);
        unsigned char out[REPORT_LEN] = { 2, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68 };
        check(hid_write(h, out, sizeof(out)) == (int)sizeof(out), "hid_write output report 2 (%ls)", hid_error(h));
        got = hid_read_timeout(h, r, sizeof(r), 5000);
        check(got == (int)sizeof(r) && r[0] == 1 && !memcmp(r + 1, out + 1, REPORT_LEN - 1),
              "hid_read_timeout: the echo as input report 1 (%d bytes)", got);
        hid_close(h);
    }
    check(hid_exit() == 0, "hid_exit");
    printf("%s: %d passed, %d failed\n", g_name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
