#include "FileReaderDriver.h"

#include <HAL/Devices/DeviceTime.h>
#include <HAL/Devices/DeviceException.h>
#include <HAL/Utils/StringUtils.h>

#include <opencv2/opencv.hpp>

#include "ReadImage.h"

using namespace std;

namespace hal {

FileReaderDriver::FileReaderDriver( const Uri& uri ) 
  : m_bShouldRun(false), m_nFramesProcessed(0)
  {
    // sat sane default parameters
    CameraDriverInterface::SetDefaultProperties({
        {"startframe", "0", "First frame to capture."},
        {"loop", "false", "Play beginning once finished."},
        {"grey", "false", "Convert internally to greyscale."},
        {"buffer", "10", "Number of frames to cache in memory"},
        {"frequency", "30", "Capture frequency to emulate if needed"},
        {"name", "FileCam", "Camera name."},
        {"id", "0", "Camera id (serial number or UUID)."}
        });
    if( !CameraDriverInterface::ParseUriProperties( uri.properties ) ){
      std::cerr << "FileReaderDriver knows about the following properties:\n";
      CameraDriverInterface::PrintPropertyMap();
      return;
    }

    m_nCurrentImageIndex = GetProperty("startframe", 0);
    m_bLoop              = GetProperty("loop", false);
    m_nBufferSize        = GetProperty("buffer", 10);
    m_sName              = GetProperty("name", std::string("FileCam"));
    m_sId                = GetProperty("id", std::string());
    frequency_           = GetProperty("frequency", 30.0);
    bool Grey            = GetProperty("grey", false);
    m_iCvImageReadFlags  = Grey ? 0 : -1;

    std::vector<std::string> ChannelRegex = Expand(uri.url, '[', ']', ',');
    for(std::string& s : ChannelRegex) {
      s = ExpandTildePath(s);
    }
    m_nNumChannels = ChannelRegex.size();


    // clear variables if previously initialized
    m_vFileList.clear();

    if(m_nNumChannels < 1) {
      std::cerr << "No channels specified.\n";
      return;
    }

    m_sBaseDir = DirUp(ChannelRegex[0]);

    m_vFileList.resize(m_nNumChannels);

    for(unsigned int ii = 0; ii < m_nNumChannels; ii++) {
      // Now generate the list of files for each channel
      std::vector< std::string >& vFiles = m_vFileList[ii];

      if(!hal::WildcardFileList(ChannelRegex[ii], vFiles)) {
        std::cerr << "No files found from regexp '" << ChannelRegex[ii] << "'.\n";
        return;
      }
    }

    // make sure each channel has the same number of images
    m_nNumImages = m_vFileList[0].size();
    for(unsigned int ii = 1; ii < m_nNumChannels; ii++){
      if(m_vFileList[ii].size() != m_nNumImages){
        std::stringstream sstm;
        sstm << "Uneven number of files. Count for camera " << ii << ": " <<
          m_vFileList[ii].size() << " vs count for camera 0: " <<
          m_nNumImages;
        std::cerr << sstm.str();
        return;
      }
    }

    // fill buffer
    m_nHead = m_nTail = 0;
    m_vBuffer.resize(m_nBufferSize);
    for (unsigned int ii=0; ii < m_nBufferSize; ii++) {
      _Read(); 
    }

    // push timestamp of first image into the Virtual Device Queue
    DeviceTime::PushTime(_GetNextTime());

    // run thread to keep buffer full
    m_bShouldRun = true;
    m_CaptureThread.reset(new std::thread(&_ThreadCaptureFunc, this));
  }

FileReaderDriver::~FileReaderDriver() {
  m_bShouldRun = false;
  if(m_CaptureThread) {
    while(!m_qImageBuffer.empty()) {
      m_qImageBuffer.pop();
    }
    m_cBufferFull.notify_one();
    m_CaptureThread->join();
  }
}

// Consumer
bool FileReaderDriver::Capture(hal::CameraMsg& vImages) {
  if(m_nCurrentImageIndex >= m_nNumImages && m_qImageBuffer.empty() &&!m_bLoop){
    return false;
  }

  std::unique_lock<std::mutex> lock(m_Mutex);

  // Wait until the buffer has data to read
  while (m_qImageBuffer.empty()) {
    m_cBufferEmpty.wait(lock);
  }

  DeviceTime::WaitForTime(_GetNextTime());

  //***************************************************
  // consume from buffer
  //***************************************************


  // now fetch the next set of images from buffer
  vImages = m_qImageBuffer.front();

  // remove image from buffer queue
  m_qImageBuffer.pop();

  // send notification that the buffer has space
  m_cBufferFull.notify_one();

  // push next timestamp to queue now that we popped from the buffer
  DeviceTime::PopAndPushTime(_GetNextTime());

  return true;
}

/*
std::string FileReaderDriver::GetProperty(const std::string& sProperty) 
{
  if(sProperty == "dir") {
    return m_sBaseDir;
  }
  else if(sProperty == "name") {
    return m_sName;
  }
  else if(sProperty == "id") {
    return m_sId;
  }

  return std::string();
}
*/

size_t FileReaderDriver::NumChannels() const {
  const hal::CameraMsg& NextFrame = m_qImageBuffer.front();
  return NextFrame.image_size();
}

size_t FileReaderDriver::Width(size_t idx) const {
  const hal::CameraMsg& NextFrame = m_qImageBuffer.front();
  if((int)idx < NextFrame.image_size()) {
    const hal::ImageMsg& NextImg = NextFrame.image(idx);
    return NextImg.width();
  }
  return 0;
}

size_t FileReaderDriver::Height(size_t idx) const {
  const hal::CameraMsg& NextFrame = m_qImageBuffer.front();
  if((int)idx < NextFrame.image_size()) {
    const hal::ImageMsg& NextImg = NextFrame.image(idx);
    return NextImg.height();
  }
  return 0;
}

// Producer
void FileReaderDriver::_ThreadCaptureFunc(FileReaderDriver* pFR) {
  while(pFR->m_bShouldRun) {
    if(!pFR->_Read()) {
      break;
    }
  }
}

bool FileReaderDriver::_Read() {
  std::unique_lock<std::mutex> lock(m_Mutex);

  // Wait until there is space in the buffer
  while(! (m_qImageBuffer.size() < m_nBufferSize)){
    m_cBufferFull.wait(lock);
  }

  //*************************************************************************
  // produce to buffer
  //*************************************************************************

  // loop over if we finished our files!
  if(m_nCurrentImageIndex == m_nNumImages) {
    if(m_bLoop == true) {
      m_nCurrentImageIndex = 0;  // Just start at the beginning
    }else{
      return false;
    }
  }

  // now fetch the next set of images
  std::string sFileName;

  hal::CameraMsg vImages;
  double device_timestamp = -1;
  for(unsigned int ii = 0; ii < m_nNumChannels; ++ii) {
    hal::ImageMsg* pbImg = vImages.add_image();
    sFileName = m_vFileList[ii][m_nCurrentImageIndex];
    cv::Mat cvImg = _ReadFile(sFileName, m_iCvImageReadFlags);

    double timestamp = _GetTimestamp(sFileName);
    if (timestamp < 0) timestamp = m_nFramesProcessed / frequency_;
    if (device_timestamp < 0) device_timestamp = timestamp;
    pbImg->set_timestamp(timestamp);

    //        hal::ReadCvMat(cvImg, pbImg);
    pbImg->set_height(cvImg.rows);
    pbImg->set_width(cvImg.cols);

    // TODO this is BAD since 4 bytes can be int, or float, etc, etc
    if(cvImg.elemSize1() == 1) {
      pbImg->set_type(hal::PB_UNSIGNED_BYTE);
    }
    if(cvImg.elemSize1() == 2) {
      pbImg->set_type(hal::PB_UNSIGNED_SHORT);
    }
    if(cvImg.elemSize1() == 4) {
      pbImg->set_type(hal::PB_FLOAT);
    }

    if(cvImg.channels() == 1) {
      pbImg->set_format(hal::PB_LUMINANCE);
    }
    if(cvImg.channels() == 3) {
      pbImg->set_format(hal::PB_RGB);
    }
    pbImg->set_data(
        (const char*)cvImg.data,
        cvImg.rows * cvImg.cols * cvImg.elemSize1() * cvImg.channels());
  }
  vImages.set_device_time(device_timestamp);
  vImages.set_system_time(device_timestamp);

  m_nCurrentImageIndex++;
  ++m_nFramesProcessed;

  // add images at the back of the queue
  m_qImageBuffer.push(vImages);

  //*************************************************************************

  // send notification that the buffer is not empty
  m_cBufferEmpty.notify_one();

  return true;
}

double FileReaderDriver::_GetNextTime() {
  if(m_qImageBuffer.empty()) {
    return -1;
  }
  return (m_qImageBuffer.front().has_device_time() ?
      m_qImageBuffer.front().device_time() : 0);
}

double FileReaderDriver::_GetTimestamp(const std::string& sFileName) const {
  // Returns the timestamp encoded in a filename, or -1.
  //
  // A timestamp is any valid number (starting with a digit) that appears in
  // any position of the string. If there are several numbers, the largest one
  // is returned.
  // Examples:
  // Camera_Left_12345.6789.jpg     returns 12345.6789
  // 12345.6789.jpg                 returns 12345.6789
  // m0001234.pgm                   returns 1234
  // Camera_1_12345.6789.jpg        returns 12345.6789
  // file.png                       returns -1

  // skip the path
  std::string::size_type pos = sFileName.find_last_of("/\\");
  if (pos == std::string::npos) pos = 0;

  double t = -1;
  const char* begin = sFileName.c_str() + pos;
  const char* end = sFileName.c_str() + sFileName.size();

  for(const char* cur = begin; cur != end;) {
    if (*cur < '0' || *cur > '9')
      ++cur;
    else {
      char* next_pos;
      double value = strtod(cur, &next_pos);
      if (next_pos == cur) break; // could not parse
      cur = next_pos;

      // in the insidious case of several numbers, choose the largest one
      if (value != HUGE_VAL && value > t) t = value;
    }
  }
  return t;
}

}
