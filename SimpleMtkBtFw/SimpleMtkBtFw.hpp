/** @file
  Copyright (c) 2020 zxystd. All rights reserved.
  SPDX-License-Identifier: GPL-3.0-only
**/

//
//  IntelBluetoothFirmware.hpp
//  IntelBluetoothFirmware
//
//  Created by zxystd on 2019/11/17.
//  Copyright © 2019 zxystd. All rights reserved.
//

//
//  SimpleMtkBtFw.hpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#ifndef SimpleMtkBtFw_H
#define SimpleMtkBtFw_H

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/usb/USB.h>
#include <libkern/OSKextLib.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>

#include "BtMtk.h"

class SimpleMtkBtFw : public IOService
{
    OSDeclareDefaultStructors(SimpleMtkBtFw)

public:
    bool init(OSDictionary *dictionary = NULL) override;
    void free() override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;

    void cleanUp();
    void publishReg(bool isSucceed, const char *fwName);

private:
    BtMtk *m_pBTMtk {nullptr};
    IOUSBHostDevice *m_pDevice {nullptr};
};

#endif
