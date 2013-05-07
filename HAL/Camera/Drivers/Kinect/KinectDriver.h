/*
   \file KinectDriver.h

 */

#ifndef _KINECT_H_
#define _KINECT_H_

#include "HAL/Camera/CameraDriverInterface.h"

#include <XnCppWrapper.h>

class KinectDriver : public CameraDriver
{
    public:
        KinectDriver();
        virtual ~KinectDriver();
        bool Capture( std::vector<rpg::ImageWrapper>& vImages );
        void PrintInfo();
        bool Init();
    private:
        unsigned int            m_nImgHeight;
        unsigned int            m_nImgWidth;
        bool                    m_bForceGreyscale;
        xn::Context             m_Context;
        std::vector<xn::DepthGenerator> m_DepthGenerators;
        std::vector<xn::ImageGenerator> m_ImageGenerators;
        std::vector<xn::IRGenerator>    m_IRGenerators;
};

#endif