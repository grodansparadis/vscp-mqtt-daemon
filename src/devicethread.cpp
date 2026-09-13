// devicethread.cpp
//
// This file is part of the VSCP (https://www.vscp.org)
//
// The MIT License (MIT)
//
// Copyright (C) 2000-2026 Ake Hedman,  contributors,, the VSCP project
// <info@vscp.org>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#define _POSIX

#ifdef WIN32
#include <pch.h>
#endif

#ifndef DWORD
#define DWORD unsigned long
#endif

#ifndef WIN32
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>

#include <spdlog/fmt/bin_to_hex.h>

#include "devicethread.h"

#include <canal-macro.h>
#include <controlobject.h>
#include <devicelist.h>
#include <level2drvdef.h>
#include <vscp.h>
#include <vscphelper.h>

#include <mustache.hpp>
using namespace kainjow::mustache;

// From vscp.cpp
extern uint8_t *__vscp_key; // (256 bits)



//////////////////////////////////////////////////////////////////////
//                         Callbacks
//////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////
// receive_event_callback
//
// Event received from MQTT client
//

static void
receive_event_callback(vscpEvent &ev, void *pobj)
{
  int rv;

  // Check pointers
  if (nullptr == pobj) {
    return;
  }

  CDeviceItem *pDeviceItem = (CDeviceItem *) pobj;

  spdlog::trace("VSCP Event received. class={0} type={1}", ev.vscp_class, ev.vscp_type);

  // Serialize driver access against close in the device thread
  pthread_mutex_lock(&pDeviceItem->m_deviceMutex);

  if (pDeviceItem->m_bQuit || (0 == pDeviceItem->m_openHandle)) {
    spdlog::warning("receive_event_callback: Device is not open or quitting, returning from receive_event_callback.");
    pthread_mutex_unlock(&pDeviceItem->m_deviceMutex);
    return;
  }

  if (VSCP_DRIVER_LEVEL1 == pDeviceItem->m_driverLevel) {

    canalMsg msg;
    if (!vscp_convertEventToCanal(&msg, &ev)) {
      spdlog::error("receive_event_callback: Failed to convert VSCP event to Canal message.");
      pthread_mutex_unlock(&pDeviceItem->m_deviceMutex);
      return;
    }

    // Use blocking method if available
    spdlog::trace("receive_event_callback: Sending event using CanalBlockingSend if available.");
    if (nullptr != pDeviceItem->m_proc_CanalBlockingSend) {
      spdlog::trace("receive_event_callback: Using CanalBlockingSend to send event.");
      if (CANAL_ERROR_SUCCESS != (rv = pDeviceItem->m_proc_CanalBlockingSend(pDeviceItem->m_openHandle, &msg, 300))) {
        spdlog::error(
          "driver: {}: receive_event_callback - Failed to send event (m_proc_CanalBlockingSend) rv={1}",
          pDeviceItem->m_strName.c_str(),
          rv);
      }
    }
    else {
      spdlog::trace("receive_event_callback: Using CanalSend to send event.");
      if (CANAL_ERROR_SUCCESS != (rv = pDeviceItem->m_proc_CanalSend(pDeviceItem->m_openHandle, &msg))) {
        spdlog::error("driver: {}: receive_event_callback - Failed to send event (m_proc_CanalSend) rv={1}",
                                     pDeviceItem->m_strName.c_str(),
                                     rv);
      }
    }
  }
  else if (VSCP_DRIVER_LEVEL2 == pDeviceItem->m_driverLevel) {
    if (CANAL_ERROR_SUCCESS != pDeviceItem->m_proc_VSCPWrite(pDeviceItem->m_openHandle, &ev, 500)) {
      spdlog::error("driver: receive_event_callback - Failed to send level II event.");
    }
  }
  else {
    spdlog::error("driver: receive_event_callback - Driver level is not valid (nor 1 nor 2).");
  }

  pthread_mutex_unlock(&pDeviceItem->m_deviceMutex);
}



// ----------------------------------------------------------------------------



#ifdef WIN32
static void
usleep(__int64 usec)
{
  HANDLE timer;
  LARGE_INTEGER ft;

  ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

  timer = CreateWaitableTimer(NULL, TRUE, NULL);
  SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
  WaitForSingleObject(timer, INFINITE);
  CloseHandle(timer);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// deviceThread
//
// New behaviour
// =============
// subscribe to MQTT channel topic_subscribe
// publish to MQTT channel topic_publish
//

void *
deviceThread(void *pData)
{
  CDeviceItem *pDeviceItem = (CDeviceItem *) pData;
  if (nullptr == pDeviceItem) {
    spdlog::error("No device item defined. Aborting device thread!");
    return NULL;
  }

  void *hdll;

  // Load dynamic library
  spdlog::debug("Loading (dlopen) dynamic library: {}", pDeviceItem->m_strPath);
  hdll = dlopen(pDeviceItem->m_strPath.c_str(), RTLD_LAZY);
  if (!hdll) {
    spdlog::error("Devicethread: Unable to load dynamic library. path = {}  {}",
                                 pDeviceItem->m_strPath,
                                 dlerror());
    return NULL;
  }

  spdlog::debug("Dynamic library loaded successfully.");

  //*************************************************************************
  //                         Level I drivers
  //*************************************************************************
  if (VSCP_DRIVER_LEVEL1 == pDeviceItem->m_driverLevel) {

    // Now find methods in library
    spdlog::debug("Devicethread: Loading level I driver: {}", pDeviceItem->m_strName);

    // * * * * CANAL OPEN * * * *
    spdlog::trace("Devicethread: Loading entry for CanalOpen from dynamic library.");
    pDeviceItem->m_proc_CanalOpen = (LPFNDLL_CANALOPEN) dlsym(hdll, "CanalOpen");
    const char *dlsym_error       = dlerror();

    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {} : Unable to get dl entry for CanalOpen.", pDeviceItem->m_strName);
      return NULL;
    }

    // * * * * CANAL CLOSE * * * *
    spdlog::trace("Devicethread: Loading entry for CanalClose from dynamic library.");
    pDeviceItem->m_proc_CanalClose = (LPFNDLL_CANALCLOSE) dlsym(hdll, "CanalClose");
    dlsym_error                    = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalClose.", pDeviceItem->m_strName);
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GETLEVEL * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetLevel from dynamic library.");
    pDeviceItem->m_proc_CanalGetLevel = (LPFNDLL_CANALGETLEVEL) dlsym(hdll, "CanalGetLevel");
    dlsym_error                       = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetLevel.", pDeviceItem->m_strName);
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL SEND * * * *
    spdlog::trace("Devicethread: Loading entry for CanalSend from dynamic library.");
    pDeviceItem->m_proc_CanalSend = (LPFNDLL_CANALSEND) dlsym(hdll, "CanalSend");
    dlsym_error                   = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalSend.", pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL DATA AVAILABLE * * * *
    spdlog::trace("Devicethread: Loading entry for CanalDataAvailable from dynamic library.");
    pDeviceItem->m_proc_CanalDataAvailable = (LPFNDLL_CANALDATAAVAILABLE) dlsym(hdll, "CanalDataAvailable");
    dlsym_error                            = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalDataAvailable.",
                                      pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL RECEIVE * * * *
    spdlog::trace("Devicethread: Loading entry for CanalReceive from dynamic library.");
    pDeviceItem->m_proc_CanalReceive = (LPFNDLL_CANALRECEIVE) dlsym(hdll, "CanalReceive");
    dlsym_error                      = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalReceive.", pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GET STATUS * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetStatus from dynamic library.");
    pDeviceItem->m_proc_CanalGetStatus = (LPFNDLL_CANALGETSTATUS) dlsym(hdll, "CanalGetStatus");
    dlsym_error                        = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetStatus.", pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GET STATISTICS * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetStatistics from dynamic library.");
    pDeviceItem->m_proc_CanalGetStatistics = (LPFNDLL_CANALGETSTATISTICS) dlsym(hdll, "CanalGetStatistics");
    dlsym_error                            = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetStatistics.",
                                      pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL SET FILTER * * * *
    spdlog::trace("Devicethread: Loading entry for CanalSetFilter from dynamic library.");
    pDeviceItem->m_proc_CanalSetFilter = (LPFNDLL_CANALSETFILTER) dlsym(hdll, "CanalSetFilter");
    dlsym_error                        = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalSetFilter.", pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL SET MASK * * * *
    spdlog::trace("Devicethread: Loading entry for CanalSetMask from dynamic library.");
    pDeviceItem->m_proc_CanalSetMask = (LPFNDLL_CANALSETMASK) dlsym(hdll, "CanalSetMask");
    dlsym_error                      = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalSetMask.", pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GET VERSION * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetVersion from dynamic library.");
    pDeviceItem->m_proc_CanalGetVersion = (LPFNDLL_CANALGETVERSION) dlsym(hdll, "CanalGetVersion");
    dlsym_error                         = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetVersion.",
                                      pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GET DLL VERSION * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetDllVersion from dynamic library.");
    pDeviceItem->m_proc_CanalGetDllVersion = (LPFNDLL_CANALGETDLLVERSION) dlsym(hdll, "CanalGetDllVersion");
    dlsym_error                            = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetDllVersion.",
                                      pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // * * * * CANAL GET VENDOR STRING * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetVendorString from dynamic library.");
    pDeviceItem->m_proc_CanalGetVendorString = (LPFNDLL_CANALGETVENDORSTRING) dlsym(hdll, "CanalGetVendorString");
    dlsym_error                              = dlerror();
    if (dlsym_error) {
      // Free the library
      spdlog::critical("Devicethread: {}: Unable to get dl entry for CanalGetVendorString.",
                                      pDeviceItem->m_strName.c_str());
      dlclose(hdll);
      return NULL;
    }

    // ******************************
    //     Generation 2 Methods
    // ******************************

    // * * * * CANAL BLOCKING SEND * * * *
    spdlog::trace("Devicethread: Loading entry for CanalBlockingSend from dynamic library.");
    pDeviceItem->m_proc_CanalBlockingSend = (LPFNDLL_CANALBLOCKINGSEND) dlsym(hdll, "CanalBlockingSend");
    dlsym_error                           = dlerror();
    if (dlsym_error) {
      spdlog::error("Devicethread: {}: Unable to get dl entry for CanalBlockingSend. Probably "
                                   "Generation 1 driver.",
                                   pDeviceItem->m_strName.c_str());
      pDeviceItem->m_proc_CanalBlockingSend = NULL;
    }

    // * * * * CANAL BLOCKING RECEIVE * * * *
    spdlog::trace("Devicethread: Loading entry for CanalBlockingReceive from dynamic library.");
    pDeviceItem->m_proc_CanalBlockingReceive = (LPFNDLL_CANALBLOCKINGRECEIVE) dlsym(hdll, "CanalBlockingReceive");
    dlsym_error                              = dlerror();
    if (dlsym_error) {
      spdlog::error("Devicethread: {}: Unable to get dl entry for CanalBlockingReceive. "
                                   "Probably Generation 1 driver.",
                                   pDeviceItem->m_strName.c_str());
      pDeviceItem->m_proc_CanalBlockingReceive = NULL;
    }

    // * * * * CANAL GET DRIVER INFO * * * *
    spdlog::trace("Devicethread: Loading entry for CanalGetDriverInfo from dynamic library.");
    pDeviceItem->m_proc_CanalGetdriverInfo = (LPFNDLL_CANALGETDRIVERINFO) dlsym(hdll, "CanalGetDriverInfo");
    dlsym_error                            = dlerror();
    if (dlsym_error) {
      spdlog::error("Devicethread: {}: Unable to get dl entry for CanalGetDriverInfo. "
                                   "Probably Generation 1 driver.",
                                   pDeviceItem->m_strName.c_str());
      pDeviceItem->m_proc_CanalGetdriverInfo = NULL;
    }

    // Open the device
    spdlog::trace("Devicethread: Opening device using CanalOpen.");
    pDeviceItem->m_openHandle =
      pDeviceItem->m_proc_CanalOpen((const char *) pDeviceItem->m_strParameter.c_str(), pDeviceItem->m_DeviceFlags);

    // Check if the driver opened properly
    if (pDeviceItem->m_openHandle <= 0) {
      spdlog::error("Devicethread: Failed to open driver. Will not use it! {} [{}] ",
                                   pDeviceItem->m_openHandle,
                                   pDeviceItem->m_strName);
      dlclose(hdll);
      return NULL;
    }

    spdlog::debug("Devicethread: {}: [Device tread] Level I Driver open.", pDeviceItem->m_strName.c_str());

    //--------------------------------------------------------------
    //                       MQTT Level I
    // -------------------------------------------------------------

    // Set interface/driver GUID
    pDeviceItem->m_mqttClient.setIfGuid(pDeviceItem->m_guid);
    spdlog::trace("Devicethread: Setting interface GUID for MQTT client. GUID={}", pDeviceItem->m_guid);

    // Set server GUID
    pDeviceItem->m_mqttClient.setSrvGuid(pDeviceItem->m_pCtrlObj->m_guid);
    spdlog::trace("Devicethread: Setting server GUID for MQTT client. GUID={}", pDeviceItem->m_pCtrlObj->m_guid);

    // Add user escapes
    pDeviceItem->m_mqttClient.setUserEscape("driver-name", pDeviceItem->m_strName);
    spdlog::trace("Devicethread: Setting user escape for driver name. Name={}", pDeviceItem->m_strName);

    // Driver level
    pDeviceItem->m_mqttClient.setUserEscape("driver-level",
                                            (VSCP_DRIVER_LEVEL1 == pDeviceItem->m_driverLevel) ? "level1" : "level2");
    spdlog::trace("Devicethread: Setting user escape for driver level. Level={}", (VSCP_DRIVER_LEVEL1 == pDeviceItem->m_driverLevel) ? "level1" : "level2");

    // Add class/type tokens
    pDeviceItem->m_mqttClient.setTokenMaps(&pDeviceItem->m_pCtrlObj->m_map_class_id2Token,
                                           &pDeviceItem->m_pCtrlObj->m_map_type_id2Token);
    spdlog::trace("Devicethread: Setting token maps for MQTT client. Class map size={}, Type map size={}", pDeviceItem->m_pCtrlObj->m_map_class_id2Token.size(), pDeviceItem->m_pCtrlObj->m_map_type_id2Token.size());

    // Set event callback - level I
    pDeviceItem->m_mqttClient.setCallbackEv(receive_event_callback, pDeviceItem);
    spdlog::trace("Devicethread: Setting event callback for MQTT client.");

    // Connect to server
    spdlog::trace("Devicethread: Connecting to MQTT broker for level I driver.");
    if (VSCP_ERROR_SUCCESS != pDeviceItem->m_mqttClient.connect()) {
      spdlog::error("Devicethread: Failed to connect to MQTT client level I driver.");
      dlclose(hdll);
      return NULL;
    }

    // -------------------------------------------------------------

    bool bActivity;

    // Get Driver Level
    pDeviceItem->m_driverLevel = (uint8_t) pDeviceItem->m_proc_CanalGetLevel(pDeviceItem->m_openHandle);

    //  * * * Level I Driver * * *

    // Check if blocking driver is available
    spdlog::trace("Devicethread: Checking if blocking driver is available.");
    if (NULL != pDeviceItem->m_proc_CanalBlockingReceive) {

      // * * * * Blocking version * * * *

      spdlog::debug("{}: [Device tread] Level I blocking version.", pDeviceItem->m_strName.c_str());

      /////////////////////////////////////////////////////////////////////////////
      //                      Device write worker thread
      /////////////////////////////////////////////////////////////////////////////

      // Wait for events or "the end"
      spdlog::debug("Devicethread: Entering main loop for blocking driver (level I).");
      while (!pDeviceItem->m_bQuit) {

        canalMsg msg;
        vscpEvent ev;

        // Get an CANAL event - blocking
        spdlog::trace("Devicethread: Waiting for CANAL message (blocking).");
        if (CANAL_ERROR_SUCCESS == pDeviceItem->m_proc_CanalBlockingReceive(pDeviceItem->m_openHandle, &msg, 50)) {

          // Publish to MQTT broker

          memset(&ev, 0, sizeof(vscpEvent));

          // Set driver GUID
          pDeviceItem->m_guid.writeGUID(ev.GUID);

          // Convert CANAL message to VSCP event
          if (!vscp_convertCanalToEvent(&ev, &msg, (unsigned char *) pDeviceItem->m_guid.getGUID())) {
            spdlog::error("Driver L1: {} Failed to convert CANAL to event.", pDeviceItem->m_strName);
            continue;
          }
          ev.obid     = 0;
          ev.GUID[14] = 0; // Make sure MSB of nickname is zero for Level I driver

          // =========================================================
          //                   Outgoing translations
          // =========================================================

          // Level I measurement events to Level II measurement float
          if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_M1_M2F) {
            spdlog::trace("Devicethread: Converting Level I measurement to Level II float.");
            vscp_convertLevel1MeasurementToLevel2Double(&ev);
          }

          // Level I measurement events to Level II measurement string
          if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_M1_M2S) {
            spdlog::trace("Devicethread: Converting Level I measurement to Level II string.");
            vscp_convertLevel1MeasurementToLevel2String(&ev);
          }

          // Level I events to Level I over Level II events
          if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_ALL_L2) {
            spdlog::trace("Devicethread: Converting Level I event to Level I over Level II event.");
            ev.vscp_class += 512;
            uint8_t *p = new uint8_t[16 + ev.sizeData];
            if (NULL != p) {
              memset(p, 0, 16 + ev.sizeData);
              memcpy(p + 16, ev.pdata, ev.sizeData);
              ev.sizeData += 16;
              delete[] ev.pdata;
              ev.pdata = p;
            }
          }

          spdlog::trace("Devicethread: Sending event to broker. {}", pDeviceItem->m_strName);
          if (!pDeviceItem->sendEvent(&ev)) {
            spdlog::error("Driver L1: {} Failed to send event to broker.", pDeviceItem->m_strName);
            vscp_deleteEvent(&ev);
            continue;
          }

          vscp_deleteEvent(&ev);
        }
      } // while

      // Signal worker threads to quit
      pDeviceItem->m_bQuit = true;

      spdlog::info("{}: [Device tread] Level I work loop ended.", pDeviceItem->m_strName);
    }
    else {

      // * * * * Non blocking version * * * *

      spdlog::info("{}: [Device tread] Entering main loop for non blocking driver (level I)", pDeviceItem->m_strName);
      while (!pDeviceItem->m_bQuit) {

        bActivity = false;

        /////////////////////////////////////////////////////////////////////////////
        //                           Receive from device
        /////////////////////////////////////////////////////////////////////////////

        canalMsg msg;
        spdlog::trace("Devicethread: Check if data is available. m_proc_CanalDataAvailable.");
        if (pDeviceItem->m_proc_CanalDataAvailable(pDeviceItem->m_openHandle)) {

          spdlog::trace("Devicethread: Data is available, attempting to receive CANAL message.");
          if (CANAL_ERROR_SUCCESS == pDeviceItem->m_proc_CanalReceive(pDeviceItem->m_openHandle, &msg)) {

            spdlog::trace("Devicethread: Successfully received CANAL message.");

            bActivity = true;

            // Publish to MQTT broker
            vscpEvent *pev = new vscpEvent;
            if (NULL != pev) {

              memset(pev, 0, sizeof(vscpEvent));
              
              // Set new frame version to UNIX_NS
              pev->head = (pev->head & ~VSCP_HEADER16_FRAME_VERSION_MASK) | VSCP_HEADER16_FRAME_VERSION_UNIX_NS;

              // Convert CANAL message to VSCP event
              spdlog::trace("Devicethread: Converting CANAL message to VSCP event.");
              if (!vscp_convertCanalToEvent(pev, &msg, (unsigned char *) pDeviceItem->m_guid.getGUID())) {
                spdlog::error("Driver L1: {} Failed to convert CANAL to event.", pDeviceItem->m_strName);
                spdlog::trace("Driver L1: {} CANAL msg: id={:X} flags={:X} obid={:X} timestamp={} sizeData={} data=[{:n}]",
                              pDeviceItem->m_strName,
                              msg.id,
                              msg.flags,
                              msg.obid,
                              msg.timestamp,
                              msg.sizeData,
                              spdlog::to_hex(msg.data, msg.data + std::min<uint8_t>(msg.sizeData, 8)));
                vscp_deleteEvent_v2(&pev);
                continue;
              }
              pev->obid = 0;
              // pev->GUID[14] = 0;   // Make sure high byte of nickname is zero for Level I driver

              // =========================================================
              //                   Outgoing translations
              // =========================================================

              // Level I measurement events to Level II measurement float
              if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_M1_M2F) {
                spdlog::trace("Devicethread: Converting Level I measurement to Level II float.");
                vscp_convertLevel1MeasurementToLevel2Double(pev);
              }

              // Level I measurement events to Level II measurement string
              if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_M1_M2S) {
                spdlog::trace("Devicethread: Converting Level I measurement to Level II string.");
                vscp_convertLevel1MeasurementToLevel2String(pev);
              }

              // Level I events to Level I over Level II events
              if (pDeviceItem->m_translation & VSCP_DRIVER_OUT_TR_ALL_L2) {
                spdlog::trace("Devicethread: Converting Level I event to Level I over Level II event.");
                pev->vscp_class += 512;
                uint8_t *p = new uint8_t[16 + pev->sizeData];
                if (NULL != p) {
                  memset(p, 0, 16 + pev->sizeData);
                  memcpy(p + 16, pev->pdata, pev->sizeData);
                  pev->sizeData += 16;
                  delete[] pev->pdata;
                  pev->pdata = p;
                }
              }

              spdlog::trace("Devicethread: Sending event to broker.");
              if (!pDeviceItem->sendEvent(pev)) {
                spdlog::error("Driver L1: {} Failed to send event to broker.", pDeviceItem->m_strName);
                vscp_deleteEvent_v2(&pev);
                continue;
              }

              vscp_deleteEvent_v2(&pev);
            }
            else {
              spdlog::error("Driver L1: {} Memory problem.\n", pDeviceItem->m_strName);
              continue;
            }
          }
        } // data available

        if (!bActivity) {
          spdlog::trace("Devicethread: No activity, sleeping for 100 ms.");cd
          usleep(100000); // 100 ms
        }

        bActivity = false;

      } // while working - non blocking

    } // if blocking/non blocking

    spdlog::info("{}: [Device tread] Level I Work loop ended.", pDeviceItem->m_strName);

    // Stop MQTT callbacks before closing the driver
    pDeviceItem->m_bQuit = true;
    pDeviceItem->m_mqttClient.disconnect();

    // Close CANAL channel
    spdlog::trace("{}: [Device tread] Closing CANAL channel.", pDeviceItem->m_strName);
    pthread_mutex_lock(&pDeviceItem->m_deviceMutex);
    pDeviceItem->m_proc_CanalClose(pDeviceItem->m_openHandle);
    pDeviceItem->m_openHandle = 0;
    pthread_mutex_unlock(&pDeviceItem->m_deviceMutex);
    spdlog::trace("{}: [Device tread] CANAL channel closed.", pDeviceItem->m_strName);

    spdlog::debug("{}: [Device tread] Level I Closed.", pDeviceItem->m_strName);

    dlclose(hdll);
  }

  //*************************************************************************
  //                         Level II drivers
  //*************************************************************************

  else if (VSCP_DRIVER_LEVEL2 == pDeviceItem->m_driverLevel) {

    // Now find methods in library
    spdlog::info("Loading level II driver: <{}>", pDeviceItem->m_strName);

    // * * * * VSCP OPEN * * * *
    spdlog::trace("{}: [Device tread] Loading level II driver.", pDeviceItem->m_strName);
    if (nullptr == (pDeviceItem->m_proc_VSCPOpen = (LPFNDLL_VSCPOPEN) dlsym(hdll, "VSCPOpen"))) {
      // Free the library
      spdlog::error("{}: Unable to get dl entry for VSCPOpen.", pDeviceItem->m_strName);
      return NULL;
    }

    // * * * * VSCP CLOSE * * * *
    spdlog::trace("{}: [Device tread] Loading VSCPClose method.", pDeviceItem->m_strName);
    if (nullptr == (pDeviceItem->m_proc_VSCPClose = (LPFNDLL_VSCPCLOSE) dlsym(hdll, "VSCPClose"))) {
      // Free the library
      spdlog::error("{}: Unable to get dl entry for VSCPClose.", pDeviceItem->m_strName);
      return NULL;
    }

    // * * * * VSCPWRITE * * * *
    spdlog::trace("{}: [Device tread] Loading VSCPWrite method.", pDeviceItem->m_strName);
    if (nullptr == (pDeviceItem->m_proc_VSCPWrite = (LPFNDLL_VSCPWRITE) dlsym(hdll, "VSCPWrite"))) {
      // Free the library
      spdlog::error("{}: Unable to get dl entry for VSCPWrite.", pDeviceItem->m_strName);
      return NULL;
    }

    // * * * * VSCPREAD * * * *
    spdlog::trace("{}: [Device tread] Loading VSCPRead method.", pDeviceItem->m_strName);
    if (nullptr == (pDeviceItem->m_proc_VSCPRead = (LPFNDLL_VSCPREAD) dlsym(hdll, "VSCPRead"))) {
      // Free the library
      spdlog::error("{}: Unable to get dl entry for VSCPBlockingReceive.", pDeviceItem->m_strName);
      return NULL;
    }

    // * * * * VSCP GET VERSION * * * *
    spdlog::trace("{}: [Device tread] Loading VSCPGetVersion method.", pDeviceItem->m_strName);
    if (nullptr == (pDeviceItem->m_proc_VSCPGetVersion = (LPFNDLL_VSCPGETVERSION) dlsym(hdll, "VSCPGetVersion"))) {
      // Free the library
      spdlog::error("{}: Unable to get dl entry for VSCPGetVersion.", pDeviceItem->m_strName);
      return NULL;
    }

    spdlog::debug("{}: Discovered all methods\n", pDeviceItem->m_strName);

    //--------------------------------------------------------------
    //                        MQTT Level II
    // -------------------------------------------------------------

    // Set driver/interface GUID
    spdlog::trace("{}: [Device tread] Setting up MQTT client with interface GUID.", pDeviceItem->m_strName);
    pDeviceItem->m_mqttClient.setIfGuid(pDeviceItem->m_guid);

    // Set server GUID
    spdlog::trace("{}: [Device tread] Setting up MQTT client with server GUID.", pDeviceItem->m_strName);
    pDeviceItem->m_mqttClient.setSrvGuid(pDeviceItem->m_pCtrlObj->m_guid);

    // Add user escapes
    spdlog::trace("{}: [Device tread] Setting MQTT client user escape for driver name.", pDeviceItem->m_strName);
    pDeviceItem->m_mqttClient.setUserEscape("driver-name", pDeviceItem->m_strName);

    // Driver level
    spdlog::trace("{}: [Device tread] Setting MQTT client user escape for driver level.", pDeviceItem->m_strName);
    pDeviceItem->m_mqttClient.setUserEscape("driver-level",
                                            (VSCP_DRIVER_LEVEL1 == pDeviceItem->m_driverLevel) ? "level1" : "level2");

    // Add class/type tokens
    spdlog::trace("{}: [Device tread] Setting MQTT client class/type tokens.", pDeviceItem->m_strName);
    pDeviceItem->m_mqttClient.setTokenMaps(&pDeviceItem->m_pCtrlObj->m_map_class_id2Token,
                                           &pDeviceItem->m_pCtrlObj->m_map_type_id2Token);

    // Set event callback - level II
    spdlog::trace("{}: [Device tread] Setting MQTT client event callback for level II.", pDeviceItem->m_strName);
    void *pParent = (void *) pDeviceItem;
    pDeviceItem->m_mqttClient.setCallbackEv(receive_event_callback, pParent);

    // -------------------------------------------------------------

    // Open up the L2 driver
    spdlog::trace("{}: [Device tread] Opening level II driver.", pDeviceItem->m_strName);
    pDeviceItem->m_openHandle =
      pDeviceItem->m_proc_VSCPOpen(pDeviceItem->m_strParameter.c_str(), pDeviceItem->m_guid.getGUID());

    if (0 == pDeviceItem->m_openHandle) {
      // Free the library
      spdlog::error("{}: [Device tread] Unable to open VSCP "
                                   " level II driver (path, config file access rights)."
                                   " There may be additional info from driver "
                                   "in log. If not enable debug flag in drivers config file",
                                   pDeviceItem->m_strName);
      dlclose(hdll);
      return NULL;
    }

    spdlog::trace("{}: [Device tread] Level II driver opened successfully.", pDeviceItem->m_strName);

    // Connect to server - after open so the receive callback
    // never sees an invalid driver handle
    spdlog::trace("{}: [Device tread] Connecting to MQTT client for level II driver.", pDeviceItem->m_strName);
    if (VSCP_ERROR_SUCCESS != pDeviceItem->m_mqttClient.connect()) {
      spdlog::error("{}: [Device tread] Failed to connect to MQTT client for level II driver.", pDeviceItem->m_strName);
      pDeviceItem->m_proc_VSCPClose(pDeviceItem->m_openHandle);
      pDeviceItem->m_openHandle = 0;
      dlclose(hdll);
      return NULL;
    }

    // --------------------------------------------------------------------
    //        Work loop L2 - receive from device - send to MQTT broker
    // --------------------------------------------------------------------

    // Just sit and wait until the end of the world as we know it...
    spdlog::trace("{}: [Device tread] Entering work loop for level II driver.", pDeviceItem->m_strName);
    while (!pDeviceItem->m_bQuit) {

      vscpEvent ev;
      memset(&ev, 0, sizeof(vscpEvent));

      spdlog::trace("{}: [Device tread] Waiting for event from level II driver.", pDeviceItem->m_strName);
      if (CANAL_ERROR_SUCCESS != pDeviceItem->m_proc_VSCPRead(pDeviceItem->m_openHandle, &ev, 50)) {
        continue;
      }

      // Original frame: convert a valid datetime block to timestamp_ns,
      // otherwise stamp with current time
      if (VSCP_HEADER16_FRAME_VERSION_UNIX_NS != (ev.head & VSCP_HEADER16_FRAME_VERSION_MASK)) {
        int64_t ns = -1;
        if ((ev.year > 0) && (ev.year < 0xffff) && (ev.month >= 1) && (ev.month <= 12) && (ev.day >= 1) &&
            (ev.day <= 31) && (ev.hour <= 23) && (ev.minute <= 59) && (ev.second <= 59)) {
          ns = vscp_to_unix_ns(ev.year, ev.month, ev.day, ev.hour, ev.minute, ev.second, ev.timestamp);
        }
        ev.head         = (ev.head & ~VSCP_HEADER16_FRAME_VERSION_MASK) | VSCP_HEADER16_FRAME_VERSION_UNIX_NS;
        ev.year         = 0xffff;
        ev.month        = 0xff;
        ev.timestamp_ns = (ns > 0) ? (uint64_t) ns : vscp_makeTimeStampNs();
      }
      else if (0 == ev.timestamp_ns) {
        ev.timestamp_ns = vscp_makeTimeStampNs();
      }

      // Publish to MQTT broker

      if (!pDeviceItem->sendEvent(&ev)) {
        spdlog::error("{}: [Device tread] Failed to send event to broker.", pDeviceItem->m_strName.c_str());
        vscp_deleteEvent(&ev);
        continue;
      }

      vscp_deleteEvent(&ev);
    }

    spdlog::debug("{}: [Device tread] Level II Closing.", pDeviceItem->m_strName);

    // Stop MQTT callbacks before closing the driver
    pDeviceItem->m_bQuit = true;
    pDeviceItem->m_mqttClient.disconnect();

    // Close channel
    spdlog::trace("{}: [Device tread] Closing level II driver.", pDeviceItem->m_strName);
    pthread_mutex_lock(&pDeviceItem->m_deviceMutex);
    pDeviceItem->m_proc_VSCPClose(pDeviceItem->m_openHandle);
    pDeviceItem->m_openHandle = 0;
    pthread_mutex_unlock(&pDeviceItem->m_deviceMutex);
    spdlog::trace("{}: [Device tread] Level II driver closed.", pDeviceItem->m_strName);

    spdlog::info("{}: [Device tread] Level II Closed.", pDeviceItem->m_strName);

    // Unload dll
    dlclose(hdll);

    spdlog::debug("{}: [Device tread] Level II Done waiting for threads.",
                                   pDeviceItem->m_strName.c_str());
  }

  return NULL;
}
