//
//  BtMtk.h
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#ifndef BtMtk_h
#define BtMtk_h

#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSObject.h>
#include <libkern/libkern.h>

#include "USBDeviceController.hpp"
#include "Hci.h"
#include "linux.h"

#define BTMTK_CMD_BUF_SIZE 272
#define BTMTK_WMT_EVENT_SIZE 64

#define BTMTK_HCI_WMT_OPCODE 0xfc6f
#define HCI_EV_WMT 0xe4

#define FIRMWARE_MT7922 "BT_RAM_CODE_MT7922_1_1_hdr.bin"
#define FIRMWARE_MT7961 "BT_RAM_CODE_MT7961_1_2_hdr.bin"
#define FIRMWARE_MT7961_FLAVOR "BT_RAM_CODE_MT7961_1a_2_hdr.bin"

#define MTK_FW_ROM_PATCH_HEADER_SIZE 32
#define MTK_FW_ROM_PATCH_GD_SIZE 64
#define MTK_FW_ROM_PATCH_SEC_MAP_SIZE 64
#define MTK_SEC_MAP_COMMON_SIZE 12
#define MTK_SEC_MAP_NEED_SEND_SIZE 52

#define BTMTK_WMT_REG_WRITE 0x1
#define BTMTK_WMT_REG_READ 0x2

#define MT7921_DLSTATUS 0x7c053c10
#define BT_DL_STATE BIT(1)

#define MTK_BT_MISC 0x70002510
#define MTK_BT_SUBSYS_RST 0x70002610
#define MTK_UDMA_INT_STA_BT 0x74000024
#define MTK_UDMA_INT_STA_BT1 0x74000308
#define MTK_BT_WDT_STATUS 0x740003A0
#define MTK_EP_RST_OPT 0x74011890
#define MTK_EP_RST_IN_OUT_OPT 0x00010001
#define MTK_BT_RST_DONE 0x00000100
#define MTK_BT_RESET_REG_CONNV3 0x70028610
#define MTK_BT_READ_DEV_ID 0x70010200

enum {
    BTMTK_WMT_PATCH_DWNLD = 0x1,
    BTMTK_WMT_TEST = 0x2,
    BTMTK_WMT_WAKEUP = 0x3,
    BTMTK_WMT_HIF = 0x4,
    BTMTK_WMT_FUNC_CTRL = 0x6,
    BTMTK_WMT_RST = 0x7,
    BTMTK_WMT_REGISTER = 0x8,
    BTMTK_WMT_SEMAPHORE = 0x17,
};

enum {
    BTMTK_WMT_INVALID,
    BTMTK_WMT_PATCH_UNDONE,
    BTMTK_WMT_PATCH_PROGRESS,
    BTMTK_WMT_PATCH_DONE,
    BTMTK_WMT_ON_UNDONE,
    BTMTK_WMT_ON_DONE,
    BTMTK_WMT_ON_PROGRESS,
};

struct btmtk_wmt_hdr {
    u8 dir;
    u8 op;
    __le16 dlen;
    u8 flag;
} __attribute__((packed));

struct btmtk_hci_wmt_evt {
    HciEventHdr hhdr;
    struct btmtk_wmt_hdr whdr;
} __attribute__((packed));

struct btmtk_hci_wmt_evt_funcc {
    struct btmtk_hci_wmt_evt hwhdr;
    __be16 status;
} __attribute__((packed));

struct btmtk_patch_header {
    u8 datetime[16];
    u8 platform[4];
    __le16 hwver;
    __le16 swver;
    __le32 magicnum;
} __attribute__((packed));

struct btmtk_global_desc {
    __le32 patch_ver;
    __le32 sub_sys;
    __le32 feature_opt;
    __le32 section_num;
} __attribute__((packed));

struct btmtk_section_map {
    __le32 sectype;
    __le32 secoffset;
    __le32 secsize;
    union {
        __le32 u4SecSpec[13];
        struct {
            __le32 dlAddr;
            __le32 dlsize;
            __le32 seckeyidx;
            __le32 alignlen;
            __le32 sectype;
            __le32 dlmodecrctype;
            __le32 crc;
            __le32 reserved[6];
        } bin_info_spec;
    };
} __attribute__((packed));

struct btmtk_hci_wmt_params {
    u8 op;
    u8 flag;
    u16 dlen;
    const void *data;
    u32 *status;
};

class BtMtk : public OSObject {
    OSDeclareDefaultStructors(BtMtk)

public:
    virtual bool initWithDevice(IOService *client, IOUSBHostDevice *dev);
    virtual void free() override;

    bool setup();
    bool shutdown();
    bool getFirmwareName(char *fwname, size_t len);

private:
    bool setupUsb();
    bool setupFirmware79xx(const char *fwname, u32 devId);
    bool hciWmtSync(btmtk_hci_wmt_params *params);
    bool buildFirmwareName(char *fwname, size_t len, u32 devId, u32 fwVersion, u32 fwFlavor);

    bool readDeviceId(u32 reg, u32 *value);
    bool regRead(u32 reg, u32 *value);
    bool uhwRegRead(u32 reg, u32 *value);
    bool uhwRegWrite(u32 reg, u32 value);
    bool subsystemReset(u32 devId);
    bool resetDone();
    bool enableBluetoothProtocol(bool enable);

    OSData *firmwareConvertion(OSData *originalFirmware);
    OSData *requestFirmwareData(const char *fwName, bool noWarn = false);

private:
    USBDeviceController *m_pUSBDeviceController {nullptr};
    u32 m_devId {0};
    u32 m_fwVersion {0};
    u32 m_fwFlavor {0};
    char m_loadedFirmwareName[64] {};
};

#endif /* BtMtk_h */
