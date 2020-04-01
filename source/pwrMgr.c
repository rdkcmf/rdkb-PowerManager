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

#ifndef _RDKB_POWER_MGR_C_
#define _RDKB_POWER_MGR_C_

/**
 *  @file pwrMgr.c
 *  @brief RDKB Power Manger
 *
 *  This file provides the implementation for the RDKB Power Manager. The
 *  processing here only handles the messaging to trigger power state transitions.
 *  There is an RDKB companion script which will perform the actual orderly
 *  shutdown and startup of the RDKB CCSP components.
 *
 *  This code is listening for the following power system transition events:
 *  Transition from Battery to AC:
 *  sysevent set rdkb-power-transition POWER_TRANS_AC
 *
 *  Transition from AC to Battery
 *  sysevent set rdkb-power-transition POWER_TRANS_BATTERY
 *
 *  When the transition is complete, the rdkb power state will change:
 *  rdkb-power-state AC
 *  rdkb-power-state BATTERY
 *
 */

/**************************************************************************/
/*      INCLUDES:                                                         */
/**************************************************************************/
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sysevent/sysevent.h>
#include <syscfg/syscfg.h>
#include <pthread.h>
#include <stdarg.h>
#include "stdbool.h"
#include "pwrMgr.h"

/**************************************************************************/
/*      LOCAL VARIABLES:                                                  */
/**************************************************************************/
static int sysevent_fd;
static token_t sysevent_token;
static pthread_t sysevent_tid;
static int sysevent_fd_gs;
static token_t sysevent_token_gs;

#define INFO  0
#define WARNING  1
#define ERROR 2

#ifdef FEATURE_SUPPORT_RDKLOG
#include "ccsp_trace.h"
const char compName[25]="LOG.RDK.PWRMGR";
#define DEBUG_INI_NAME  "/etc/debug.ini"
#define PWRMGRLOG(x, ...) { if((x)==(INFO)){CcspTraceInfo((__VA_ARGS__));}else if((x)==(WARNING)){CcspTraceWarning((__VA_ARGS__));}else if((x)==(ERROR)){CcspTraceError((__VA_ARGS__));} }
#else
#define PWRMGRLOG(x, ...) {fprintf(stderr, "PowerMgrLog<%s:%d> ", __FUNCTION__, __LINE__);fprintf(stderr, __VA_ARGS__);}
#endif

// We need to put this after the ccsp_trace.h above, since it re-defines CHAR
#include "mta_hal.h"

#define _DEBUG 1
#define THREAD_NAME_LEN 16 //length is restricted to 16 characters, including the terminating null byte
#define DATA_SIZE 1024

// Power Management state structure. This should have PWRMGR_STATE_TOTAL-1 entries
PWRMGR_PwrStateItem powerStateArr[] = { {PWRMGR_STATE_UNKNOWN, "POWER_TRANS_UNKNOWN", "Unknown"},
                                        {PWRMGR_STATE_AC,   "POWER_TRANS_AC", "AC"},
                                        {PWRMGR_STATE_BATT, "POWER_TRANS_BATTERY", "Battery"} };

static PWRMGR_PwrState gCurPowerState;

static int PwrMgr_StateTranstion(char *cState);

/**
 *  @brief Set Power Manager system defaults
 *  @return 0
 */
static void PwrMgr_SetDefaults()
{
    // Not sure what we should do here. Should we ask someone what the current state is? Basically if we
    // boot up in battery mode are we going to get a later notification that there was a power state change?
    // For now, we will call the mta hal to see what our current power state is.
    gCurPowerState = PWRMGR_STATE_AC;
    char status[DATA_SIZE] = {0};
    int len = 0;
    int halStatus = RETURN_OK;

    // Fetch the current battery status from mta - returns "AC", "Battery" or "Unknown"
    halStatus = mta_hal_BatteryGetPowerStatus (&status, &len);

    if (halStatus == RETURN_OK && len > 0 && status[0] != 0) {
        PWRMGRLOG(INFO, "%s: Power Manager mta_hal_BatteryGetPowerStatus returned %s\n",__FUNCTION__, status);

        if (strcmp(status, powerStateArr[PWRMGR_STATE_BATT].pwrStateStr) == 0) {
            PwrMgr_StateTranstion(powerStateArr[PWRMGR_STATE_BATT].pwrTransStr);
        }
    } else {
        PWRMGRLOG(ERROR, "%s: Power Manager mta_hal_BatteryGetPowerStatus call FAILED!\n",__FUNCTION__);
    }

    PWRMGRLOG(INFO, "%s: Power Manager initializing with %s\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrStateStr);


    // wait a couple seconds before sending the initial sysevent
    sleep(5);
    PwrMgr_SyseventSetStr("rdkb-power-state", powerStateArr[gCurPowerState].pwrStateStr, 0);
}

/**
 *  @brief Send sysevent string
 *  @return 0
 */
int PwrMgr_SyseventSetStr(const char *name, unsigned char *value, int bufsz)
{
    return sysevent_set(sysevent_fd_gs, sysevent_token_gs, name, value, bufsz);
}

/**
 *  @brief Transition power states
 *  @return 0
 */
static int PwrMgr_StateTranstion(char *cState)
{
    FILE *fp = NULL;
    char cmd[DATA_SIZE] = {0};
    bool transSuccess = false;

    PWRMGR_PwrState newState = PWRMGR_STATE_UNKNOWN;
    PWRMGRLOG(INFO, "Entering into %s new state\n",__FUNCTION__);

    // Convert from sysevent string to power state
    int i=0;
    for (i=0;i<PWRMGR_STATE_TOTAL;i++) {
        if (strcmp(powerStateArr[i].pwrTransStr,cState) == 0) {
            newState = powerStateArr[i].pwrState;
            break;
        }
    }

    if (newState == gCurPowerState) {
        PWRMGRLOG(WARNING, "%s: Power transition requested to current state %s ignored\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrTransStr);
    } else {
        // Check the state we are transitioning to
        switch (newState){
        case PWRMGR_STATE_AC:
            PWRMGRLOG(INFO, "%s: Power transition requested from %s to %s\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrTransStr, powerStateArr[newState].pwrTransStr);
            // We need to call an RDKB management script to tear down the CCSP components.
            sprintf(cmd, "/bin/sh /usr/ccsp/pwrMgr/rdkb_power_manager.sh POWER_TRANS_AC");

            if (system( cmd ) == 0)
            {
                transSuccess = true;
                gCurPowerState = newState;
            }
            else
            {
                /* Could not run command we can't transition to new state */
                PWRMGRLOG(ERROR, "Error opening command pipe during power transition! \n");
                return true;
            }

            break;
        case PWRMGR_STATE_BATT:
            PWRMGRLOG(INFO, "%s: Power transition requested from %s to %s\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrTransStr, powerStateArr[newState].pwrTransStr);
            // We need to call an RDKB management script to tear down the CCSP components.
            sprintf(cmd, "/bin/sh /usr/ccsp/pwrMgr/rdkb_power_manager.sh POWER_TRANS_BATTERY");

            if (system( cmd ) == 0)
            {
                transSuccess = true;
                gCurPowerState = newState;
            }
            else
            {
                /* Could not run command we can't transition to new state */
                PWRMGRLOG(ERROR, "Error opening command pipe during power transition! \n");
                return true;
            }

            break;
        default:
            PWRMGRLOG(ERROR, "%s: Transition requested to unknown power state %s\n",__FUNCTION__, cState);
            break;
        }

        if (transSuccess) {
            PWRMGRLOG(INFO, "%s: Power transition to %s Success\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrTransStr);
            PwrMgr_SyseventSetStr("rdkb-power-state", powerStateArr[gCurPowerState].pwrStateStr, 0);
        } else {
            PWRMGRLOG(ERROR, "%s: Power transition to %s FAILED\n",__FUNCTION__, powerStateArr[gCurPowerState].pwrTransStr);
        }
    }

    PWRMGRLOG(INFO, "Exiting from %s\n",__FUNCTION__);
    return 0;
}

/**
 *  @brief Power Manager Sysevent handler
 *  @return 0
 */
static void *PwrMgr_sysevent_handler(void *data)
{
    PWRMGRLOG(INFO, "Entering into %s\n",__FUNCTION__)

    /* Power transition event ids */
    async_id_t power_transition_asyncid;

    sysevent_setnotification(sysevent_fd, sysevent_token, "rdkb-power-transition",  &power_transition_asyncid);
    sysevent_set_options(sysevent_fd_gs, sysevent_token, "rdkb-power-state", TUPLE_FLAG_EVENT);

    for (;;)
    {
        unsigned char name[25], val[42];
        int namelen = sizeof(name);
        int vallen  = sizeof(val);
        int err;
        async_id_t getnotification_asyncid;


        err = sysevent_getnotification(sysevent_fd, sysevent_token, name, &namelen,  val, &vallen, &getnotification_asyncid);

        if (err)
        {
            PWRMGRLOG(ERROR, "sysevent_getnotification failed with error: %d\n", err)
            if ( 0 != system("pidof syseventd")) {

                CcspTraceWarning(("%s syseventd not running  \n",__FUNCTION__));
           	sleep(600);
	    } 
        }
        else
        {
            PWRMGRLOG(WARNING, "received notification event %s\n", name)

            if (strcmp(name, "rdkb-power-transition") == 0)
            {
                if (vallen > 0 && val[0] != '\0') {
                    PwrMgr_StateTranstion(val);
                }
            }
            else
            {
                PWRMGRLOG(WARNING, "undefined event %s \n",name)
            }			
        }
    }

    PWRMGRLOG(INFO, "Exiting from %s\n",__FUNCTION__)
    return 0;
}

/**
 *  @brief Power Manager register for system events
 *  @return 0
 */
static bool PwrMgr_Register_sysevent()
{
    bool status = false;
    const int max_retries = 6;
    int retry = 0;
    PWRMGRLOG(INFO, "Entering into %s\n",__FUNCTION__)

    do
    {
        sysevent_fd = sysevent_open("127.0.0.1", SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, "rdkb_power_manger", &sysevent_token);
        if (sysevent_fd < 0)
        {
            PWRMGRLOG(ERROR, "rdkb_power_manager failed to register with sysevent daemon\n");
            status = false;
        }
        else
        {  
            PWRMGRLOG(INFO, "rdkb_power_manager registered with sysevent daemon successfully\n");
            status = true;
        }

        //Make another connection for gets/sets
        sysevent_fd_gs = sysevent_open("127.0.0.1", SE_SERVER_WELL_KNOWN_PORT, SE_VERSION, "rdkb_power_manager-gs", &sysevent_token_gs);
        if (sysevent_fd_gs < 0)
        {
            PWRMGRLOG(ERROR, "rdkb_power_manager-gs failed to register with sysevent daemon\n");
            status = false;
        }
        else
        {
            PWRMGRLOG(INFO, "rdkb_power_manager-gs registered with sysevent daemon successfully\n");
            status = true;
        }

        if(status == false) {
        	system("/usr/bin/syseventd");
                sleep(5);
        }
    }while((status == false) && (retry++ < max_retries));

    if (status != false)
       PwrMgr_SetDefaults();

    PWRMGRLOG(INFO, "Exiting from %s\n",__FUNCTION__);
    return status;
}

/**
 *  @brief Power Manager initialize code
 *  @return 0
 */
static int PwrMgr_Init()
{
    int status = 0;
    int thread_status = 0;
    char thread_name[THREAD_NAME_LEN];
    PWRMGRLOG(INFO, "Entering into %s\n",__FUNCTION__)

    if (PwrMgr_Register_sysevent() == false)
    {
        PWRMGRLOG(ERROR, "PwrMgr_Register_sysevent failed\n")
        status = -1;
    }
    else 
    {
        PWRMGRLOG(INFO, "PwrMgr_Register_sysevent Successful\n")
    
        thread_status = pthread_create(&sysevent_tid, NULL, PwrMgr_sysevent_handler, NULL);
        if (thread_status == 0)
        {
            PWRMGRLOG(INFO, "PwrMgr_sysevent_handler thread created successfully\n");

            memset( thread_name, '\0', sizeof(char) * THREAD_NAME_LEN );
            strcpy( thread_name, "pwrMgr_sysevent");

            if (pthread_setname_np(sysevent_tid, thread_name) == 0)
                PWRMGRLOG(INFO, "PwrMgr_sysevent_handler thread name %s set successfully\n", thread_name)
            else
                PWRMGRLOG(ERROR, "%s error occurred while setting PwrMgr_sysevent_handler thread name\n", strerror(errno))
                
            sleep(5);
        }
        else
        {
            PWRMGRLOG(ERROR, "%s error occured while creating PwrMgr_sysevent_handler thread\n", strerror(errno))
            status = -1;
        }
    }
    PWRMGRLOG(INFO, "Exiting from %s\n",__FUNCTION__)
    return status;
}

/**
 *  @brief Power Manager check to see if we are already running
 *  @return 0
 */
static bool checkIfAlreadyRunning(const char* name)
{
    PWRMGRLOG(INFO, "Entering into %s\n",__FUNCTION__)
    bool status = true;
	
    FILE *fp = fopen("/tmp/.rdkbPowerMgr.pid", "r");
    if (fp == NULL) 
    {
        PWRMGRLOG(ERROR, "File /tmp/.rdkbPowerMgr.pid doesn't exist\n")
        FILE *pfp = fopen("/tmp/.rdkbPowerMgr.pid", "w");
        if (pfp == NULL) 
        {
            PWRMGRLOG(ERROR, "Error in creating file /tmp/.rdkbPowerMgr.pid\n")
        }
        else
        {
            pid_t pid = getpid();
            fprintf(pfp, "%d", pid);
            fclose(pfp);
        }
        status = false;
    }
    else
    {
        fclose(fp);
    }
    PWRMGRLOG(INFO, "Exiting from %s\n",__FUNCTION__)
    return status;
}

/**
 *  @brief Power Manager daemonize process
 *  @return 0
 */
static void daemonize(void) 
{
    PWRMGRLOG(INFO, "Entering into %s\n",__FUNCTION__)
    int fd;
    switch (fork()) {
    case 0:
      	PWRMGRLOG(ERROR, "In child pid=%d\n", getpid())
        break;
    case -1:
    	// Error
    	PWRMGRLOG(ERROR, "Error daemonizing (fork)! %d - %s\n", errno, strerror(errno))
    	exit(0);
    	break;
    default:
     	PWRMGRLOG(ERROR, "In parent exiting\n")
    	_exit(0);
    }

    //create new session and process group
    if (setsid() < 0) {
        PWRMGRLOG(ERROR, "Error demonizing (setsid)! %d - %s\n", errno, strerror(errno))
    	exit(0);
    }    

#ifndef  _DEBUG
    //redirect fd's 0,1,2 to /dev/null     
    fd = open("/dev/null", O_RDONLY);
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 1) {
        dup2(fd, 1);
        close(fd);
    }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 2) {
        dup2(fd, 2);
        close(fd);
    }
#endif	
}


/**
 *  @brief Init and run the Provisioning process
 *  @param[in] argc
 *  @param[in] argv
 *  @return never exits
 **************************************************************************/
int main(int argc, char *argv[])
{
    int status = 0;
    const int max_retries = 6;
    int retry = 0;

#ifdef FEATURE_SUPPORT_RDKLOG
    pComponentName = compName;
    rdk_logger_init(DEBUG_INI_NAME);
#endif

    PWRMGRLOG(INFO, "Started power manager\n")

    daemonize();

    if (checkIfAlreadyRunning(argv[0]) == true)
    {
        PWRMGRLOG(ERROR, "Process %s already running\n", argv[0])
        status = 1;
    }
    else
    {
        if (retry < max_retries)
        {
            if (PwrMgr_Init() != 0)
            {
                PWRMGRLOG(ERROR, "Power Manager Initialization failed\n")
                status = 1;
            }
            else
            {
                PWRMGRLOG(INFO, "Power Manager initialization completed\n")
                //wait for sysevent_tid thread to terminate
                pthread_join(sysevent_tid, NULL);
                
                PWRMGRLOG(INFO,"sysevent_tid thread terminated\n")
            }
        }
        else
        {
            PWRMGRLOG(ERROR, "syscfg init failed permanently\n")
            status = 1;
        }
	PWRMGRLOG(INFO, "power manager app terminated\n")
    }
    return status;
}
#endif
