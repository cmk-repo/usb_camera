#include "usb_camera/usb_camera.h"

#include <cv_bridge/cv_bridge.h>

namespace usb_camera {

using std::cout;
using std::endl;
using sensor_msgs::CameraInfo;
using sensor_msgs::CameraInfoPtr;
using camera_info_manager::CameraInfoManager;

UsbCamera::UsbCamera(const ros::NodeHandle &nh) : nh_{nh}, it_{nh} {
  nh_.param<std::string>("frame_id", frame_id_, std::string("camera"));
  double fps;
  nh_.param<double>("fps", fps, 20.0);
  ROS_ASSERT_MSG(fps > 0, "Camera: fps must be greater than 0");
  rate_.reset(new ros::Rate(fps));

  // Create a camera
  nh_.param<int>("device", device_, 0);

  // Setup camera publisher and dyanmic reconfigure callback
  std::string calib_url;
  nh_.param<std::string>("calib_url", calib_url, "");
  CameraInfoManager cinfo_manager(nh_, frame_id_, calib_url);
  if (!cinfo_manager.isCalibrated()) {
    ROS_WARN_STREAM("Camera: " << frame_id_ << " not calibrated");
  }
  cinfo_ = CameraInfoPtr(new CameraInfo(cinfo_manager.getCameraInfo()));

  camera_pub_ = it_.advertiseCamera("image_raw", 1);
  server_.setCallback(
      boost::bind(&UsbCamera::ReconfigureCallback, this, _1, _2));
}

void UsbCamera::Run() {
  UsbCameraConfig config;
  nh_.param<bool>("color", config.color, false);
  Connect();
  Configure(config);
  Start();
}

void UsbCamera::End() { Stop(); }

void UsbCamera::PublishImage(const cv::Mat &image) {
  // Construct a cv image
  std_msgs::Header header;
  header.stamp = ros::Time::now();
  header.seq = seq_++;
  header.frame_id = frame_id_;
  std::string encodings;
  if (image.channels() == 1) {
    encodings = sensor_msgs::image_encodings::MONO8;
  } else if (image.channels() == 3) {
    encodings = sensor_msgs::image_encodings::BGR8;
  }
  // Convert to ros iamge msg and publish camera
  cv_bridge::CvImage cv_image(header, encodings, image);
  image_ = cv_image.toImageMsg();
  cinfo_->header = image_->header;
  camera_pub_.publish(image_, cinfo_);
  rate_->sleep();
}

void UsbCamera::ReconfigureCallback(usb_camera::UsbCameraDynConfig &config,
                                    int level) {
  // Do nothing when first starting
  if (level < 0) {
    return;
  }
  // Get config
  UsbCameraConfig usb_camera_config;
  usb_camera_config.color = config.color;
  Configure(usb_camera_config);
}

void UsbCamera::Connect() {
  camera_.reset(new cv::VideoCapture(device_));
  cout << label_ << "Connecting to camera " << device_ << endl;
}

void UsbCamera::Configure(const UsbCameraConfig &config) {
  cout << label_ << "Configuring camera" << endl;
  color_ = config.color;
  // Print final setting
  cout << label_ << "color: " << color_
       << " width: " << width() << " height: " << height() << endl;
}

void UsbCamera::Start() {
  // Set acquire to true
  acquire_ = true;
  // Create a new thread for acquisition
  image_thread_.reset(new std::thread(&UsbCamera::AcquireImages, this));
  cout << label_ << "Starting camera" << endl;
}

void UsbCamera::Stop() {
  cout << label_ << "Stopping camera" << endl;
  // Set acquire to false to stop the thread
  acquire_ = false;
  // Wait for the tread to finish
  image_thread_->join();
}

void UsbCamera::AcquireImages() {
  while (acquire_) {
    cv::Mat image_raw;
    camera_->read(image_raw);
    if (color_ && image_raw.channels() == 1) {
      cv::cvtColor(image_raw, image_raw, CV_GRAY2BGR);
    } else if (!color_ && image_raw.channels() == 3) {
      cv::cvtColor(image_raw, image_raw, CV_BGR2GRAY);
    }
    PublishImage(image_raw);
  }
}

inline int UsbCamera::width() {
  return camera_->get(CV_CAP_PROP_FRAME_WIDTH);
}

inline int UsbCamera::height() {
  return camera_->get(CV_CAP_PROP_FRAME_HEIGHT);
}

}  // namespace usb_camera
