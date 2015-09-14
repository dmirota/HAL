#include "DeinterlaceDriver.h"

#include <iostream>

namespace hal
{

DeinterlaceDriver::DeinterlaceDriver(
    std::shared_ptr<CameraDriverInterface> input_cam, 
    const Uri& uri
  )
    : m_Input(input_cam),
      m_nImgWidth(input_cam->Width()),
      m_nImgHeight(input_cam->Height())
{
  // sat sane default parameters
//  CameraDriverInterface::SetDefaultProperties({   });
  if( !CameraDriverInterface::ParseUriProperties( uri.properties ) ){
    std::cerr << "DeinterlaceDriver knows about the following properties:\n";
    CameraDriverInterface::PrintPropertyMap();
    return;
  }
  m_Buffer = (unsigned char*)malloc(m_nImgHeight*m_nImgWidth*2);
}

DeinterlaceDriver::~DeinterlaceDriver()
{
  free(m_Buffer);
}

bool DeinterlaceDriver::Capture( hal::CameraMsg& vImages )
{
    m_Message.Clear();
    m_Input->Capture( m_Message );

    if( m_Message.mutable_image(0)->type() != hal::PB_UNSIGNED_SHORT ) {
      std::cerr << "HAL: Error! Expecting image with depth of 16 bits." << std::endl;
      return false;
    }

    vImages.set_device_time( m_Message.device_time() );

    dc1394_deinterlace_stereo((uint8_t*)m_Message.mutable_image(0)->data().data(),
                              (uint8_t*)m_Buffer, m_nImgWidth*2, m_nImgHeight);

    const unsigned int nImgSize = m_nImgWidth * m_nImgHeight;

    hal::ImageMsg* pbImg = vImages.add_image();
    pbImg->set_width( m_nImgWidth );
    pbImg->set_height( m_nImgHeight );
    pbImg->set_data( m_Buffer, nImgSize );
    pbImg->set_type( hal::PB_UNSIGNED_BYTE );
    pbImg->set_format( m_Message.mutable_image(0)->format() );

    pbImg = vImages.add_image();
    pbImg->set_width( m_nImgWidth );
    pbImg->set_height( m_nImgHeight );
    pbImg->set_data( m_Buffer+nImgSize, nImgSize);
    pbImg->set_type( hal::PB_UNSIGNED_BYTE );
    pbImg->set_format( m_Message.mutable_image(0)->format() );

    return true;
}

size_t DeinterlaceDriver::NumChannels() const
{
    return 2;
}

size_t DeinterlaceDriver::Width( size_t /*idx*/ ) const
{
    return m_nImgWidth;
}

size_t DeinterlaceDriver::Height( size_t /*idx*/ ) const
{
    return m_nImgHeight;
}

} // namespace
