#include <boost_plugin_loader/plugin_loader.hpp>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <industrial_calibration/core/serialization.h>
#include <industrial_calibration/target_finders/opencv/target_finder.h>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <yaml-cpp/yaml.h>

template<typename T>
T getParameter(ros::NodeHandle& nh, const std::string& key)
{
  T val;
  if(!nh.getParam(key, val))
      throw std::runtime_error("Failed to get '" + key + "' parameter");
  return val;
}

class TargetDetector
{
public:
  TargetDetector()
      : it_(ros::NodeHandle())
  {
    // Configure the plugin loader
    loader_.search_libraries.insert(INDUSTRIAL_CALIBRATION_PLUGIN_LIBRARIES);
    loader_.search_libraries_env = INDUSTRIAL_CALIBRATION_SEARCH_LIBRARIES_ENV;

    // Load the target finder
    ros::NodeHandle pnh("~");
    YAML::Node config = YAML::LoadFile(getParameter<std::string>(pnh, "config_file"));
    YAML::Node target_finder_config = getMember<YAML::Node>(config, "target_finder");
    factory_ = loader_.createInstance<industrial_calibration::TargetFinderFactoryOpenCV>(getMember<std::string>(target_finder_config, "type"));
    target_finder_ = factory_->create(target_finder_config);

    // Setup subscriber and publishers
    image_sub_ = it_.subscribe("image", 1, &TargetDetector::imageCb, this);
    detected_image_pub_ = it_.advertise("image_detected", 1);
    annotated_image_pub_ = it_.advertise("image_annotated", 1);
  }

  void imageCb(const sensor_msgs::ImageConstPtr& msg)
  {
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      if(sensor_msgs::image_encodings::bitDepth(msg->encoding) == 8)
      {
        cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
      }
      else
      {
        cv_ptr = cv_bridge::toCvCopy(msg);
        cv::normalize(cv_ptr->image, cv_ptr->image, 0, 255, cv::NormTypes::NORM_MINMAX);
        cv_ptr->image.convertTo(cv_ptr->image, CV_8U);
        cv_ptr->encoding = sensor_msgs::image_encodings::BGR8;
        if(cv_ptr->image.channels() != 3)
          cv::cvtColor(cv_ptr->image, cv_ptr->image, cv::COLOR_GRAY2BGR);
      }

      // Find target in image
      industrial_calibration::TargetFeatures2D target_features = target_finder_->findTargetFeatures(cv_ptr->image);
      cv::Mat annotated_image = target_finder_->drawTargetFeatures(cv_ptr->image, target_features);
      cv_bridge::CvImagePtr annotated_image_cv(new cv_bridge::CvImage(cv_ptr->header, cv_ptr->encoding, annotated_image));

      // Publish raw_image and image with drawn features
      detected_image_pub_.publish(msg);
      annotated_image_pub_.publish(annotated_image_cv->toImageMsg());
    }
    catch (const std::runtime_error& ex)
    {
      ROS_ERROR_STREAM(ex.what());
    }
  }

private:
  boost_plugin_loader::PluginLoader loader_;
  industrial_calibration::TargetFinderFactoryOpenCV::Ptr factory_;
  industrial_calibration::TargetFinderOpenCV::ConstPtr target_finder_;

  image_transport::ImageTransport it_;
  image_transport::Subscriber image_sub_;
  image_transport::Publisher detected_image_pub_;
  image_transport::Publisher annotated_image_pub_;
};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "target_detector_node");
  TargetDetector targetDetectorNode;
  ROS_INFO_STREAM("Started target detector node...");
  ros::spin();
  
  ros::shutdown();
  return 0;
}
