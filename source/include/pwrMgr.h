/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
/**
 *  @file pwrMgr.h
 *  @brief RDKB Power Manger
 *
 *  This file provides the apis and headers for the RDKB Power Manager. The
 *  processing here only handles the messaging to trigger power state transitions.
 *  There is an RDKB companion script which will perform the actual orderly
 *  shutdown and startup of the RDKB CCSP components.
 *
 *  This code is listening for the following power system transition events:
 *  Transition from Battery to AC:
 *  sysevent set rdkb-power-transition POWER_TRANS_AC
 *
 *  Transition from AC to Battery if Device Supports Battery. Note: As of Now this is XBB Battery Only
 *  sysevent set rdkb-power-transition POWER_TRANS_BATTERY
 *
 *  Transition to Thermal Hot:
 *  sysevent set rdkb-power-transition POWER_TRANS_HOT
 *
 *  Transition from Thermal Hot to Thermal Cooled:
 *  sysevent set rdkb-power-transition POWER_TRANS_COOLED
 *
 *  When the transition is complete, the rdkb power state will change:
 *  rdkb-power-state AC
 *  rdkb-power-state BATTERY
 *  rdkb-power-state ThermalHot
 *  rdkb-power-state ThermalCooled
 *
 */

 
#ifndef _RDKB_POWER_MGR_H_
#define _RDKB_POWER_MGR_H_

typedef enum
{
    PWRMGR_STATE_UNKNOWN = 0,
    PWRMGR_STATE_AC,
#if defined (_XBB1_SUPPORTED_)
    PWRMGR_STATE_BATT,
#endif
    PWRMGR_STATE_HOT,
    PWRMGR_STATE_COOLED,
    PWRMGR_STATE_TOTAL
} PWRMGR_PwrState;

typedef struct
{
    PWRMGR_PwrState pwrState; // Enum value of the power state
    char *pwrTransStr;  // Power State transition string
    char *pwrStateStr;  // Power State string
} PWRMGR_PwrStateItem;

int PwrMgr_SyseventSetStr(const char *name, unsigned char *value, int bufsz);

#endif
