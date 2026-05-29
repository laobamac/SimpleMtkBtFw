//
//  USBDeviceController.hpp
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

//
//  USBHCIController.hpp
//
//  Created by qcwap on 2021/5/21.
//  Copyright © 2021 zxystd. All rights reserved.
//

#ifndef USBDeviceController_hpp
#define USBDeviceController_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/usb/USB.h>
#include <libkern/OSKextLib.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>

#include "Hci.h"

typedef struct {
    int status;
    uint32_t dataLen;
} InterruptResp;

class USBDeviceController : public OSObject {
    OSDeclareDefaultStructors(USBDeviceController)
    
public:
    
    virtual bool init(IOService *client, IOUSBHostDevice *dev);
    
    virtual void free() override;
    
    virtual bool initConfiguration();
    
    virtual bool findInterface();
    
    virtual bool findPipes();
    
    IOReturn bulkPipeRead(void *buf, uint32_t buf_size, uint32_t *size, uint32_t timeout);
    
    IOReturn interruptPipeRead(void *buf, uint32_t buf_size, uint32_t *size, uint32_t timeout);
    
    IOReturn sendHCIRequest(HciCommandHdr *cmd, uint32_t timeout);
    
    IOReturn bulkWrite(const void *data, uint32_t length, uint32_t timeout);
    
    IOReturn controlRequest(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                            void *data, uint16_t length, uint32_t timeout);
    
    IOReturn readWMTEvent(void *buf, uint32_t bufSize, uint32_t *size, uint32_t timeout);
    
    const char* stringFromReturn(IOReturn code);
    
    static void interruptHandler(void *owner, void *parameter, IOReturn status, uint32_t bytesTransferred);
    
private:
    IOUSBHostDevice* m_pDevice {nullptr};
    IOService*  m_pClient {nullptr};
    IOUSBHostInterface* m_pInterface {nullptr};
    
    IOUSBHostPipe* m_pInterruptReadPipe {nullptr};
    IOUSBHostPipe* m_pBulkWritePipe {nullptr};
    IOUSBHostPipe* m_pBulkReadPipe {nullptr};
    
    IOLock *_hciLock {nullptr};
    IOBufferMemoryDescriptor* mReadBuffer {nullptr};
};

#endif /* USBDeviceController_hpp */
