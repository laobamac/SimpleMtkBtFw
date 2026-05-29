//
//  Log.h
//  SimpleMtkBtFw
//
//  Created by laobamac on 2026/5/30.
//  Copyright © 2026 laobamac. All rights reserved.
//

#ifndef Log_h
#define Log_h

#include <IOKit/IOLib.h>

#define XYLog(fmt, x...)\
do\
{\
	IOLog("%s: " fmt, "SimpleMtkBtFw", ##x);\
}while(0)

#endif /* Log_h */
