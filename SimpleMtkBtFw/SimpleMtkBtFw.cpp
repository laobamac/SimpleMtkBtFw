//
//  SimpleMtkBtFw.cpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#include "BtMtk.h"

#include "SimpleMtkBtFw.hpp"
#include <libkern/libkern.h>
#include <libkern/OSKextLib.h>
#include <libkern/version.h>
#include <libkern/OSTypes.h>
#include <IOKit/usb/StandardUSB.h>
#include "Hci.h"
#include "linux.h"
#include "Log.h"

#define super IOService
OSDefineMetaClassAndStructors(SimpleMtkBtFw, IOService)

enum { kMyOffPowerState = 0, kMyOnPowerState = 1 };

#define kIOPMPowerOff 0

static IOPMPowerState myTwoStates[2] =
{
    {1, kIOPMPowerOff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

bool SimpleMtkBtFw::init(OSDictionary *dictionary)
{
    XYLog("Driver init()\n");
    return super::init(dictionary);
}

void SimpleMtkBtFw::free()
{
    XYLog("Driver free()\n");
    super::free();
}

bool SimpleMtkBtFw::start(IOService *provider)
{
    XYLog("Driver Start()\n");
    char fwName[64] {};
    m_pDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (m_pDevice == NULL) {
        XYLog("Driver Start fail, not usb device\n");
        return false;
    }
    PMinit();
    registerPowerDriver(this, myTwoStates, 2);
    provider->joinPMtree(this);
    makeUsable();

    if (!super::start(provider)) {
        return false;
    }

    if (!m_pDevice->open(this)) {
        XYLog("start fail, can not open device\n");
        publishReg(false, nullptr);
        cleanUp();
        stop(this);
        return false;
    }

    m_pBTMtk = new BtMtk();
    if (!m_pBTMtk || !m_pBTMtk->initWithDevice(this, m_pDevice)) {
        XYLog("start fail, can not init device\n");
        publishReg(false, nullptr);
        cleanUp();
        stop(this);
        return false;
    }

    XYLog("BT init succeed\n");
    if (!m_pBTMtk->setup()) {
        publishReg(false, nullptr);
        cleanUp();
        stop(this);
        return false;
    }
    m_pBTMtk->getFirmwareName(fwName, sizeof(fwName));
    publishReg(true, fwName);
    cleanUp();
    return true;
}

void SimpleMtkBtFw::publishReg(bool isSucceed, const char *fwName)
{
    m_pDevice->setProperty("FirmwareLoaded", isSucceed);
    if (isSucceed && fwName) {
        OSString *fwNameString = OSString::withCString(fwName);
        if (fwNameString) {
            setProperty("fw_name", fwNameString);
            fwNameString->release();
        }
    }
    if (version_major >= 21) {
        m_pDevice->setName("Bluetooth USB Host Controller");
    }
}

void SimpleMtkBtFw::cleanUp()
{
    XYLog("Clean up...\n");
    OSSafeReleaseNULL(m_pBTMtk);
    if (m_pDevice) {
        if (m_pDevice->isOpen(this)) {
            m_pDevice->close(this);
        }
        m_pDevice = NULL;
    }
}

IOReturn SimpleMtkBtFw::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice)
{
    return IOPMAckImplied;
}

void SimpleMtkBtFw::stop(IOService *provider)
{
    XYLog("Driver Stop()\n");
    PMstop();
    super::stop(provider);
}

IOService *SimpleMtkBtFw::probe(IOService *provider, SInt32 *score)
{
    XYLog("Driver Probe()\n");
    if (!super::probe(provider, score)) {
        XYLog("super probe failed\n");
        return NULL;
    }
    m_pDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (!m_pDevice) {
        XYLog("is not usb device\n");
        return NULL;
    }
    UInt16 vendorID = USBToHost16(m_pDevice->getDeviceDescriptor()->idVendor);
    UInt16 productID = USBToHost16(m_pDevice->getDeviceDescriptor()->idProduct);
    XYLog("name=%s, class=%s, vendorID=0x%04X, productID=0x%04X\n",
          m_pDevice->getName(), provider->metaClass->getClassName(), vendorID, productID);
    m_pDevice = NULL;
    return this;
}
