//
//  SimpleMtkBtPatcher.cpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

//
//  SimpleMtkBtPatcher.cpp
//  SimpleMtkBtPatcher
//
//  Created by zxystd <zxystd@foxmail.com> on 2021/2/8.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

#include "SimpleMtkBtPatcher.hpp"

static CSimpleMtkBtPatcher mtkBtPatcher;
static CSimpleMtkBtPatcher *callbackMtkBtPatcher = nullptr;

static const char *bootargOff[] {
    "-smbtcompatoff"
};

static const char *bootargDebug[] {
    "-smbtcompatdbg"
};

static const char *bootargBeta[] {
    "-smbtcompatbeta"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::MountainLion,
    KernelVersion::Tahoe,
    []() {
        mtkBtPatcher.init();
    }
};

static const char *SimpleMtkBtPatcher_IOBluetoothFamily[] { "/System/Library/Extensions/IOBluetoothFamily.kext/Contents/MacOS/IOBluetoothFamily" };

static KernelPatcher::KextInfo SimpleMtkBtPatcher_IOBluetoothInfo {
    "com.apple.iokit.IOBluetoothFamily",
    SimpleMtkBtPatcher_IOBluetoothFamily,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

static const char *SimpleMtkBtPatcher_IOUSBHostFamily[] {
    "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily" };

static KernelPatcher::KextInfo SimpleMtkBtPatcher_IOUsbHostInfo {
    "com.apple.iokit.IOUSBHostFamily",
    SimpleMtkBtPatcher_IOUSBHostFamily,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

bool CSimpleMtkBtPatcher::_randomAddressInit = false;
bool CSimpleMtkBtPatcher::_randomAddressHack = false;

bool CSimpleMtkBtPatcher::init()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    callbackMtkBtPatcher = this;
    if (getKernelVersion() < KernelVersion::Monterey) {
        lilu.onKextLoadForce(&SimpleMtkBtPatcher_IOBluetoothInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackMtkBtPatcher->processKext(patcher, index, address, size);
        }, this);
    } else {
        lilu.onKextLoadForce(&SimpleMtkBtPatcher_IOUsbHostInfo, 1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackMtkBtPatcher->processKext(patcher, index, address, size);
        }, this);
    }
    return true;
}

void CSimpleMtkBtPatcher::free()
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
}

void CSimpleMtkBtPatcher::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    DBGLOG(DRV_NAME, "%s", __PRETTY_FUNCTION__);
    if (getKernelVersion() < KernelVersion::Monterey) {
        if (SimpleMtkBtPatcher_IOBluetoothInfo.loadIndex == index) {
            DBGLOG(DRV_NAME, "%s", SimpleMtkBtPatcher_IOBluetoothInfo.id);
            
            KernelPatcher::RouteRequest findQueueRequestRequest {
                "__ZN25IOBluetoothHostController17FindQueuedRequestEtP22BluetoothDeviceAddresstbPP21IOBluetoothHCIRequest",
                newFindQueueRequest,
                oldFindQueueRequest
            };
            patcher.routeMultiple(index, &findQueueRequestRequest, 1, address, size);
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                DBGLOG(DRV_NAME, "routed %s", findQueueRequestRequest.symbol);
            } else {
                SYSLOG(DRV_NAME, "failed to resolve %s, error = %d", findQueueRequestRequest.symbol, patcher.getError());
                patcher.clearError();
            }
            
        }
    } else {
        if (SimpleMtkBtPatcher_IOUsbHostInfo.loadIndex == index) {
            SYSLOG(DRV_NAME, "%s", SimpleMtkBtPatcher_IOUsbHostInfo.id);

            KernelPatcher::RouteRequest pipeIoRequest {
                "__ZN13IOUSBHostPipe2ioEP18IOMemoryDescriptorjP19IOUSBHostCompletionj",
                newPipeIo,
                oldPipeIo
            };
            patcher.routeMultiple(index, &pipeIoRequest, 1, address, size);
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                SYSLOG(DRV_NAME, "routed %s", pipeIoRequest.symbol);
            } else {
                SYSLOG(DRV_NAME, "failed to resolve %s, error = %d", pipeIoRequest.symbol, patcher.getError());
                patcher.clearError();
            }

            /*
             * deviceRequest carries HCI commands (EP0). It is hooked for the
             * Read Clock fix; the LE random address workaround (written for
             * Intel controllers) stays opt-in via -smbtrandaddr.
             */
            _randomAddressHack = checkKernelArgument("-smbtrandaddr");

            KernelPatcher::RouteRequest hostDeviceRequest {
            "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
                newHostDeviceRequest,
                oldHostDeviceRequest
            };
            patcher.routeMultiple(index, &hostDeviceRequest, 1, address, size);
            if (patcher.getError() == KernelPatcher::Error::NoError) {
                SYSLOG(DRV_NAME, "routed %s", hostDeviceRequest.symbol);
            } else {
                SYSLOG(DRV_NAME, "failed to resolve %s, error = %d", hostDeviceRequest.symbol, patcher.getError());
                patcher.clearError();
            }
        }
    }
}

#pragma mark - For Bigsur-, patch unhandled 0x2019 opcode

#define HCI_OP_LE_START_ENCRYPTION 0x2019

IOReturn CSimpleMtkBtPatcher::newFindQueueRequest(void *that, unsigned short arg1, void *addr, unsigned short arg2, bool arg3, void **hciRequestPtr)
{
    IOReturn ret = FunctionCast(newFindQueueRequest, callbackMtkBtPatcher->oldFindQueueRequest)(that, arg1, addr, arg2, arg3, hciRequestPtr);
    if (ret != 0 && arg1 == HCI_OP_LE_START_ENCRYPTION) {
        ret = FunctionCast(newFindQueueRequest, callbackMtkBtPatcher->oldFindQueueRequest)(that, arg1, addr, 0xffff, arg3, hciRequestPtr);
        DBGLOG(DRV_NAME, "%s ret: %d arg1: 0x%04x arg2: 0x%04x arg3: %d ptr: %p", __FUNCTION__, ret, arg1, arg2, arg3, *hciRequestPtr);
    }
    return ret;
}

#pragma mark - For Monterey+ patch for intercept HCI REQ and RESP

StandardUSB::DeviceRequest randomAddressRequest;
// Hardcoded Random Address HCI Command
const uint8_t randomAddressHci[9] = {0x05, 0x20, 0x06, 0x94, 0x50, 0x64, 0xD0, 0x78, 0x6B}; 
IOBufferMemoryDescriptor *writeHCIDescriptor = nullptr;

#define MAX_HCI_BUF_LEN             255
#define HCI_OP_RESET                0x0c03
#define HCI_OP_LE_SET_SCAN_PARAM    0x200B
#define HCI_OP_LE_SET_SCAN_ENABLE   0x200C

IOReturn CSimpleMtkBtPatcher::newHostDeviceRequest(void *that, IOService *provider, StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor, unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout)
{
    HciCommandHdr *hdr = nullptr;
    uint32_t hdrLen = 0;
    char hciBuf[MAX_HCI_BUF_LEN] = {0};

    fixReadClock(request, data, descriptor);
    
    if (data == nullptr) {
        if (descriptor != nullptr &&
            (getKernelVersion() < KernelVersion::Sequoia || !descriptor->prepare(kIODirectionOut))) {
            if (descriptor->getLength() > 0) {
                descriptor->readBytes(0, hciBuf, min(descriptor->getLength(), MAX_HCI_BUF_LEN));
                hdrLen = (uint32_t)min(descriptor->getLength(), MAX_HCI_BUF_LEN);
            }
            if (getKernelVersion() >= KernelVersion::Sequoia)
                descriptor->complete(kIODirectionOut);
        }
        hdr = (HciCommandHdr *)hciBuf;
        if (_randomAddressHack && hdr->opcode == HCI_OP_LE_SET_SCAN_PARAM) {
            if (!_randomAddressInit) {
                randomAddressRequest.bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeClass, kRequestRecipientInterface);
                randomAddressRequest.bRequest = 0xE0;
                randomAddressRequest.wIndex = 0;
                randomAddressRequest.wValue = 0;
                randomAddressRequest.wLength = 9;
                length = 9;
                if (writeHCIDescriptor == nullptr)
                    writeHCIDescriptor = IOBufferMemoryDescriptor::withBytes(randomAddressHci, 9, kIODirectionOut);
                writeHCIDescriptor->prepare(kIODirectionOut);
                IOReturn ret = FunctionCast(newHostDeviceRequest, callbackMtkBtPatcher->oldHostDeviceRequest)(that, provider, randomAddressRequest, nullptr, writeHCIDescriptor, length, nullptr, timeout);
                writeHCIDescriptor->complete();
                const char *randAddressDump = _hexDumpHCIData((uint8_t *)randomAddressHci, 9);
                if (randAddressDump) {
                    SYSLOG(DRV_NAME, "[PATCH] Sending Random Address HCI %d %s", ret, randAddressDump);
                    IOFree((void *)randAddressDump, 9 * 3 + 1);
                }
                _randomAddressInit = true;
                SYSLOG(DRV_NAME, "[PATCH] Resend LE SCAN PARAM HCI %d", ret);
            }
        }
    } else {
        hdr = (HciCommandHdr *)data;
        hdrLen = request.wLength - 3;
    }
    if (hdr) {
        // HCI reset, we need to send Random address again
        if (hdr->opcode == HCI_OP_RESET)
            _randomAddressInit = false;
#if DEBUG
        DBGLOG(DRV_NAME, "[%s] bRequest: 0x%x direction: %s type: %s recipient: %s wValue: 0x%02x wIndex: 0x%02x opcode: 0x%04x len: %d length: %d async: %d", provider->getName(), request.bRequest, requestDirectionNames[(request.bmRequestType & kDeviceRequestDirectionMask) >> kDeviceRequestDirectionPhase], requestRecipientNames[(request.bmRequestType & kDeviceRequestRecipientMask) >> kDeviceRequestRecipientPhase], requestTypeNames[(request.bmRequestType & kDeviceRequestTypeMask) >> kDeviceRequestTypePhase], request.wValue, request.wIndex, hdr->opcode, hdr->len, request.wLength, completion != nullptr);
        if (hdrLen) {
            const char *dump = _hexDumpHCIData((uint8_t *)hdr, hdrLen);
            if (dump) {
                DBGLOG(DRV_NAME, "[Request]: %s", dump);
                IOFree((void *)dump, hdrLen * 3 + 1);
            }
        }
#endif
    }
    return FunctionCast(newHostDeviceRequest, callbackMtkBtPatcher->oldHostDeviceRequest)(that, provider, request, data, descriptor, length, completion, timeout);
}

#pragma mark - HCI Read Clock for MediaTek

/*
 * bluetoothd reads the BT clock with HCI_Read_Clock(handle, Which_Clock=0).
 * For the local clock MediaTek returns Connection_Handle 0 instead of the
 * handle that was passed, so bluetoothd cannot match the result to the
 * connection ("Failed to read BT clock"). AAC-ELD streaming to AirPods
 * timestamps audio with that clock, and the headphones never start playing.
 * Ask for the piconet clock of the connection instead (Which_Clock=1): the
 * controller then returns the handle, and as central the piconet clock is
 * our own native clock.
 */
#define HCI_OP_READ_CLOCK 0x1407
static uint32_t readClockRewritten, aclPbRewritten;

static void publishCounters()
{
    IOService *res = IOService::getResourceService();
    if (!res)
        return;
    OSDictionary *d = OSDictionary::withCapacity(2);
    if (!d)
        return;
    OSNumber *n1 = OSNumber::withNumber(readClockRewritten, 32);
    OSNumber *n2 = OSNumber::withNumber(aclPbRewritten, 32);
    if (n1) { d->setObject("ReadClockRewritten", n1); n1->release(); }
    if (n2) { d->setObject("AclPbRewritten", n2); n2->release(); }
    res->setProperty("SimpleMtkBtPatcher", d);
    d->release();
}

void CSimpleMtkBtPatcher::fixReadClock(StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor)
{
    /* HCI command: class request to the interface, host to device */
    if ((request.bmRequestType & 0x80) != 0 || request.wLength != 6)
        return;
    uint8_t cmd[6];
    if (data != nullptr) {
        memcpy(cmd, data, sizeof(cmd));
    } else if (descriptor != nullptr && descriptor->prepare(kIODirectionInOut) == kIOReturnSuccess) {
        bool ok = descriptor->readBytes(0, cmd, sizeof(cmd)) == sizeof(cmd);
        if (ok && (cmd[0] | (cmd[1] << 8)) == HCI_OP_READ_CLOCK && cmd[2] == 3 && cmd[5] == 0) {
            cmd[5] = 1;
            if (descriptor->writeBytes(5, &cmd[5], 1) == 1) {
                readClockRewritten++;
                publishCounters();
            }
        }
        descriptor->complete(kIODirectionInOut);
        return;
    } else {
        return;
    }
    if ((cmd[0] | (cmd[1] << 8)) == HCI_OP_READ_CLOCK && cmd[2] == 3 && cmd[5] == 0) {
        static_cast<uint8_t *>(data)[5] = 1;
        readClockRewritten++;
        publishCounters();
    }
}

#pragma mark - Non-flushable ACL for MediaTek

/*
 * bluetoothd sends A2DP media as "first automatically flushable" ACL
 * fragments (packet boundary flag 0b10). MediaTek firmware flushes those
 * when a large L2CAP frame (AAC-ELD to AirPods, ~966 bytes) does not make
 * it in one baseband packet, so the headphones get broken frames, never
 * start playback and pause the stream. Linux always sends ACL data as
 * non-flushable (0b00) to these controllers. Rewrite the flag on HCI ACL
 * packets going to bulk OUT endpoints.
 */
static uint32_t pbRewritten;

IOReturn CSimpleMtkBtPatcher::newPipeIo(void *that, IOMemoryDescriptor *dataBuffer, uint32_t dataBufferLength, IOUSBHostCompletion *completion, uint32_t completionTimeoutMs)
{
    IOUSBHostPipe *pipe = static_cast<IOUSBHostPipe *>(that);
    if (dataBuffer != nullptr && dataBufferLength >= 4 && dataBufferLength <= 1100) {
        const StandardUSB::EndpointDescriptor *ep = pipe->getEndpointDescriptor();
        if (ep != nullptr &&
            (ep->bmAttributes & 0x03) == 0x02 &&        /* bulk */
            (ep->bEndpointAddress & 0x80) == 0 &&       /* OUT */
            dataBuffer->prepare(kIODirectionInOut) == kIOReturnSuccess) {
            uint8_t hdr[4];
            if (dataBuffer->readBytes(0, hdr, sizeof(hdr)) == sizeof(hdr)) {
                uint16_t handle = hdr[0] | (hdr[1] << 8);
                uint16_t len = hdr[2] | (hdr[3] << 8);
                /* looks like HCI ACL: sane handle, length matches, PB = 0b10 */
                if ((handle & 0x0fff) < 0x0f00 && len + 4u == dataBufferLength &&
                    ((handle >> 12) & 3) == 2) {
                    hdr[1] &= ~0x30;
                    if (dataBuffer->writeBytes(1, &hdr[1], 1) == 1) {
                        if (pbRewritten++ < 3)
                            SYSLOG(DRV_NAME, "ACL PB flushable->non-flushable handle 0x%03x len %u", handle & 0x0fff, len);
                        if ((++aclPbRewritten & 0x3ff) == 1)
                            publishCounters();
                    }
                }
            }
            dataBuffer->complete(kIODirectionInOut);
        }
    }
    return FunctionCast(newPipeIo, callbackMtkBtPatcher->oldPipeIo)(that, dataBuffer, dataBufferLength, completion, completionTimeoutMs);
}
