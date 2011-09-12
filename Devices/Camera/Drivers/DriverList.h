// Single inclusion point for drivers. In the future this file will likely be
// automatically generated by CMake.

#ifndef _CAMERA_DRIVER_LIST_H_
#define _CAMERA_DRIVER_LIST_H_

///////////////////////////////////////////////////////////////////////////////
// Global map of driver name to driver creation function pointers:
std::map<std::string,CameraDriver*(*)()> g_mDriverTable;

#include "RPG/Devices/Camera/Drivers/FileReader/FileReaderDriver.h"
CameraDriverRegisteryEntry<FileReaderDriver> _FileReaderReg( "FileReader" );

#ifdef USE_BUMBLEBEE_2
#include "RPG/Devices/Camera/Drivers/Bumblebee2/Bumblebee2Driver.h"
CameraDriverRegisteryEntry<Bumblebee2Driver> _Bumblebee2Reg( "Bumblebee2" );
#endif

#endif

