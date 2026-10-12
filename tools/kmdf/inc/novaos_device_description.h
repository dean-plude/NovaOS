/*
 * novaos_device_description.h — DEVICE_DESCRIPTION as of Windows 8
 *
 * build_kmdf.py puts this in place of mingw-w64's (Windows 7) definition in
 * its copy of wdm.h; version 3 adds the address width and the system DMA
 * controller fields.
 */
#define DEVICE_DESCRIPTION_VERSION3 0x0003

typedef struct _DEVICE_DESCRIPTION {
  ULONG Version;
  BOOLEAN Master;
  BOOLEAN ScatterGather;
  BOOLEAN DemandMode;
  BOOLEAN AutoInitialize;
  BOOLEAN Dma32BitAddresses;
  BOOLEAN IgnoreCount;
  BOOLEAN Reserved1;
  BOOLEAN Dma64BitAddresses;
  ULONG BusNumber;
  ULONG DmaChannel;
  INTERFACE_TYPE InterfaceType;
  DMA_WIDTH DmaWidth;
  DMA_SPEED DmaSpeed;
  ULONG MaximumLength;
  ULONG DmaPort;
  ULONG DmaAddressWidth;
  ULONG DmaControllerInstance;
  ULONG DmaRequestLine;
  PHYSICAL_ADDRESS DeviceAddress;
} DEVICE_DESCRIPTION, *PDEVICE_DESCRIPTION;
