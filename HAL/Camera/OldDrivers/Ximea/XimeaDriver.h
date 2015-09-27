#pragma once

#include <deque>
#include <vector>

#include <HAL/Camera/CameraDriverInterface.h>
#include <HAL/Utils/Uri.h>

#include <xiApi.h>

namespace hal {


  class XimeaDriver : public CameraDriverInterface
  {
    public:
      XimeaDriver( const Uri& uri );

      ~XimeaDriver();

      bool Capture(hal::CameraMsg& images);

      std::shared_ptr<CameraDriverInterface> GetInputDriver() {
        return std::shared_ptr<CameraDriverInterface>();
      }

      size_t NumChannels() const;
      size_t Width(size_t /*idx*/ = 0) const;
      size_t Height(size_t /*idx*/ = 0) const;

    private:
      void _CheckError(XI_RETURN err, std::string place);

    private:
      size_t                              num_channels_;
      std::vector<HANDLE>                 cam_handles_;
      XI_IMG                              image_;
      XI_IMG_FORMAT                       image_format_;
      unsigned int                        image_width_;
      unsigned int                        image_height_;
      int                                 sync_;
      uint8_t                             binning_;
      std::deque<std::vector<double> >    sync_times_;
  };

} /* namespace */