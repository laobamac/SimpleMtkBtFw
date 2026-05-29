//
//  BtMtk.cpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#include "BtMtk.h"
#include "Log.h"
#include "FwData.h"

#define super OSObject
OSDefineMetaClassAndStructors(BtMtk, OSObject)

bool BtMtk::initWithDevice(IOService *client, IOUSBHostDevice *dev)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (!super::init()) {
        return false;
    }

    m_pUSBDeviceController = new USBDeviceController();
    if (!m_pUSBDeviceController || !m_pUSBDeviceController->init(client, dev)) {
        return false;
    }
    if (!m_pUSBDeviceController->initConfiguration()) {
        return false;
    }
    if (!m_pUSBDeviceController->findInterface()) {
        return false;
    }
    if (!m_pUSBDeviceController->findPipes()) {
        return false;
    }
    return true;
}

void BtMtk::free()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    OSSafeReleaseNULL(m_pUSBDeviceController);
    super::free();
}

bool BtMtk::setup()
{
    return setupUsb();
}

bool BtMtk::shutdown()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    return enableBluetoothProtocol(false);
}

bool BtMtk::getFirmwareName(char *fwname, size_t len)
{
    if (!fwname || len == 0) {
        return false;
    }
    strncpy(fwname, m_loadedFirmwareName, len - 1);
    fwname[len - 1] = '\0';
    return true;
}

bool BtMtk::setupUsb()
{
    u32 devId = 0;
    u32 fwVersion = 0;
    u32 fwFlavor = 0;
    char fwname[64] {};

    if (!readDeviceId(0x80000008, &devId)) {
        XYLog("Failed to get initial device id\n");
        return false;
    }

    if (!devId || devId != 0x7663) {
        if (!readDeviceId(MTK_BT_READ_DEV_ID, &devId)) {
            XYLog("Failed to get device id\n");
            return false;
        }
        if (!readDeviceId(0x80021004, &fwVersion)) {
            XYLog("Failed to get firmware version\n");
            return false;
        }
        if (!readDeviceId(0x70010020, &fwFlavor)) {
            XYLog("Failed to get firmware flavor\n");
            return false;
        }
        fwFlavor = (fwFlavor & 0x00000080) >> 7;
    }

    m_devId = devId;
    m_fwVersion = fwVersion;
    m_fwFlavor = fwFlavor;
    XYLog("MediaTek dev_id=0x%04x fw_version=0x%08x fw_flavor=%u\n", devId, fwVersion, fwFlavor);

    switch (devId) {
        case 0x7922:
        case 0x7961:
            if (!buildFirmwareName(fwname, sizeof(fwname), devId, fwVersion, fwFlavor)) {
                return false;
            }
            if (!setupFirmware79xx(fwname, devId)) {
                XYLog("Failed to set up firmware: %s\n", fwname);
                subsystemReset(devId);
                return false;
            }
            if (!uhwRegWrite(MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT)) {
                return false;
            }
            if (!enableBluetoothProtocol(true)) {
                XYLog("Failed to enable Bluetooth protocol\n");
                return false;
            }
            strncpy(m_loadedFirmwareName, fwname, sizeof(m_loadedFirmwareName) - 1);
            m_loadedFirmwareName[sizeof(m_loadedFirmwareName) - 1] = '\0';
            XYLog("MediaTek device setup complete\n");
            return true;
        default:
            XYLog("Unsupported MediaTek hardware variant: 0x%08x\n", devId);
            return false;
    }
}

bool BtMtk::buildFirmwareName(char *fwname, size_t len, u32 devId, u32 fwVersion, u32 fwFlavor)
{
    if (!fwname || len == 0) {
        return false;
    }

    if (devId == 0x7961 && fwFlavor) {
        snprintf(fwname, len, "BT_RAM_CODE_MT%04x_1a_%x_hdr.bin",
                 devId & 0xffff, (fwVersion & 0xff) + 1);
    } else {
        snprintf(fwname, len, "BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
                 devId & 0xffff, (fwVersion & 0xff) + 1);
    }
    XYLog("MediaTek firmware file: %s\n", fwname);
    return true;
}

bool BtMtk::setupFirmware79xx(const char *fwname, u32 devId)
{
    OSData *fwData = requestFirmwareData(fwname);
    if (!fwData) {
        XYLog("Failed to load firmware file: %s\n", fwname);
        return false;
    }

    bool ret = true;
    const u8 *fwBase = (const u8 *)fwData->getBytesNoCopy();
    const u8 *fwPtr = fwBase;
    size_t fwSize = fwData->getLength();

    if (fwSize < MTK_FW_ROM_PATCH_HEADER_SIZE + MTK_FW_ROM_PATCH_GD_SIZE) {
        XYLog("Firmware too small: %zu\n", fwSize);
        OSSafeReleaseNULL(fwData);
        return false;
    }

    btmtk_patch_header *hdr = (btmtk_patch_header *)fwBase;
    btmtk_global_desc *globalDesc = (btmtk_global_desc *)(fwBase + MTK_FW_ROM_PATCH_HEADER_SIZE);
    u32 sectionNum = get_unaligned_le32(&globalDesc->section_num);

    XYLog("HW/SW Version: 0x%04x%04x, Build Time: %.16s, sections=%u\n",
          get_unaligned_le16(&hdr->hwver), get_unaligned_le16(&hdr->swver),
          hdr->datetime, sectionNum);

    for (u32 i = 0; i < sectionNum; i++) {
        size_t sectionMapOffset = MTK_FW_ROM_PATCH_HEADER_SIZE + MTK_FW_ROM_PATCH_GD_SIZE +
                                  MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i;
        if (sectionMapOffset + sizeof(btmtk_section_map) > fwSize) {
            XYLog("Invalid section map offset: %zu\n", sectionMapOffset);
            ret = false;
            break;
        }

        btmtk_section_map *sectionMap = (btmtk_section_map *)(fwBase + sectionMapOffset);
        u32 sectionOffset = get_unaligned_le32(&sectionMap->secoffset);
        u32 dlSize = get_unaligned_le32(&sectionMap->bin_info_spec.dlsize);

        if (sectionMapOffset + MTK_SEC_MAP_COMMON_SIZE + MTK_SEC_MAP_NEED_SEND_SIZE + 1 > fwSize) {
            XYLog("Invalid section map payload offset: %zu\n", sectionMapOffset);
            ret = false;
            break;
        }
        if (dlSize == 0) {
            continue;
        }
        if ((size_t)sectionOffset + dlSize > fwSize) {
            XYLog("Invalid section data offset=0x%x size=0x%x fwSize=%zu\n",
                  sectionOffset, dlSize, fwSize);
            ret = false;
            break;
        }

        u8 firstBlock = 1;
        u8 retry = 20;
        u32 status = BTMTK_WMT_INVALID;
        u8 cmd[64] {};

        while (retry > 0) {
            cmd[0] = 0;
            memcpy(cmd + 1,
                   fwBase + MTK_FW_ROM_PATCH_HEADER_SIZE + MTK_FW_ROM_PATCH_GD_SIZE +
                   MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i + MTK_SEC_MAP_COMMON_SIZE,
                   MTK_SEC_MAP_NEED_SEND_SIZE + 1);

            btmtk_hci_wmt_params params {};
            params.op = BTMTK_WMT_PATCH_DWNLD;
            params.flag = 0;
            params.dlen = MTK_SEC_MAP_NEED_SEND_SIZE + 1;
            params.data = cmd;
            params.status = &status;

            if (!hciWmtSync(&params)) {
                XYLog("Failed to send WMT patch metadata for section %u\n", i);
                ret = false;
                goto done;
            }

            if (status == BTMTK_WMT_PATCH_UNDONE) {
                break;
            }
            if (status == BTMTK_WMT_PATCH_PROGRESS) {
                IOSleep(100);
                retry--;
                continue;
            }
            if (status == BTMTK_WMT_PATCH_DONE) {
                goto next_section;
            }

            XYLog("Unexpected WMT patch status %u for section %u\n", status, i);
            ret = false;
            goto done;
        }

        if (retry == 0) {
            XYLog("WMT patch metadata retry exhausted for section %u\n", i);
            ret = false;
            goto done;
        }

        fwPtr = fwBase + sectionOffset;
        while (dlSize > 0) {
            u16 dlen = (dlSize > 250) ? 250 : (u16)dlSize;
            u8 flag;

            if (firstBlock == 1) {
                flag = 1;
                firstBlock = 0;
            } else if (dlSize - dlen == 0) {
                flag = 3;
            } else {
                flag = 2;
            }

            btmtk_hci_wmt_params params {};
            params.op = BTMTK_WMT_PATCH_DWNLD;
            params.flag = flag;
            params.dlen = dlen;
            params.data = fwPtr;
            params.status = &status;

            if (!hciWmtSync(&params)) {
                XYLog("Failed to send WMT patch data for section %u\n", i);
                ret = false;
                goto done;
            }
            if (status == BTMTK_WMT_PATCH_PROGRESS) {
                XYLog("Unexpected progress status during section payload download\n");
                ret = false;
                goto done;
            }

            dlSize -= dlen;
            fwPtr += dlen;
        }

    next_section:
        continue;
    }

    IOSleep(120);

done:
    OSSafeReleaseNULL(fwData);
    return ret;
}

bool BtMtk::hciWmtSync(btmtk_hci_wmt_params *params)
{
    if (!params) {
        return false;
    }

    u32 status = BTMTK_WMT_INVALID;
    u16 hlen = (u16)(sizeof(btmtk_wmt_hdr) + params->dlen);
    if (hlen > 255 || hlen > (BTMTK_CMD_BUF_SIZE - HCI_COMMAND_HDR_SIZE)) {
        return false;
    }
    u8 cmdBuf[BTMTK_CMD_BUF_SIZE] {};
    u8 evtBuf[BTMTK_WMT_EVENT_SIZE] {};
    u32 evtLen = 0;

    HciCommandHdr *hci = (HciCommandHdr *)cmdBuf;
    btmtk_wmt_hdr *hdr = (btmtk_wmt_hdr *)hci->data;

    hci->opcode = OSSwapHostToLittleInt16(BTMTK_HCI_WMT_OPCODE);
    hci->len = hlen;
    hdr->dir = 1;
    hdr->op = params->op;
    hdr->dlen = OSSwapHostToLittleInt16(params->dlen + 1);
    hdr->flag = params->flag;
    if (params->dlen && params->data) {
        memcpy(hci->data + sizeof(*hdr), params->data, params->dlen);
    }

    IOReturn ret = m_pUSBDeviceController->sendHCIRequest(hci, HCI_INIT_TIMEOUT);
    if (ret != kIOReturnSuccess) {
        XYLog("Failed to send WMT HCI command: %s %d\n",
              m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }

    ret = m_pUSBDeviceController->readWMTEvent(evtBuf, sizeof(evtBuf), &evtLen, HCI_INIT_TIMEOUT);
    if (ret != kIOReturnSuccess) {
        XYLog("Failed to read WMT event: %s %d\n",
              m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    if (evtLen < sizeof(btmtk_hci_wmt_evt)) {
        XYLog("WMT event too short: %u\n", evtLen);
        return false;
    }

    btmtk_hci_wmt_evt *evt = (btmtk_hci_wmt_evt *)evtBuf;
    if (evt->hhdr.evt != HCI_EV_WMT) {
        XYLog("Unexpected WMT event id: 0x%02x\n", evt->hhdr.evt);
        return false;
    }
    if (evt->whdr.op != hdr->op) {
        XYLog("Wrong WMT op received %u expected %u\n", evt->whdr.op, hdr->op);
        return false;
    }

    switch (evt->whdr.op) {
        case BTMTK_WMT_SEMAPHORE:
            if (evt->whdr.flag == 2) {
                status = BTMTK_WMT_PATCH_UNDONE;
            } else {
                status = BTMTK_WMT_PATCH_DONE;
            }
            break;
        case BTMTK_WMT_FUNC_CTRL:
            if (evtLen < sizeof(btmtk_hci_wmt_evt_funcc)) {
                status = BTMTK_WMT_ON_UNDONE;
                break;
            } else {
                btmtk_hci_wmt_evt_funcc *funcc = (btmtk_hci_wmt_evt_funcc *)evtBuf;
                u16 funccStatus = OSSwapBigToHostInt16(funcc->status);
                if (funccStatus == 0x404) {
                    status = BTMTK_WMT_ON_DONE;
                } else if (funccStatus == 0x420) {
                    status = BTMTK_WMT_ON_PROGRESS;
                } else {
                    status = BTMTK_WMT_ON_UNDONE;
                }
            }
            break;
        case BTMTK_WMT_PATCH_DWNLD:
            if (evt->whdr.flag == 2) {
                status = BTMTK_WMT_PATCH_DONE;
            } else if (evt->whdr.flag == 1) {
                status = BTMTK_WMT_PATCH_PROGRESS;
            } else {
                status = BTMTK_WMT_PATCH_UNDONE;
            }
            break;
        default:
            status = BTMTK_WMT_INVALID;
            break;
    }

    if (params->status) {
        *params->status = status;
    }
    return true;
}

bool BtMtk::enableBluetoothProtocol(bool enable)
{
    u8 param = enable ? 1 : 0;
    btmtk_hci_wmt_params wmtParams {};
    wmtParams.op = BTMTK_WMT_FUNC_CTRL;
    wmtParams.flag = 0;
    wmtParams.dlen = sizeof(param);
    wmtParams.data = &param;
    wmtParams.status = nullptr;

    return hciWmtSync(&wmtParams);
}

bool BtMtk::readDeviceId(u32 reg, u32 *value)
{
    return regRead(reg, value);
}

bool BtMtk::regRead(u32 reg, u32 *value)
{
    if (!value) {
        return false;
    }
    IOReturn ret = m_pUSBDeviceController->controlRequest(
        makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeVendor, kRequestRecipientDevice),
        0x63, reg >> 16, reg & 0xffff, value, sizeof(*value), HCI_INIT_TIMEOUT);
    if (ret != kIOReturnSuccess) {
        XYLog("Failed to read reg 0x%08x: %s %d\n",
              reg, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    *value = OSSwapLittleToHostInt32(*value);
    XYLog("reg=0x%08x value=0x%08x\n", reg, *value);
    return true;
}

bool BtMtk::uhwRegRead(u32 reg, u32 *value)
{
    if (!value) {
        return false;
    }
    IOReturn ret = m_pUSBDeviceController->controlRequest(0xDE, 0x01, reg >> 16, reg & 0xffff,
                                                         value, sizeof(*value), HCI_INIT_TIMEOUT);
    if (ret != kIOReturnSuccess) {
        XYLog("Failed to read uhw reg 0x%08x: %s %d\n",
              reg, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    *value = OSSwapLittleToHostInt32(*value);
    XYLog("uhw reg=0x%08x value=0x%08x\n", reg, *value);
    return true;
}

bool BtMtk::uhwRegWrite(u32 reg, u32 value)
{
    u32 leValue = OSSwapHostToLittleInt32(value);
    IOReturn ret = m_pUSBDeviceController->controlRequest(0x5E, 0x02, reg >> 16, reg & 0xffff,
                                                         &leValue, sizeof(leValue), HCI_INIT_TIMEOUT);
    if (ret != kIOReturnSuccess) {
        XYLog("Failed to write uhw reg 0x%08x: %s %d\n",
              reg, m_pUSBDeviceController->stringFromReturn(ret), ret);
        return false;
    }
    return true;
}

bool BtMtk::resetDone()
{
    u32 value = 0;
    if (!uhwRegRead(MTK_BT_MISC, &value)) {
        return false;
    }
    return (value & MTK_BT_RST_DONE) != 0;
}

bool BtMtk::subsystemReset(u32 devId)
{
    u32 value = 0;

    if (devId == 0x7922) {
        if (!uhwRegRead(MTK_BT_SUBSYS_RST, &value)) {
            return false;
        }
        value |= 0x00002020;
        if (!uhwRegWrite(MTK_BT_SUBSYS_RST, value)) {
            return false;
        }
        if (!uhwRegWrite(MTK_EP_RST_OPT, 0x00010001)) {
            return false;
        }
        if (!uhwRegRead(MTK_BT_SUBSYS_RST, &value)) {
            return false;
        }
        value |= BIT(0);
        if (!uhwRegWrite(MTK_BT_SUBSYS_RST, value)) {
            return false;
        }
        IOSleep(100);
    } else {
        if (!uhwRegWrite(MTK_EP_RST_OPT, MTK_EP_RST_IN_OUT_OPT)) {
            return false;
        }
        if (!uhwRegRead(MTK_BT_WDT_STATUS, &value)) {
            return false;
        }
        if (!uhwRegWrite(MTK_BT_SUBSYS_RST, 1)) {
            return false;
        }
        if (!uhwRegWrite(MTK_UDMA_INT_STA_BT, 0x000000FF)) {
            return false;
        }
        if (!uhwRegRead(MTK_UDMA_INT_STA_BT, &value)) {
            return false;
        }
        if (!uhwRegWrite(MTK_UDMA_INT_STA_BT1, 0x000000FF)) {
            return false;
        }
        if (!uhwRegRead(MTK_UDMA_INT_STA_BT1, &value)) {
            return false;
        }
        IOSleep(20);
        if (!uhwRegWrite(MTK_BT_SUBSYS_RST, 0)) {
            return false;
        }
        if (!uhwRegRead(MTK_BT_SUBSYS_RST, &value)) {
            return false;
        }
    }

    for (int i = 0; i < 50; i++) {
        if (resetDone()) {
            break;
        }
        IOSleep(20);
    }

    if (devId == 0x7922 && !uhwRegWrite(MTK_UDMA_INT_STA_BT, 0x000000FF)) {
        return false;
    }

    if (!readDeviceId(MTK_BT_READ_DEV_ID, &value)) {
        XYLog("Can't get device id after subsystem reset\n");
        return false;
    }

    return true;
}

OSData *BtMtk::firmwareConvertion(OSData *originalFirmware)
{
    if (!originalFirmware) {
        return NULL;
    }

    OSData *fwData = OSData::withCapacity((unsigned int)originalFirmware->getLength());
    if (!fwData) {
        return NULL;
    }

    enum { kInflateChunkSize = 4096 };
    unsigned char *fwBytes = (unsigned char *)IOMalloc(kInflateChunkSize);
    if (!fwBytes) {
        OSSafeReleaseNULL(fwData);
        return NULL;
    }

    z_stream stream {};
    stream.next_in = (Bytef *)originalFirmware->getBytesNoCopy();
    stream.avail_in = (uInt)originalFirmware->getLength();
    stream.zalloc = zcalloc;
    stream.zfree = zcfree;

    int err = inflateInit(&stream);
    if (err != Z_OK) {
        IOFree(fwBytes, kInflateChunkSize);
        OSSafeReleaseNULL(fwData);
        return NULL;
    }

    do {
        stream.next_out = fwBytes;
        stream.avail_out = kInflateChunkSize;
        err = inflate(&stream, Z_NO_FLUSH);
        if (err != Z_OK && err != Z_STREAM_END) {
            inflateEnd(&stream);
            IOFree(fwBytes, kInflateChunkSize);
            OSSafeReleaseNULL(fwData);
            return NULL;
        }

        uInt produced = kInflateChunkSize - stream.avail_out;
        if (produced && !fwData->appendBytes(fwBytes, produced)) {
            inflateEnd(&stream);
            IOFree(fwBytes, kInflateChunkSize);
            OSSafeReleaseNULL(fwData);
            return NULL;
        }
    } while (err != Z_STREAM_END);

    inflateEnd(&stream);
    IOFree(fwBytes, kInflateChunkSize);
    return fwData;
}

OSData *BtMtk::requestFirmwareData(const char *fwName, bool noWarn)
{
    OSData *compressedFwData = getFWDescByName(fwName);
    if (!compressedFwData) {
        if (!noWarn) {
            XYLog("Firmware %s not found\n", fwName);
        }
        return NULL;
    }
    XYLog("Found device firmware %s\n", fwName);
    OSData *fwData = firmwareConvertion(compressedFwData);
    OSSafeReleaseNULL(compressedFwData);
    if (!fwData) {
        XYLog("Firmware %s uncompress failed\n", fwName);
        return NULL;
    }
    return fwData;
}
