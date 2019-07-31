//
//  SuperSpeedFIFIOBridge.hpp
//  Scope
//
//  Created by Daniel Vasile on 2019-07-30.
//  Copyright © 2019 EEVengers. All rights reserved.
//

#ifndef SuperSpeedFIFIOBridge_hpp
#define SuperSpeedFIFIOBridge_hpp

#include "EVLibrary.h"

/*
 * Used To Get A Handle To The FTDI SuperSpeed FIFO Bridge
 * @params
 * FT_HANDLE *device_handle - A pointer to a variable which will hold the inialized handle to the FIFO SuperSpeed Bridge
 * @return
 * 0 on success
 */
int InitFTDISuperSpeedChip(FT_HANDLE *deviceHandle);



#endif /* SuperSpeedFIFIOBridge_hpp */
