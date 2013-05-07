
#include "FileReaderDriver.h"

#include <Mvlpp/Utils.h>  // for FindFiles and PrintError
#include <boost/format.hpp>

#include <HAL/VirtualDevice.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////
FileReaderDriver::FileReaderDriver(){}

///////////////////////////////////////////////////////////////////////////////
FileReaderDriver::~FileReaderDriver()
{
    if( m_CaptureThread ) {
        m_CaptureThread->interrupt();
        m_CaptureThread->join();
    }
}

///////////////////////////////////////////////////////////////////////////////
// Consumer
bool FileReaderDriver::Capture( std::vector<rpg::ImageWrapper>& vImages )
{
    if( m_nCurrentImageIndex >= m_nNumImages && m_qImageBuffer.size() == 0 && !m_bLoop) {
        return false;
    }

    boost::mutex::scoped_lock lock(m_Mutex);

    // Wait until the buffer has data to read
    while(m_qImageBuffer.size() == 0){
        m_cBufferEmpty.wait(lock);
    }

    VirtualDevice::WaitForTime( _GetNextTime() );

    //***************************************************
    // consume from buffer
    //***************************************************

    // allocate images if necessary
    if( vImages.size() != m_nNumChannels )
        vImages.resize( m_nNumChannels );

    // now fetch the next set of images from buffer
    for( unsigned int ii = 0; ii < m_nNumChannels; ii++ ) {
        vImages[ii] = m_qImageBuffer.front()[ii].clone();
    }

    // remove image from buffer queue
    m_qImageBuffer.pop();

    // send notification that the buffer has space
    m_cBufferFull.notify_one();

    // push next timestamp to queue now that we popped from the buffer
    VirtualDevice::PopAndPushTime( _GetNextTime() );

    return true;
}


///////////////////////////////////////////////////////////////////////////////
void FileReaderDriver::PrintInfo() {

    std::cout <<
    "\nFILEREADER\n"
    "--------------------------------\n"
    "Reads images from the disk."
    "\n\n"
    "Options:\n"
    "   -sdir           <source directory for images and camera model files> [default '.']\n"
    "   -chanN          <regular expression for channel N>\n"
    "   -sf             <start frame> [default 0]\n"
    "   -buffsize       <size of buffer for image pre-read> [default 35]\n"
    "   -timekeeper     <name of variable holding image timestamps> [default 'SystemTime']\n"
    "\n"
    "Note:\n"
    "   -lfile & -rfile can be used as aliases to Channel-0 & Channel-1.\n"
    "\n"
    "Flags:\n"
    "   -greyscale      If the driver should return images in greyscale.\n"
    "   -loop           If the driver should restart once images are consumed.\n"
    "\n"
    "Examples:\n"
    "./Exec  -idev FileReader  -lfile \"left.*pgm\"  -rfile \"right.*pgm\"\n"
    "./Exec  -idev FileReader  -chan0 \"left.*pgm\"  -chan1 \"right.*pgm\"   -chan2 \"depth.*pdm\"\n\n";
}

///////////////////////////////////////////////////////////////////////////////
bool FileReaderDriver::Init()
{
    // clear variables if previously initialized
    m_vFileList.clear();


    m_nBufferSize        = m_pPropertyMap->GetProperty<unsigned int>( "BufferSize", 35 );
    m_nStartFrame        = m_pPropertyMap->GetProperty<unsigned int>( "StartFrame",  0 );
    m_bLoop              = m_pPropertyMap->GetProperty<bool>( "Loop",  false );
    m_nCurrentImageIndex = m_nStartFrame;
    m_iCvImageReadFlags  = m_pPropertyMap->GetProperty<bool>( "ForceGreyscale",  false )
            ? cv::IMREAD_GRAYSCALE : cv::IMREAD_UNCHANGED;
    m_sTimeKeeper = m_pPropertyMap->GetProperty<std::string>( "TimeKeeper",  "SystemTime" );

    m_nNumChannels       = m_pPropertyMap->GetProperty<unsigned int>( "NumChannels", 0 );

    if(m_nNumChannels < 1) {
        mvl::PrintError( "ERROR: No channels specified. Set property NumChannels.\n" );
        return false;
    }

    m_vFileList.resize( m_nNumChannels );

    // Get data path
    std::string sChannelPath = m_pPropertyMap->GetProperty( "DataSourceDir", "");

    for( unsigned int ii = 0; ii < m_nNumChannels; ii++ ) {
        std::string sChannelName  = (boost::format("Channel-%d")%ii).str();
        std::string sChannelRegex = m_pPropertyMap->GetProperty( sChannelName, "");

        // Split channel regex into directory and file components
        size_t pos = sChannelRegex.rfind("/");
        std::string sSubDirectory;

        if(pos != string::npos)
        {
            sSubDirectory = sChannelRegex.substr(0,pos);
            sChannelRegex = sChannelRegex.substr(pos+1);
        }

        // Now generate the list of files for each channel
        std::vector< std::string >& vFiles = m_vFileList[ii];

        if(mvl::FindFiles(sChannelPath + "/" + sSubDirectory, sChannelRegex, vFiles) == false){
        //if( mvl::FindFiles( sChannelRegex, vFiles ) == false ) {
            mvl::PrintError( "ERROR: No files found from regexp\n" );
            return false;
        }
    }

    // make sure each channel has the same number of images
    m_nNumImages = m_vFileList[0].size();
    for( unsigned int ii = 1; ii < m_nNumChannels; ii++ ){
        if( m_vFileList[ii].size() != m_nNumImages ){
            mvl::PrintError( "ERROR: uneven number of files\n" );
            return false;
        }
    }

    // fill buffer
    for (unsigned int ii=0; ii < m_nBufferSize; ii++) {	_Read(); }

    // push timestamp of first image into the Virtual Device Queue
    VirtualDevice::PushTime( _GetNextTime() );


    // run thread to keep buffer full
    m_CaptureThread = new boost::thread( &_ThreadCaptureFunc, this );


    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Producer
void FileReaderDriver::_ThreadCaptureFunc( FileReaderDriver* pFR )
{
    while(1){
        try {
            boost::this_thread::interruption_point();

            if(!pFR->_Read()) {
                break;
            }

        } catch( boost::thread_interrupted& interruption ) {
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
bool FileReaderDriver::_Read()
{
    boost::mutex::scoped_lock lock(m_Mutex);

    // Wait until there is space in the buffer
    while(! (m_qImageBuffer.size() < m_nBufferSize) ){
        m_cBufferFull.wait(lock);
    }

    //*************************************************************************
    // produce to buffer
    //*************************************************************************

    // loop over if we finished our files!
    if( m_nCurrentImageIndex == m_nNumImages ) {
        if(m_bLoop == true) {
            m_nCurrentImageIndex = m_nStartFrame;
        }else{
            return false;
        }
    }

    // now fetch the next set of images
    std::string sFileName;
    std::vector< rpg::ImageWrapper > vImages;
    vImages.resize(m_nNumChannels);

    for( unsigned int ii = 0; ii < m_nNumChannels; ii++ ) {
        sFileName = m_vFileList[ii][m_nCurrentImageIndex];
        vImages[ii].read( sFileName, true, m_iCvImageReadFlags );
    }

    m_nCurrentImageIndex++;

    // add images at the back of the queue
    m_qImageBuffer.push(vImages);

    //*************************************************************************

    // send notification that the buffer is not empty
    m_cBufferEmpty.notify_one();

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
double FileReaderDriver::_GetNextTime()
{
    if( m_qImageBuffer.size() == 0 ) {
        return -1;
    }
    return m_qImageBuffer.front()[0].Map.GetProperty<double>( m_sTimeKeeper, 0 );
}