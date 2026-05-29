//
//  USBDeviceController.cpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#include "USBDeviceController.hpp"
#include "Log.h"
#include "Hci.h"
#include <kern/clock.h>

#define super OSObject
OSDefineMetaClassAndStructors(USBDeviceController, OSObject)

#define kReadBufferSize 4096

static bool isBluetoothControlInterface(const StandardUSB::InterfaceDescriptor *interfaceDescriptor)
{
    return interfaceDescriptor &&
           interfaceDescriptor->bInterfaceClass == 0xe0 &&
           interfaceDescriptor->bInterfaceSubClass == 0x01 &&
           interfaceDescriptor->bInterfaceProtocol == 0x01;
}

static bool hasBluetoothHciEndpoints(IOUSBHostInterface *interface)
{
    if (!interface) {
        return false;
    }

    const StandardUSB::ConfigurationDescriptor *configDescriptor = interface->getConfigurationDescriptor();
    const StandardUSB::InterfaceDescriptor *interfaceDescriptor = interface->getInterfaceDescriptor();
    if (!configDescriptor || !interfaceDescriptor) {
        return false;
    }

    bool hasInterruptIn = false;
    bool hasBulkIn = false;
    bool hasBulkOut = false;
    const EndpointDescriptor *endpointDescriptor = NULL;
    while ((endpointDescriptor = StandardUSB::getNextEndpointDescriptor(configDescriptor, interfaceDescriptor, endpointDescriptor))) {
        uint8_t epDirection = StandardUSB::getEndpointDirection(endpointDescriptor);
        uint8_t epType = StandardUSB::getEndpointType(endpointDescriptor);

        if (epDirection == kUSBIn && epType == kUSBInterrupt) {
            hasInterruptIn = true;
        } else if (epDirection == kUSBIn && epType == kUSBBulk) {
            hasBulkIn = true;
        } else if (epDirection == kUSBOut && epType == kUSBBulk) {
            hasBulkOut = true;
        }
    }

    return hasInterruptIn && hasBulkIn && hasBulkOut;
}

bool USBDeviceController::
init(IOService *client, IOUSBHostDevice *dev)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (!super::init()) {
        return false;
    }
    _hciLock = IOLockAlloc();
    if (!_hciLock) {
        return false;
    }
    mReadBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task
                                                              , kIODirectionIn, kReadBufferSize);
    if (!mReadBuffer) {
        XYLog("Fail to alloc read buffer\n");
        return false;
    }
    
    mReadBuffer->prepare(kIODirectionIn);
    m_pDevice = dev;
    m_pClient = client;
    return true;
}

bool USBDeviceController::
findInterface()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    OSIterator* iterator = m_pDevice->getChildIterator(gIOServicePlane);
    OSObject* candidate = NULL;
    if (iterator == NULL) {
        XYLog("can not create child iterator\n");
        return false;
    }
    while((candidate = iterator->getNextObject()) != NULL) {
        IOUSBHostInterface* interfaceCandidate = OSDynamicCast(IOUSBHostInterface, candidate);
        if(interfaceCandidate != NULL && hasBluetoothHciEndpoints(interfaceCandidate)) {
            const StandardUSB::InterfaceDescriptor *interfaceDescriptor = interfaceCandidate->getInterfaceDescriptor();
            XYLog("Found interface number %u class=0x%02x subclass=0x%02x protocol=0x%02x\n",
                  interfaceDescriptor->bInterfaceNumber,
                  interfaceDescriptor->bInterfaceClass,
                  interfaceDescriptor->bInterfaceSubClass,
                  interfaceDescriptor->bInterfaceProtocol);
            if (!m_pInterface || isBluetoothControlInterface(interfaceDescriptor)) {
                OSSafeReleaseNULL(m_pInterface);
                interfaceCandidate->retain();
                m_pInterface = interfaceCandidate;
            }
            if (isBluetoothControlInterface(interfaceDescriptor)) {
                XYLog("Selected Bluetooth control interface\n");
                break;
            }
        }
    }
    OSSafeReleaseNULL(iterator);
    if (m_pInterface == NULL) {
        XYLog("can not find Bluetooth HCI interface\n");
        return false;
    }
    if (!m_pInterface->open(m_pClient)) {
        XYLog("can not open interface\n");
        OSSafeReleaseNULL(m_pInterface);
        return false;
    }
    return true;
}

void USBDeviceController::
free()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (m_pBulkWritePipe) {
        m_pBulkWritePipe->abort();
        OSSafeReleaseNULL(m_pBulkWritePipe);
    }
    if (m_pBulkReadPipe) {
        m_pBulkReadPipe->abort();
        OSSafeReleaseNULL(m_pBulkReadPipe);
    }
    if (m_pInterruptReadPipe) {
        m_pInterruptReadPipe->abort();
        OSSafeReleaseNULL(m_pInterruptReadPipe);
    }
    if (mReadBuffer) {
        mReadBuffer->complete(kIODirectionIn);
        OSSafeReleaseNULL(mReadBuffer);
    }
    if (_hciLock) {
        IOLockFree(_hciLock);
        _hciLock = NULL;
    }
    if (m_pInterface) {
        if (m_pClient && m_pInterface->isOpen(m_pClient)) {
            m_pInterface->close(m_pClient);
            m_pClient = NULL;
        }
        OSSafeReleaseNULL(m_pInterface);
    }
    m_pDevice = NULL;
    super::free();
}

bool USBDeviceController::
initConfiguration()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    uint8_t configIndex = 0;
    uint8_t configNum = m_pDevice->getDeviceDescriptor()->bNumConfigurations;
    if (configNum < configIndex + configNum) {
        XYLog("config num error (num: %d)\n", configNum);
        return false;
    }
    const StandardUSB::ConfigurationDescriptor *configDescriptor = m_pDevice->getConfigurationDescriptor(configIndex);
    if (!configDescriptor) {
        XYLog("getConfigurationDescriptor(%d) failed\n", configIndex);
        return false;
    }
    XYLog("set configuration to %d\n", configDescriptor->bConfigurationValue);
    IOReturn ret = m_pDevice->setConfiguration(configDescriptor->bConfigurationValue);
    if (ret != kIOReturnSuccess) {
        XYLog("set device configuration to %d failed\n", configDescriptor->bConfigurationValue);
        return false;
    }
    return true;
}

bool USBDeviceController::
findPipes()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    const StandardUSB::ConfigurationDescriptor *configDescriptor;
    const StandardUSB::InterfaceDescriptor *interfaceDescriptor;
    const EndpointDescriptor *endpointDescriptor;
    uint8_t epDirection, epType;
    
    configDescriptor = m_pInterface->getConfigurationDescriptor();
    interfaceDescriptor = m_pInterface->getInterfaceDescriptor();
    if (configDescriptor == NULL || interfaceDescriptor == NULL) {
        XYLog("Find descriptor NULL\n");
        return false;
    }
    endpointDescriptor = NULL;
    while ((endpointDescriptor = StandardUSB::getNextEndpointDescriptor(configDescriptor, interfaceDescriptor, endpointDescriptor))) {
        epDirection = StandardUSB::getEndpointDirection(endpointDescriptor);
        epType = StandardUSB::getEndpointType(endpointDescriptor);
        if (epDirection == kUSBIn && epType == kUSBInterrupt) {
            XYLog("Found Interrupt endpoint!\n");
            m_pInterruptReadPipe = m_pInterface->copyPipe(StandardUSB::getEndpointAddress(endpointDescriptor));
            if (m_pInterruptReadPipe == NULL) {
                XYLog("Copy InterruptReadPipe pipe fail\n");
                return false;
            }
        } else if (epDirection == kUSBOut && epType == kUSBBulk) {
            XYLog("Found Bulk out endpoint!\n");
            m_pBulkWritePipe = m_pInterface->copyPipe(StandardUSB::getEndpointAddress(endpointDescriptor));
            if (m_pBulkWritePipe == NULL) {
                XYLog("Copy Bulk pipe fail\n");
                return false;
            }
        } else if (epDirection == kUSBIn && epType == kUSBBulk) {
            XYLog("Found Bulk in endpoint!\n");
            m_pBulkReadPipe = m_pInterface->copyPipe(StandardUSB::getEndpointAddress(endpointDescriptor));
            if (m_pBulkReadPipe == NULL) {
                XYLog("Copy Bulk pipe fail\n");
                return false;
            }
        }
    }
    return (m_pInterruptReadPipe != NULL && m_pBulkWritePipe != NULL && m_pBulkReadPipe != NULL);
}

IOReturn USBDeviceController::
bulkPipeRead(void *buf, uint32_t buf_size, uint32_t *size, uint32_t timeout)
{
    uint32_t actualLength = 0;
    IOReturn ret = m_pBulkReadPipe->io(mReadBuffer, (uint32_t)mReadBuffer->getLength(), actualLength, timeout);
    if (ret == kIOUSBPipeStalled) {
        m_pBulkReadPipe->clearStall(true);
        ret = m_pBulkReadPipe->io(mReadBuffer, (uint32_t)mReadBuffer->getLength(), actualLength, timeout);
    }
    if (ret == kIOReturnSuccess) {
        if (buf && actualLength > buf_size) {
            XYLog("%s buf size too small. buflen: %d act: %d\n", __FUNCTION__, buf_size, actualLength);
        }
        if (buf) {
            memcpy(buf, mReadBuffer->getBytesNoCopy(), min(actualLength, buf_size));
        }
        if (size) {
            *size = min(actualLength, buf_size);
        }
    } else {
        XYLog("%s failed: %s %d\n", __FUNCTION__, stringFromReturn(ret), ret);
    }
    return ret;
}

void USBDeviceController::
interruptHandler(void *owner, void *parameter, IOReturn status, uint32_t bytesTransferred)
{
    USBDeviceController *controller = OSDynamicCast(USBDeviceController, (OSObject *)owner);
    if (!controller || !parameter) {
        return;
    }
    switch (status) {
        case kIOReturnSuccess:
            break;
        case kIOReturnNotResponding:
            controller->m_pInterruptReadPipe->clearStall(false);
        default:
            XYLog("%s status: %s (%d) len: %d\n", __FUNCTION__, controller->stringFromReturn(status), status, bytesTransferred);
            break;
    }
    
    InterruptResp *resp = (InterruptResp *)parameter;
    resp->status = status;
    resp->dataLen = bytesTransferred;
    IOLockWakeup(controller->_hciLock, controller, true);
}

IOReturn USBDeviceController::
interruptPipeRead(void *buf, uint32_t buf_size, uint32_t *size, uint32_t timeout)
{
    AbsoluteTime deadline;
    IOUSBHostCompletion comple;
    InterruptResp interrupResp;
    
    clock_interval_to_deadline(timeout, kMillisecondScale, reinterpret_cast<uint64_t*> (&deadline));
    memset(&interrupResp, 0, sizeof(interrupResp));
    comple.action = interruptHandler;
    comple.owner = this;
    comple.parameter = &interrupResp;
    
    IOReturn ret = m_pInterruptReadPipe->io(mReadBuffer, (uint32_t)mReadBuffer->getLength(), &comple, 0);
    if (ret == kIOUSBPipeStalled) {
        m_pInterruptReadPipe->clearStall(true);
        ret = m_pInterruptReadPipe->io(mReadBuffer, (uint32_t)mReadBuffer->getLength(), &comple, 0);
    }
    
    if (ret == kIOReturnSuccess) {
        IOLockLock(_hciLock);
        if (IOLockSleepDeadline(_hciLock, this, deadline, THREAD_INTERRUPTIBLE) != THREAD_AWAKENED) {
            IOLockUnlock(_hciLock);
            m_pInterruptReadPipe->abort();
            XYLog("%s Timeout\n", __FUNCTION__);
            return kIOReturnTimeout;
        }
        if (interrupResp.dataLen <= 0) {
            IOLockUnlock(_hciLock);
            XYLog("%s invalid response size: %d\n", __FUNCTION__, interrupResp.dataLen);
            return kIOReturnError;
        }
        if (buf && interrupResp.dataLen > buf_size) {
            XYLog("%s buf size too small. buflen: %d act: %d\n", __FUNCTION__, buf_size, interrupResp.dataLen);
        }
        if (buf) {
            memcpy(buf, mReadBuffer->getBytesNoCopy(), min(interrupResp.dataLen, buf_size));
        }
        if (size) {
            *size = min(interrupResp.dataLen, buf_size);
        }
        IOLockUnlock(_hciLock);
    } else {
        XYLog("%s failed: %s %d\n", __FUNCTION__, stringFromReturn(ret), ret);
    }
    return ret;
}

IOReturn USBDeviceController::
sendHCIRequest(HciCommandHdr *cmd, uint32_t timeout)
{
    uint32_t actualLength;
    StandardUSB::DeviceRequest request =
    {
        .bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeClass, kRequestRecipientDevice),
        .bRequest = 0,
        .wValue = 0,
        .wIndex = 0,
        .wLength = (uint16_t)(HCI_COMMAND_HDR_SIZE + cmd->len)
    };
    
    return m_pInterface->deviceRequest(request, cmd, actualLength, timeout);
}

IOReturn USBDeviceController::
controlRequest(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
               void *data, uint16_t length, uint32_t timeout)
{
    uint32_t actualLength = 0;
    StandardUSB::DeviceRequest request =
    {
        .bmRequestType = bmRequestType,
        .bRequest = bRequest,
        .wValue = wValue,
        .wIndex = wIndex,
        .wLength = length
    };
    
    return m_pDevice->deviceRequest(m_pClient, request, data, actualLength, timeout);
}

IOReturn USBDeviceController::
readWMTEvent(void *buf, uint32_t bufSize, uint32_t *size, uint32_t timeout)
{
    if (!buf || bufSize == 0) {
        return kIOReturnBadArgument;
    }
    
    uint8_t tmp[64] {};
    uint32_t actualLength = 0;
    uint64_t deadline = 0;
    StandardUSB::DeviceRequest request =
    {
        .bmRequestType = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeVendor, kRequestRecipientDevice),
        .bRequest = 1,
        .wValue = 48,
        .wIndex = 0,
        .wLength = sizeof(tmp)
    };
    
    clock_interval_to_deadline(timeout, kMillisecondScale, &deadline);
    while (true) {
        actualLength = 0;
        memset(tmp, 0, sizeof(tmp));
        IOReturn ret = m_pDevice->deviceRequest(m_pClient, request, tmp, actualLength, 100);
        if (ret != kIOReturnSuccess && ret != kIOReturnTimeout) {
            return ret;
        }
        if (ret == kIOReturnSuccess && actualLength > 0) {
            uint32_t copyLength = min(actualLength, bufSize);
            memcpy(buf, tmp, copyLength);
            if (size) {
                *size = copyLength;
            }
            if (actualLength > bufSize) {
                XYLog("%s buf size too small. buflen: %d act: %d\n", __FUNCTION__, bufSize, actualLength);
            }
            return kIOReturnSuccess;
        }
        uint64_t now = 0;
        clock_get_uptime(&now);
        if (now >= deadline) {
            break;
        }
        IODelay(500);
    }
    return kIOReturnTimeout;
}

IOReturn USBDeviceController::
bulkWrite(const void *data, uint32_t length, uint32_t timeout)
{
    IOMemoryDescriptor* buffer = IOMemoryDescriptor::withAddress((void *)data, length, kIODirectionOut);
    if (!buffer) {
        XYLog("Unable to allocate bulk write buffer.\n");
        return kIOReturnNoMemory;
    }
    IOReturn ret;
    uint32_t actLen = 0;
    if ((ret = buffer->prepare(kIODirectionOut)) != kIOReturnSuccess) {
        XYLog("Failed to prepare bulk write memory buffer (error %d).\n", ret);
        buffer->release();
        return ret;
    }
    if ((ret = m_pBulkWritePipe->io(buffer, (uint32_t)buffer->getLength(), actLen, timeout)) != kIOReturnSuccess) {
        XYLog("Failed to write to bulk pipe (error %d)\n", ret);
        buffer->complete();
        buffer->release();
        return ret;
    }
    if ((ret = buffer->complete(kIODirectionOut)) != kIOReturnSuccess) {
        XYLog("Failed to complete bulk write memory buffer (error %d)\n", ret);
        buffer->release();
        return ret;
    }
    return ret;
}

const char* USBDeviceController::
stringFromReturn(IOReturn code)
{
    return m_pDevice->stringFromReturn(code);
}
