/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Register WebServer SysMods
// Register the WebServer SysMod with the SysManager
//
// Rob Dobson 2018-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "sdkconfig.h"
#include "WebServer.h"

// Check if networking is enabled
#if defined(CONFIG_ESP_WIFI_ENABLED) || defined(CONFIG_ETH_USE_ESP32_EMAC) || defined(CONFIG_ETH_USE_SPI_ETHERNET) || defined(CONFIG_ETH_USE_OPENETH) || defined(CONFIG_ETH_USE_RMII_ETHERNET)
#define NETWORKING_IS_ENABLED
#endif

namespace RegisterSysMods
{
    void registerWebServer(SysManager& sysManager)
    {
        // WebServer
#ifdef NETWORKING_IS_ENABLED
        sysManager.registerSysMod("WebServer", WebServer::create, false, "NetMan");
#endif
    }
}

