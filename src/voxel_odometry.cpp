/*
 *  Copyright 2013 Néstor Morales Hernández <nestor@isaatc.ull.es>
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#include "voxel_odometry.h"

#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <ros/ros.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <geometry_msgs/PoseArray.h>
#include <std_msgs/Float64.h>
#include <sensor_msgs/PointCloud2.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_ros/filters/voxel_grid.h>
#include <pcl-1.7/pcl/common/common.h>
#include <pcl_ros/filters/voxel_grid.h>
#include <pcl_ros/features/normal_3d.h>
#include <pcl-1.7/pcl/segmentation/conditional_euclidean_clustering.h>

#include <pcl/point_cloud.h>
#include <pcl/octree/octree.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>

#include <boost/foreach.hpp>
#include <boost/graph/graph_concepts.hpp>

#include <opencv2/opencv.hpp>
#include <message_filters/synchronizer.h>

#include <eigen3/Eigen/Core>

#include <nav_msgs/Odometry.h>

#include <math.h>

 ///////////////////////////////////////////

// #include <octomap/octomap.h>
// #include <octomap/OcTree.h>

#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/console/time.h>
// 
#include <pcl/filters/voxel_grid.h>
#include <pcl/features/normal_3d.h>
#include <pcl/segmentation/conditional_euclidean_clustering.h>

#include <pcl-1.7/pcl/search/kdtree.h>

#include <iostream>
#include <queue>
#include <pcl-1.7/pcl/impl/point_types.hpp>

#include "voxel_odometry/stats.h"
#include "utilspolargridtracking.h"

using namespace std;

namespace voxel_odometry {
    
VoxelOdometry::VoxelOdometry()
{
    m_initialized = false;
    
    m_obstacleColors.resize(boost::extents[MAX_OBSTACLES_VISUALIZATION][3]);
    for (uint32_t i = 0; i < MAX_OBSTACLES_VISUALIZATION; i++) {
        for (uint32_t c = 0; c < 3; c++) {
            m_obstacleColors[i][c] = (double)rand() / RAND_MAX;
        }
    }
    
    m_particleColors.resize(boost::extents[MAX_PARTICLE_AGE_REPRESENTATION]);
    m_particleColors[0] = cv::Scalar(255, 0, 0);   // Blue
    m_particleColors[1] = cv::Scalar(0, 255, 0);   // Green
    m_particleColors[2] = cv::Scalar(0, 0, 255);   // Red
    m_particleColors[3] = cv::Scalar(255, 255, 0);  // Cyan
    m_particleColors[4] = cv::Scalar(255, 0, 255);  // Magenta
    m_particleColors[5] = cv::Scalar(0, 255, 255);   // Yellow
    m_particleColors[6] = cv::Scalar(0, 0, 0);        // Black
    m_particleColors[7] = cv::Scalar(255, 255, 255);    // White
    
    m_lastMapOdomTransform.stamp_ = ros::Time(-1);
    ros::NodeHandle nh("~");
    
    // Reading params
    nh.param<string>("map_frame", m_mapFrame, "/map");
    nh.param<string>("pose_frame", m_poseFrame, "/base_footprint");
    nh.param<string>("camera_frame", m_cameraFrame, "/base_left_cam");
    
    nh.param("use_oflow", m_useOFlow, false);
    
    nh.param("publish_intermediate_info", m_publishIntermediateInfo, false);
    
    double dummyDouble;
    nh.param<double>("cell_size_x", dummyDouble, 0.5);
    m_cellSizeX = dummyDouble;
    nh.param<double>("cell_size_y", dummyDouble, 0.5);
    m_cellSizeY = dummyDouble;
    nh.param<double>("cell_size_z", dummyDouble, 0.5);
    m_cellSizeZ = dummyDouble;
    
    m_voxelSize = max(max(m_cellSizeX, m_cellSizeY), m_cellSizeZ);
    
    int dummyInteger;
    nh.param<int>("max_particles_number_per_voxel", dummyInteger, 30);
    m_maxNumberOfParticles = dummyInteger;
    nh.param<int>("num_threads", dummyInteger, 8);
    m_threads = dummyInteger;

    nh.param<double>("max_vel_x", m_maxVelX, 2.0);
    nh.param<double>("max_vel_y", m_maxVelY, 2.0);
    nh.param<double>("max_vel_z", m_maxVelZ, 0.0);

    if (m_maxVelZ != 0.0) {
        ROS_WARN("The max speed expected for the z axis is %f. Are you sure you expect this behaviour?", m_maxVelZ);
    }
    
    nh.param<double>("min_vel_x", m_minVelX, 0.3);
    nh.param<double>("min_vel_y", m_minVelY, 0.3);
    nh.param<double>("min_vel_z", m_minVelZ, 0.0);
    
    m_maxMagnitude = cv::norm(cv::Vec3f(m_maxVelX, m_maxVelY, m_maxVelZ));
    m_minMagnitude = cv::norm(cv::Vec3f(m_minVelX, m_minVelY, m_minVelZ));
    
    nh.param<double>("yaw_interval", m_yawInterval, 1.0);
    nh.param<double>("pitch_interval", m_pitchInterval, 1.0);
    nh.param<double>("speed_factor", m_factorSpeed, 0.1);
    
    nh.param<double>("occupancy_prob_tresh", m_threshOccupancyProb, 0.5);

    nh.param<int>("l1_distance_for_neighbor_thresh_x", dummyInteger, 1);
    m_neighBorX = dummyInteger;
    nh.param<int>("l1_distance_for_neighbor_thresh_y", dummyInteger, 1);
    m_neighBorY = dummyInteger;
    nh.param<int>("l1_distance_for_neighbor_thresh_z", dummyInteger, 1);
    m_neighBorZ = dummyInteger;
    
    string obstacleSpeedMethodStr;
    nh.param<string>("obstacle_speed_method", obstacleSpeedMethodStr, SPEED_METHOD_CIRC_HIST_STR);
    if (obstacleSpeedMethodStr == SPEED_METHOD_CIRC_HIST_STR) {
        m_obstacleSpeedMethod = SPEED_METHOD_CIRC_HIST;
    } else if (obstacleSpeedMethodStr == SPEED_METHOD_MEAN_STR) {
        m_obstacleSpeedMethod = SPEED_METHOD_MEAN;
    } else {
        ROS_ERROR_NAMED(__FILE__, 
                        "\"%s\" is not a valid obstacle speed computation method", obstacleSpeedMethodStr.c_str());
        exit(0);
    }
    
    nh.param<double>("random_particles_per_voxel", m_particlesPerVoxel, 100.0);
    
    // BEGIN: Just with flood_fill_segment
    nh.param<double>("yaw_thresh_to_join_voxels", m_threshYaw, 90.0 * M_PI / 180.0);
    nh.param<double>("pitch_thresh_to_join_voxels", m_threshPitch, 9999999.0);
    nh.param<double>("magnitude_thresh_to_join_voxels", m_threshMagnitude, 9999999.0);
    
    nh.param<int>("min_voxels_per_obstacle", dummyInteger, 2 * 2 * 2);
    m_minVoxelsPerObstacle = dummyInteger;
    
    nh.param<double>("min_voxel_density", m_minVoxelDensity, 10.0);
    nh.param<double>("max_common_value_to_join", m_maxCommonVolume, 0.8);
    nh.param<double>("min_obstacle_height", m_minObstacleHeight, 1.25);
    // END: Just with use_flood_fill_segment
    
    // BEGIN: Just with original segmentation method
    string voxelSpeedMethodStr;
    nh.param<string>("voxel_speed_method", voxelSpeedMethodStr, SPEED_METHOD_CIRC_HIST_STR);
    if (voxelSpeedMethodStr == SPEED_METHOD_CIRC_HIST_STR) {
        m_speedMethod = SPEED_METHOD_CIRC_HIST;
    } else if (voxelSpeedMethodStr == SPEED_METHOD_MEAN_STR) {
        m_speedMethod = SPEED_METHOD_MEAN;
    } else {
        ROS_ERROR_NAMED(__FILE__, 
                        "\"%s\" is not a valid obstacle speed computation method", voxelSpeedMethodStr.c_str());
        exit(0);
    }
    // END: Just with original segmentation method
    
    m_currX = m_currY = 0.0;
    
    m_currTheta = std::numeric_limits<double>::infinity();
    
    // Topics
    std::string left_info_topic = "left/camera_info";
    std::string right_info_topic = "right/camera_info";
    
    // Synchronize input topics. Optionally do approximate synchronization or not.
    bool approx;
    int queue_size;
    nh.param("approximate_sync", approx, false);
    nh.param("queue_size", queue_size, 10);
    nh.param("input_from_cameras", m_inputFromCameras, true);
    
//     if (m_inputFromCameras) {
//         m_leftCameraInfoSub.subscribe(nh, left_info_topic, 1);
//         m_rightCameraInfoSub.subscribe(nh, right_info_topic, 1);
//         m_pointCloudSub.subscribe(nh, "pointCloud", 10);
//         
//         if (m_useOFlow) {
// 
//             m_oFlowSub.subscribe(nh, "flow_vectors", 10);
//             m_oFlowCloud.reset(new pcl::PointCloud<pcl::PointXYZRGBNormal>);
//             
//             if (approx)
//             {
//                 m_approxSynchronizerOFlow.reset( new ApproximateSyncOFlow(ApproximatePolicyOFlow(queue_size),
//                                                                         m_pointCloudSub, m_oFlowSub, 
//                                                                         m_leftCameraInfoSub, 
//                                                                         m_rightCameraInfoSub) );
//                 m_approxSynchronizerOFlow->registerCallback(boost::bind(&VoxelOdometry::pointCloudCallback, 
//                                                                         this, _1, _2, _3, _4));
//             }
//             else
//             {
//                 m_exactSynchronizerOFlow.reset( new ExactSyncOFlow(ExactPolicyOFlow(queue_size),
//                                                                         m_pointCloudSub, m_oFlowSub, 
//                                                                         m_leftCameraInfoSub, 
//                                                                         m_rightCameraInfoSub) );
//                 m_exactSynchronizerOFlow->registerCallback(boost::bind(&VoxelOdometry::pointCloudCallback, 
//                                                                     this, _1, _2, _3, _4));
//             }
//         } else {
//             
//             if (approx)
//             {
//                 m_approxSynchronizer.reset( new ApproximateSync(ApproximatePolicy(queue_size),
//                                                                 m_pointCloudSub, 
//                                                                 m_leftCameraInfoSub, 
//                                                                 m_rightCameraInfoSub) );
//                 m_approxSynchronizer->registerCallback(boost::bind(&VoxelOdometry::pointCloudCallback, 
//                                                                 this, _1, _2, _3));
//             }
//             else
//             {
//                 m_exactSynchronizer.reset( new ExactSync(ExactPolicy(queue_size),
//                                                             m_pointCloudSub,
//                                                             m_leftCameraInfoSub, 
//                                                             m_rightCameraInfoSub) );
//                 m_exactSynchronizer->registerCallback(boost::bind(&VoxelOdometry::pointCloudCallback, 
//                                                                 this, _1, _2, _3));
//             }
//         }
//     } else {
        m_pointCloudJustPointCloudSub = nh.subscribe<sensor_msgs::PointCloud2>("pointCloud", 1, boost::bind(&VoxelOdometry::pointCloudCallback, this, _1));
//     }
    
    m_pointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    
    m_voxelsPub = nh.advertise<visualization_msgs::MarkerArray>("voxels", 1);
    m_voxelsIdxPub = nh.advertise<visualization_msgs::MarkerArray>("voxelsIdx", 1);
    m_particlesPub = nh.advertise<visualization_msgs::MarkerArray> ("particles", 1);
    m_particles0Pub = nh.advertise<geometry_msgs::PoseArray> ("particles0", 1);
    m_particles1Pub = nh.advertise<geometry_msgs::PoseArray> ("particles1", 1);
    m_particles2Pub = nh.advertise<geometry_msgs::PoseArray> ("particles2", 1);
    m_particles3Pub = nh.advertise<geometry_msgs::PoseArray> ("particles3", 1);
    m_particlesDPub = nh.advertise<geometry_msgs::PoseArray> ("particlesD", 1);
    m_particlesSimplePub = nh.advertise<geometry_msgs::PoseArray> ("particlesSimple", 1);
    m_oFlowPub = nh.advertise<geometry_msgs::PoseArray> ("oflow_visualization", 1);
    m_pointsPerVoxelPub = nh.advertise<sensor_msgs::PointCloud2> ("pointPerVoxel", 1);
    m_mainVectorsPub = nh.advertise<visualization_msgs::MarkerArray>("mainVectors", 1);
    m_obstaclesPub = nh.advertise<visualization_msgs::MarkerArray>("obstacles", 1);
    m_obstacleCubesPub = nh.advertise<visualization_msgs::MarkerArray>("cubes", 1);
    m_obstacleSpeedPub = nh.advertise<visualization_msgs::MarkerArray>("obstacleSpeed", 1);
    m_obstacleSpeedTextPub = nh.advertise<visualization_msgs::MarkerArray>("obstacleSpeedText", 1);
    m_timeStatsPub = nh.advertise<voxel_odometry::stats>("time_stats", 1);
    m_dynObjectsPub = nh.advertise<sensor_msgs::PointCloud2> ("dynamic_objects", 1);
    m_fakePointCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("fakePointCloud", 1);
    m_fakeParticlesPub = nh.advertise<geometry_msgs::PoseArray> ("fakeParticles", 1);
    
    m_segmentedPointCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("segmentedPointCloud", 1);
    m_debugSegmentPub = nh.advertise<sensor_msgs::PointCloud2> ("debugSegment", 1);
    m_debugProbPub = nh.advertise<sensor_msgs::PointCloud2> ("debugProbPub", 1);
    
    m_odomPub = nh.advertise<nav_msgs::Odometry>("odom", 50);
//     ros::spin();
}

void VoxelOdometry::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msgPointCloud) 
{
    cout << __FUNCTION__ << ":" << __LINE__ << endl;
    m_cameraFrame = msgPointCloud->header.frame_id;
    try {
        m_tfListener.lookupTransform(m_mapFrame, m_poseFrame, msgPointCloud->header.stamp, m_pose2MapTransform);
        m_tfListener.lookupTransform(m_cameraFrame, m_mapFrame, msgPointCloud->header.stamp, m_map2CamTransform);
//         m_tfListener.lookupTransform(m_mapFrame, m_poseFrame, ros::Time(0), m_pose2MapTransform);
//         m_tfListener.lookupTransform(m_cameraFrame, m_mapFrame, ros::Time(0), m_map2CamTransform);
    } catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
    }

    pcl::PointCloud<pcl::PointXYZ>::Ptr tmpCloud (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromROSMsg<pcl::PointXYZ>(*msgPointCloud, *tmpCloud);
    
    pcl::copyPointCloud(*tmpCloud, *m_pointCloud);
    
    if (m_pointCloud->size() != 0) {
    
        m_deltaTime = (msgPointCloud->header.stamp - m_lastPointCloudTime).toSec();
        
        m_lastPointCloudTime = msgPointCloud->header.stamp;
        
        m_currentId = msgPointCloud->header.seq;
        
        compute(m_pointCloud);
    }
}

void VoxelOdometry::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msgPointCloud,
                                           const sensor_msgs::CameraInfoConstPtr& leftCameraInfo, 
                                           const sensor_msgs::CameraInfoConstPtr& rightCameraInfo) 
{
    m_cameraFrame = msgPointCloud->header.frame_id;
    try {
        m_tfListener.lookupTransform(m_mapFrame, m_poseFrame, ros::Time(0), m_pose2MapTransform);
        m_tfListener.lookupTransform(m_cameraFrame, m_mapFrame, ros::Time(0), m_map2CamTransform);
    } catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
    }
    
    pcl::fromROSMsg<pcl::PointXYZRGB>(*msgPointCloud, *m_pointCloud);
    
    if (m_pointCloud->size() != 0) {
        
        m_deltaTime = (msgPointCloud->header.stamp - m_lastPointCloudTime).toSec();
        
        m_lastPointCloudTime = msgPointCloud->header.stamp;
        
        m_stereoCameraModel.fromCameraInfo(*leftCameraInfo, *rightCameraInfo);
        m_currentId = leftCameraInfo->header.seq;
        
        compute(m_pointCloud);
    }
    
}

void VoxelOdometry::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msgPointCloud, 
                                           const sensor_msgs::PointCloud2::ConstPtr& msgOFlow,
                                           const sensor_msgs::CameraInfoConstPtr& leftCameraInfo, 
                                           const sensor_msgs::CameraInfoConstPtr& rightCameraInfo) 
{
    m_cameraFrame = msgPointCloud->header.frame_id;
    try {
        m_tfListener.lookupTransform(m_mapFrame, m_poseFrame, ros::Time(0), m_pose2MapTransform);
    } catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
    }
    
    pcl::fromROSMsg<pcl::PointXYZRGB>(*msgPointCloud, *m_pointCloud);
    
    if (m_pointCloud->size() != 0) {
        
        m_deltaTime = (msgPointCloud->header.stamp - m_lastPointCloudTime).toSec();
        
        
        pcl::fromROSMsg<pcl::PointXYZRGBNormal>(*msgOFlow, *m_oFlowCloud);
        
        m_lastPointCloudTime = msgPointCloud->header.stamp;
        m_currentId = leftCameraInfo->header.seq;
        
        m_stereoCameraModel.fromCameraInfo(*leftCameraInfo, *rightCameraInfo);
        
        compute(m_pointCloud);
    }
}

/**
 * Given a certain pointCloud, the frame is computed
 * @param pointCloud: The input point cloud.
 */
void VoxelOdometry::compute(const PointCloudPtr& pointCloud)
{
    cout << __FILE__ << endl;
    voxel_odometry::stats timeStatsMsg;
    timeStatsMsg.header.seq = m_currentId;
    timeStatsMsg.header.stamp = ros::Time::now();
    
    INIT_CLOCK(startCompute)
    
    // Grid is reset
    INIT_CLOCK(startCompute1)
//     reset();
    END_CLOCK(totalCompute1, startCompute1)
    ROS_INFO("[%s] %d, reset: %f seconds", __FUNCTION__, __LINE__, totalCompute1);
    
//     INIT_CLOCK(startExtractDynamicObjects)
//     extractDynamicObjects(pointCloud);
//     END_CLOCK(totalExtractDynamicObjects, startExtractDynamicObjects)
//     ROS_INFO("[%s] %d, extractDynamicObjects: %f seconds", __FUNCTION__, __LINE__, totalExtractDynamicObjects);
    
// START OF COMMENT
    // Having a point cloud, the voxel grid is computed
    INIT_CLOCK(startCompute2)
    getVoxelGridFromPointCloud(pointCloud);
    END_CLOCK(totalCompute2, startCompute2)
    ROS_INFO("[%s] %d, getVoxelGridFromPointCloud: %f seconds", __FUNCTION__, __LINE__, totalCompute2);
    timeStatsMsg.getVoxelGridFromPointCloud = totalCompute2;
    
    if(m_useOFlow) {
        INIT_CLOCK(startCompute3)
        updateFromOFlow();
        END_CLOCK(totalCompute3, startCompute3)
        ROS_INFO("[%s] %d, updateFromOFlow: %f seconds", __FUNCTION__, __LINE__, totalCompute3);
        timeStatsMsg.updateFromOFlow = totalCompute3;
    }
    INIT_CLOCK(startCompute4)
    getMeasurementModel();
    END_CLOCK(totalCompute4, startCompute4)
    ROS_INFO("[%s] %d, getMeasurementModel: %f seconds", __FUNCTION__, __LINE__, totalCompute4);
    timeStatsMsg.getMeasurementModel = totalCompute4;
    
    // TODO:
    // Improve the way in which flow vectors are computed
    
    if (m_initialized) {
        INIT_CLOCK(startCompute5)
        prediction();
        END_CLOCK(totalCompute5, startCompute5)
        ROS_INFO("[%s] %d, prediction: %f seconds", __FUNCTION__, __LINE__, totalCompute5);
        timeStatsMsg.prediction = totalCompute5;
        
        INIT_CLOCK(startCompute6)
        measurementBasedUpdate();
        END_CLOCK(totalCompute6, startCompute6)
        ROS_INFO("[%s] %d, measurementBasedUpdate: %f seconds", __FUNCTION__, __LINE__, totalCompute6);
        timeStatsMsg.measurementBasedUpdate = totalCompute6;

//         publishParticles();
        
        INIT_CLOCK(startCompute7)
        joinVoxels();
        END_CLOCK(totalCompute7, startCompute7)
        ROS_INFO("[%s] %d, joinVoxels: %f seconds", __FUNCTION__, __LINE__, totalCompute7);
        timeStatsMsg.segment = totalCompute7;

        INIT_CLOCK(startCompute8)
        updateSpeedFromObstacles();
        END_CLOCK(totalCompute8, startCompute8)
        ROS_INFO("[%s] %d, updateSpeedFromObstacles: %f seconds", __FUNCTION__, __LINE__, totalCompute8);
        timeStatsMsg.updateSpeedFromObstacles = totalCompute8;
        INIT_CLOCK(startCompute2)
    }
    INIT_CLOCK(startCompute9)
    initialization();
    END_CLOCK(totalCompute9, startCompute9)
    ROS_INFO("[%s] %d, initialization: %f seconds", __FUNCTION__, __LINE__, totalCompute9);
    timeStatsMsg.initialization = totalCompute9;
// END OF COMMENT
    
    
    
//     publishParticles(m_oldParticlesPub, 2.0);
    
//     initialization();
    
    

    END_CLOCK(totalCompute, startCompute)
    
    ROS_INFO("[%s] Total time: %f seconds", __FUNCTION__, totalCompute);
    timeStatsMsg.totalCompute = totalCompute;
    
    INIT_CLOCK(startVis)
    if (m_publishIntermediateInfo) {
//         cout << __FILE__ << ":" << __LINE__ << endl;
        publishVoxels();
//         cout << __FILE__ << ":" << __LINE__ << endl;
        publishOFlow();
//         cout << __FILE__ << ":" << __LINE__ << endl;
        publishParticles();
        publishMainVectors();
//         cout << __FILE__ << ":" << __LINE__ << endl;
        publishObstacles();
//         cout << __FILE__ << ":" << __LINE__ << endl;
        publishObstacleCubes();
//         cout << __FILE__ << ":" << __LINE__ << endl;
    }
    END_CLOCK(totalVis, startVis)
    
    publishFakePointCloud();
    publishOdom();
//     cout << __FILE__ << ":" << __LINE__ << endl;
    
    ROS_INFO("[%s] Total visualization time: %f seconds", __FUNCTION__, totalVis);
    timeStatsMsg.totalVisualization = totalVis;
    
    m_timeStatsPub.publish(timeStatsMsg);
}


/**
 * The grid is emptied
 */
void VoxelOdometry::reset()
{
    for (uint32_t x = 0; x < m_dimX; x++) {
        for (uint32_t y = 0; y < m_dimY; y++) {
            for (uint32_t z = 0; z < m_dimZ; z++) {
                m_grid[x][y][z]->reset();
            }
        }
    }
}

void VoxelOdometry::getVoxelGridFromPointCloud(const PointCloudPtr& pointCloud)
{
    INIT_CLOCK(startCompute)
    
    PointType minPoint, maxPoint;
    
//     if (m_threads >= 3) {
//         // This way is twice faster than pcl::getMinMax3D implementation
//         minPoint = pointCloud->at(0);
//         maxPoint = pointCloud->at(0);
//         
//         #pragma omp parallel num_threads(3) 
//         {
//             #pragma omp sections nowait 
//             {
//                 #pragma omp section 
//                 {
//                     // X
//                     for (uint32_t i = 0; i < pointCloud->size(); i++) {
//                         if (minPoint.x > pointCloud->at(i).x) minPoint.x = pointCloud->at(i).x;
//                         if (maxPoint.x < pointCloud->at(i).x) maxPoint.x = pointCloud->at(i).x;
//                     }
//                 }
//                 #pragma omp section 
//                 {
//                     // Y
//                     for (uint32_t i = 0; i < pointCloud->size(); i++) {
//                         if (minPoint.y > pointCloud->at(i).y) minPoint.y = pointCloud->at(i).y;
//                         if (maxPoint.y < pointCloud->at(i).y) maxPoint.y = pointCloud->at(i).y;
//                     }
//                 }
//                 #pragma omp section 
//                 {
//                     // Z
//                     for (uint32_t i = 0; i < pointCloud->size(); i++) {
//                         if (minPoint.z > pointCloud->at(i).z) minPoint.z = pointCloud->at(i).z;
//                         if (maxPoint.z < pointCloud->at(i).z) maxPoint.z = pointCloud->at(i).z;
//                     }
//                 }
//             }
//         }
//     } else {
//         pcl::getMinMax3D(*pointCloud, minPoint, maxPoint);
//     }
    

    const float halfSizeX = m_cellSizeX / 2.0f;
    const float halfSizeY = m_cellSizeY / 2.0f;
    const float halfSizeZ = m_cellSizeZ / 2.0f;
    
    m_minX = floor(minPoint.x / m_cellSizeX) * m_cellSizeX;
    m_minY = floor(minPoint.y / m_cellSizeY) * m_cellSizeY;
    m_minZ = floor(minPoint.z / m_cellSizeZ) * m_cellSizeZ;
    
    m_maxX = ceil(maxPoint.x / m_cellSizeX) * m_cellSizeX;
    m_maxY = ceil(maxPoint.y / m_cellSizeY) * m_cellSizeY;
    m_maxZ = ceil(maxPoint.z / m_cellSizeZ) * m_cellSizeZ;
    
    m_minX = -20.0;
    m_maxX = 20.0;
    m_minY = -20.0;
    m_maxY = 20.0;
    m_minZ = 0.5;
    m_maxZ = 3.5;
    
    m_dimX = (m_maxX - m_minX) / m_cellSizeX;
    m_dimY = (m_maxY - m_minY) / m_cellSizeY; 
    m_dimZ = (m_maxZ - m_minZ) / m_cellSizeZ;
    
    m_grid.resize(boost::extents[0][0][0]);
    m_grid.resize(boost::extents[m_dimX][m_dimY][m_dimZ]);
    
    m_voxelList.clear();
    m_voxelList.reserve(m_dimX * m_dimY * m_dimZ);
    
    END_CLOCK(totalCompute, startCompute)
    ROS_INFO("[%s] %d: %f seconds", __FUNCTION__, __LINE__, totalCompute);
    
    RESET_CLOCK(startCompute)
    
    // Create the Kd-Tree
    pcl::search::KdTree<PointType> kdtree;
    kdtree.setSortedResults(false);
    kdtree.setEpsilon(std::min(halfSizeX, std::min(halfSizeY, halfSizeZ)));
    kdtree.setInputCloud (pointCloud);
    END_CLOCK_2(totalCompute, startCompute)
    ROS_INFO("[%s] %d: %f seconds", __FUNCTION__, __LINE__, totalCompute);

    RESET_CLOCK(startCompute)

    std::vector<int> pointIdxRadiusSearch;
    std::vector<float> pointRadiusSquaredDistance;

    double focalX = 0.0, focalY = 0.0;
    if (m_inputFromCameras) {
        focalX = m_stereoCameraModel.left().fx();
        focalY = m_stereoCameraModel.left().fy();
    }

    PointCloudPtr currPointCloud(new PointCloud);

    cout << "m_minX " << m_minX << endl;
    cout << "m_maxX " << m_maxX << endl;
    cout << "m_minY " << m_minY << endl;
    cout << "m_maxY " << m_maxY << endl;
    cout << "m_minZ " << m_minZ << endl;
    cout << "m_maxZ " << m_maxZ << endl;
    
    PointType searchPoint;
    for (searchPoint.x = m_minX + halfSizeX; searchPoint.x < m_maxX; searchPoint.x += m_cellSizeX) {
        for (searchPoint.y = m_minY + halfSizeY; searchPoint.y < m_maxY; searchPoint.y += m_cellSizeY) {
            for (searchPoint.z = m_minZ + halfSizeZ; searchPoint.z < m_maxZ; searchPoint.z += m_cellSizeZ) {

                if ((fabs(searchPoint.x) < 6.0) || (fabs(searchPoint.x) < 6.0) || (fabs(searchPoint.x) < 6.0))
                    continue;
                
                float prob = 1.0;
                const uint32_t neighbours = kdtree.radiusSearch(searchPoint, m_voxelSize / 2.0, 
                                                                pointIdxRadiusSearch, pointRadiusSquaredDistance);

                if (neighbours == 0)
                    continue;

                currPointCloud->push_back(searchPoint);

                // Avoids using static obstacles
//                 if (!m_inputFromCameras && m_lastPointCloud) {

//                     const uint32_t & prevNeighbours = m_kdtreeLastPointCloud.radiusSearch(searchPoint, m_voxelSize / 2.0, 
//                                                                                     pointIdxRadiusSearch, pointRadiusSquaredDistance);

//                     if (prevNeighbours != 0)
//                         continue;

//                 }

                if (m_inputFromCameras) {

                    tf::Vector3 point = m_map2CamTransform * tf::Vector3(searchPoint.x, searchPoint.y, searchPoint.z);
                    const float & X = point[0];
                    const float & Y = point[1];
                    const float & Z = point[2];
                    
                    const float & fX_Z = focalX / Z;
                    const float & u = X * fX_Z;
                    const float & u0 = (X - m_cellSizeX) * fX_Z;
                    const float & u1 = (X + m_cellSizeX) * fX_Z;
                    const float & sigmaX = (u1 - u0) + 1;//2 * (u1 - u0) + 1;
                    
                    const float & fY_Z = focalY / Z;
                    const float & v = Y * fY_Z;
                    const float & v0 = (Y - m_cellSizeY) * fY_Z;
                    const float & v1 = (Y + m_cellSizeY) * fY_Z;
                    const float & sigmaY = (u1 - u0) + 1; //2 * (v1 - v0) + 1;
                    
                    prob = neighbours / sqrt(sigmaX * sigmaY);

                }

                // Just voxels with enough probability are added to the list
                if (prob > m_threshOccupancyProb) {

                    const float & x = floor((searchPoint.x - m_minX) / m_cellSizeX);
                    const float & y = floor((searchPoint.y - m_minY)  / m_cellSizeY);
                    const float & z = floor((searchPoint.z - m_minZ)  / m_cellSizeZ);

                    // FIXME: This is just for debugging
//                     if (z == 1.0) {

                        image_geometry::StereoCameraModel * stereoCameraModel = NULL;
                        if (m_inputFromCameras)
                            stereoCameraModel = &m_stereoCameraModel;
    
                        VoxelPtr voxelPtr( new Voxel(x, y, z, 
                                            searchPoint.x, searchPoint.y, searchPoint.z, 
                                            m_cellSizeX, m_cellSizeY, m_cellSizeZ, 
                                            m_maxVelX, m_maxVelY, m_maxVelZ, 
                                            stereoCameraModel, m_speedMethod,
                                            m_yawInterval, m_pitchInterval, m_factorSpeed));

                        if (! m_inputFromCameras)
                            voxelPtr->setOccupiedProb(1.0);

                        m_voxelList.push_back(voxelPtr);
                        m_grid[x][y][z] = voxelPtr;

//                     }
                }

            }
        }
    }
    
    cout << "m_voxelList.size() " << m_voxelList.size() << endl;

//     m_lastPointCloud.reset(new PointCloud);
//     pcl::copyPointCloud(*currPointCloud, *m_lastPointCloud);
//     BOOST_FOREACH (VoxelPtr & voxel, m_voxelList) {
//         m_lastPointCloud->push_back(PointType(voxel->centroidX(), voxel->centroidY(), voxel->centroidZ()));
//     }
//     m_kdtreeLastPointCloud.setSortedResults(false);
//     m_kdtreeLastPointCloud.setEpsilon(std::min(halfSizeX, std::min(halfSizeY, halfSizeZ)));
//     m_kdtreeLastPointCloud.setInputCloud (m_lastPointCloud);
    
    END_CLOCK_2(totalCompute, startCompute)
    ROS_INFO("[%s] %d: %f seconds", __FUNCTION__, __LINE__, totalCompute);
}


void VoxelOdometry::updateFromOFlow()
{
    BOOST_FOREACH(pcl::PointXYZRGBNormal & flowVector, *m_oFlowCloud) {

        
        if (cv::norm(cv::Vec3f(flowVector.normal_x, flowVector.normal_y, flowVector.normal_z)) > 
            cv::norm(cv::Vec3f(m_maxVelX, m_maxVelY, m_maxVelZ))) {
            
            continue;
        }
            
        ParticlePtr particle(new Particle3d(flowVector.x, flowVector.y, flowVector.z, 
                                    flowVector.normal_x, flowVector.normal_y, flowVector.normal_z, 
                                    m_pose2MapTransform));
        
        int32_t xPos, yPos, zPos;
        particleToVoxel(particle, xPos, yPos, zPos);
        
        if ((xPos >= 0) && (xPos < m_dimX) &&
            (yPos >= 0) && (yPos < m_dimY) &&
            (zPos >= 0) && (zPos < m_dimZ)) {                    
            
            if (m_grid[xPos][yPos][zPos]->occupied()) {
                particle->setAge(m_grid[xPos][yPos][zPos]->oldestParticle() + 2);
                
                m_grid[xPos][yPos][zPos]->addFlowParticle(particle);
            }
        }
    }
}

// TODO: This part is not meant to be used in the future. I leave it for compatibility, but 
// it will dissappear.
void VoxelOdometry::getMeasurementModel()
{
    if (m_inputFromCameras) {
        for (uint32_t x = 0; x < m_dimX; x++) {
            for (uint32_t y = 0; y < m_dimY; y++) {
                for (uint32_t z = 0; z < m_dimZ; z++) {
                    VoxelPtr & voxel = m_grid[x][y][z];
                    
                    if (voxel) {
                        const int & sigmaX = voxel->sigmaX();
                        const int & sigmaY = voxel->sigmaY();
                        const int & sigmaZ = voxel->sigmaZ();
                        
                        for (uint32_t x1 = max(0, (int)(x - sigmaX)); x1 <= min((int)(m_dimX - 1), (int)(x + sigmaX)); x1++) {
                            for (uint32_t y1 = max(0, (int)(y - sigmaY)); y1 <= min((int)(m_dimY - 1), (int)(y + sigmaY)); y1++) {
                                for (uint32_t z1 = max(0, (int)(z - sigmaZ)); z1 <= min((int)(m_dimZ - 1), (int)(z + sigmaZ)); z1++) {
                                    if (m_grid[x1][y1][z1])
                                        m_grid[x1][y1][z1]->incNeighborOcc();
                                }
                            }
                        }
                    }
                }
            }
        }
        
        for (uint32_t x = 0; x < m_dimX; x++) {
            for (uint32_t y = 0; y < m_dimY; y++) {
                for (uint32_t z = 0; z < m_dimZ; z++) {
                    VoxelPtr & voxel = m_grid[x][y][z];

                    if (voxel) {
                        const int & sigmaX = voxel->sigmaX();
                        const int & sigmaY = voxel->sigmaY();
                        const int & sigmaZ = voxel->sigmaZ();
                    
                        // p(m(x,z) | occupied)
                        const double occupiedProb = (double)voxel->neighborOcc() / 
                                    ((2.0 * (double)sigmaX + 1.0) + (2.0 * (double)sigmaY + 1.0) + (2.0 * (double)sigmaZ + 1.0));
                        voxel->setOccupiedProb(occupiedProb);
                    }
                }
            }
        }
    }/* else {
        for (uint32_t x = 0; x < m_dimX; x++) {
            for (uint32_t y = 0; y < m_dimY; y++) {
                for (uint32_t z = 0; z < m_dimZ; z++) {
                    VoxelPtr & voxel = m_grid[x][y][z];
                    
                    if (voxel) {
                        const int & sigmaX = voxel->sigmaX();
                        const int & sigmaY = voxel->sigmaY();
                        const int & sigmaZ = voxel->sigmaZ();
                        
                        // p(m(x,z) | occupied)
                        const double occupiedProb = 1.0;
                    }
                }
            }
        }
    }*/
}

void VoxelOdometry::initialization()
{
    cout << "Initializing " << m_voxelList.size() << endl;
    
    vector <ParticleList> particles(m_voxelList.size());

    uint32_t totalParticles = 0;
    #pragma omp parallel for
    for (uint32_t i = 0; i < m_voxelList.size(); i++) {
        VoxelPtr & voxel = m_voxelList.at(i);

        const double & occupiedProb = voxel->occupiedProb();
                
        particles[i] = voxel->createParticlesStatic(m_pose2MapTransform);
        
        totalParticles += particles[i].size();
    }

    m_particles.reserve(m_particles.size() + totalParticles);
    
//     totalParticles = 0;
    for (uint32_t i = 0; i < m_voxelList.size(); i++) {
        
        ParticleList & newParticles = particles[i];
        
        m_particles.insert(m_particles.end(), newParticles.begin(), newParticles.end());
        
//         totalParticles += newParticles.size();
    }
    
    m_initialized = true;
}

inline void VoxelOdometry::particleToVoxel(const ParticlePtr & particle, 
                                               int32_t & posX, int32_t & posY, int32_t & posZ)
{
    const double dPosX = (particle->x() - m_minX) / m_cellSizeX;
    const double dPosY = (particle->y() - m_minY) / m_cellSizeY;
    const double dPosZ = (particle->z() - m_minZ) / m_cellSizeZ;

    // This check is needed to avoid truncating to 0 the case (-0.***)
    posX = (dPosX < 0.0)? -1 : dPosX;
    posY = (dPosY < 0.0)? -1 : dPosY;
    posZ = (dPosZ < 0.0)? -1 : dPosZ;
}

void VoxelOdometry::prediction()
{
    // TODO: Put correct values for deltaX, deltaY, deltaZ, deltaVX, deltaVY, deltaVZ in class Particle,
    // based on the covariance matrix
    ParticleList newParticles;
    newParticles.reserve(m_particles.size());
    BOOST_FOREACH(ParticlePtr & particle, m_particles) {
        particle->transform(m_deltaTime);
        
        int32_t xPos, yPos, zPos;
        particleToVoxel(particle, xPos, yPos, zPos);
        
        if ((xPos >= 0) && (xPos < m_dimX) &&
            (yPos >= 0) && (yPos < m_dimY) &&
            (zPos >= 0) && (zPos < m_dimZ)) {                    
        
            VoxelPtr & voxel = m_grid[xPos][yPos][zPos];
            if (voxel) {
                voxel->addParticle(particle);
                newParticles.push_back(particle);
            }
        }
    }
    
    if (m_useOFlow) {
        for (uint32_t x = 0; x < m_dimX; x++) {
            for (uint32_t y = 0; y < m_dimY; y++) {
                for (uint32_t z = 0; z < m_dimZ; z++) {
                    VoxelPtr & voxel = m_grid[x][y][z];
                    if (voxel)
                        voxel->joinParticles();
                }
            }
        }
    }
    
    m_particles.swap(newParticles);
        
}

void VoxelOdometry::measurementBasedUpdate()
{
    for (uint32_t x = 0; x < m_dimX; x++) {
        for (uint32_t y = 0; y < m_dimY; y++) {
            for (uint32_t z = 0; z < m_dimZ; z++) {
                VoxelPtr & voxel = m_grid[x][y][z];
                
                if ((voxel) && (! voxel->empty()) && (voxel->occupied())) {
                    voxel->sortParticles();
//                     voxel->setMainVectors(m_deltaX, m_deltaY, m_deltaZ);
                    voxel->updateHistogram();
                    voxel->reduceParticles(m_maxNumberOfParticles);
//                     voxel->centerParticles();
                }
            }
        }
    }
    
    return;
    
    for (uint32_t x = 0; x < m_dimX; x++) {
        for (uint32_t y = 0; y < m_dimY; y++) {
            for (uint32_t z = 0; z < m_dimZ; z++) {
                VoxelPtr & voxel = m_grid[x][y][z];
            
                if ((! voxel->empty()) && (voxel->occupied())) {
                    voxel->setOccupiedPosteriorProb(m_particlesPerVoxel);
                    const double Nrc = voxel->occupiedPosteriorProb() * m_particlesPerVoxel;
                    const double fc = Nrc / voxel->numParticles();
                    
                    if (fc > 1.0) {
                        const double Fn = floor(fc);       // Integer part
                        const double Ff = fc - Fn;         // Fractional part
                        
                        for (uint32_t i = 0; i < voxel->numParticles(); i++) {
                            
                            const ParticlePtr & p = voxel->getParticle(i);
                            
                            for (uint32_t k = 1; k < Fn; k++)
                                for (uint32_t n = 0; n < p->age(); n++)
                                    voxel->makeCopy(p);
                            
                            const double r = (double)rand() / (double)RAND_MAX;
                            if (r < Ff)
                                voxel->makeCopy(p);
                        }
                    } else if (fc < 1.0) {
                        for (uint32_t i = 0; i < voxel->numParticles(); i++) {
                            const double r = (double)rand() / (double)RAND_MAX;
                            if (r > fc)
                                voxel->removeParticle(i);
                        }
                    }
                }
            }
        }
    }
}

void VoxelOdometry::joinVoxels()
{
    m_obstacles.clear();
    VoxelObstaclePtr obst(new VoxelObstacle(m_obstacles.size(), 
                            m_threshYaw, m_threshPitch, m_threshMagnitude, 
                            m_minVoxelDensity, m_obstacleSpeedMethod, 
                            m_yawInterval, m_pitchInterval));

    BOOST_FOREACH(VoxelPtr & voxel, m_voxelList) {
        
        obst->addVoxelToObstacle(voxel);
    }
        
    m_obstacles.push_back(obst);
}

void VoxelOdometry::updateSpeedFromObstacles()
{
    BOOST_FOREACH(VoxelObstaclePtr & obstacle, m_obstacles) {
//         obstacle.updateSpeed(m_deltaX, m_deltaY, m_deltaZ);
        obstacle->updateSpeedFromParticles();
        obstacle->updateHistogram(m_maxVelX, m_maxVelY, m_maxVelZ, m_factorSpeed, m_minMagnitude);
    }
}

void VoxelOdometry::publishVoxels()
{
    visualization_msgs::MarkerArray voxelMarkers;
    visualization_msgs::MarkerArray voxelIdxList;
    visualization_msgs::MarkerArray voxelIdxListCleaner;
    
    voxelIdxListCleaner.markers.clear();
    for (uint32_t i = 0; i < m_dimX * m_dimY * m_dimZ; i++) {
        visualization_msgs::Marker voxelIdx;
        voxelIdx.header.frame_id = m_mapFrame;
        voxelIdx.header.stamp = ros::Time();
        voxelIdx.id = i;
        
        voxelIdx.ns = "voxels";
        voxelIdx.type = visualization_msgs::Marker::CUBE;
        voxelIdx.action = visualization_msgs::Marker::DELETE;
        
        voxelIdxListCleaner.markers.push_back(voxelIdx);
    }
    m_voxelsPub.publish(voxelIdxListCleaner);
    
    uint32_t idCount = 0;
    BOOST_FOREACH(const VoxelPtr & voxel, m_voxelList) {
        visualization_msgs::Marker voxelMarker;
        voxelMarker.header.frame_id = m_mapFrame;
        voxelMarker.header.stamp = ros::Time();
        voxelMarker.id = idCount++;
        voxelMarker.ns = "voxels";
        voxelMarker.type = visualization_msgs::Marker::CUBE;
        voxelMarker.action = visualization_msgs::Marker::ADD;

        voxelMarker.pose.position.x = voxel->centroidX();
        voxelMarker.pose.position.y = voxel->centroidY();
        voxelMarker.pose.position.z = voxel->centroidZ();
        
        voxelMarker.pose.orientation.x = 0.0;
        voxelMarker.pose.orientation.y = 0.0;
        voxelMarker.pose.orientation.z = 0.0;
        voxelMarker.pose.orientation.w = 1.0;
        voxelMarker.scale.x = m_cellSizeX;
        voxelMarker.scale.y = m_cellSizeY;
        voxelMarker.scale.z = m_cellSizeZ;
        voxelMarker.color.r = (double)rand() / RAND_MAX;
        voxelMarker.color.g = (double)rand() / RAND_MAX;
        voxelMarker.color.b = (double)rand() / RAND_MAX;
        voxelMarker.color.a = 0.5;
//         voxelMarker.color.a = voxel.occupiedProb();
        
        voxelMarkers.markers.push_back(voxelMarker);

        // ********************************************************
        // Publication of the speed text of the obstacle
        // ********************************************************
        
        visualization_msgs::Marker voxelIdx;
        voxelIdx.header.frame_id = m_mapFrame;
        voxelIdx.header.stamp = ros::Time();
        voxelIdx.id = idCount;
        voxelIdx.ns = "speedText";
        voxelIdx.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        voxelIdx.action = visualization_msgs::Marker::ADD;
        
        voxelIdx.pose.position.x = voxel->centroidX();
        voxelIdx.pose.position.y = voxel->centroidY();
        voxelIdx.pose.position.z = voxel->centroidZ();
        
        voxelIdx.pose.orientation.x = 0.0;
        voxelIdx.pose.orientation.y = 0.0;
        voxelIdx.pose.orientation.z = 0.0;
        voxelIdx.pose.orientation.w = 1.0;
        voxelIdx.scale.z = 0.125;
        voxelIdx.color.a = 1.0;
        voxelIdx.color.r = 0.0;
        voxelIdx.color.g = 1.0;
        voxelIdx.color.b = 0.0;
        
        stringstream ss;
        ss << voxel->x() << ", " << voxel->y() << ", " << voxel->z() << endl;
        voxelIdx.text = ss.str();
        
        voxelIdxList.markers.push_back(voxelIdx);
    }
    
    m_voxelsIdxPub.publish(voxelIdxList);

    m_voxelsPub.publish(voxelMarkers);
}

void VoxelOdometry::publishOFlow()
{
    geometry_msgs::PoseArray oflowVectors;
    
    oflowVectors.header.frame_id = m_mapFrame;
    oflowVectors.header.stamp = ros::Time();
    
    uint32_t idCount = 0;
    for (uint32_t x = 0; x < m_dimX; x++) {
        for (uint32_t y = 0; y < m_dimY; y++) {
            for (uint32_t z = 0; z < m_dimZ; z++) {
                VoxelPtr & voxel = m_grid[x][y][z];
                
                if (voxel) {
                
                    const ParticleList & oflowParticles = voxel->getOFlowParticles();
                    
                    BOOST_FOREACH(const ParticlePtr & particle, oflowParticles) {
                        geometry_msgs::Pose pose;
                        
                        pose.position.x = particle->x();
                        pose.position.y = particle->y();
                        pose.position.z = particle->z();
                        
                        const double & vx = particle->vx();
                        const double & vy = particle->vy();
                        const double & vz = particle->vz();
                        
                        const tf::Quaternion & quat = particle->getQuaternion();
                        pose.orientation.w = quat.w();
                        pose.orientation.x = quat.x();
                        pose.orientation.y = quat.y();
                        pose.orientation.z = quat.z();
                        
                        oflowVectors.poses.push_back(pose);
                    }
                }
            }
        }
    }
//     BOOST_FOREACH(const pcl::PointXYZRGBNormal & point, *m_oFlowCloud) {
//         geometry_msgs::Pose pose;
//         
//         pose.position.x = point.x;
//         pose.position.y = point.y;
//         pose.position.z = point.z;
//         
//         const double & vx = point.normal_x;
//         const double & vy = point.normal_y;
//         const double & vz = point.normal_z;
//         
//         const Particle3d particle(point.x, point.y, point.z, vx, vy, vz, m_pose2MapTransform);
//         
//         const tf::Quaternion & quat = particle.getQuaternion();
//         pose.orientation.w = quat.w();
//         pose.orientation.x = quat.x();
//         pose.orientation.y = quat.y();
//         pose.orientation.z = quat.z();
//         
//         oflowVectors.poses.push_back(pose);
//     }
    
    m_oFlowPub.publish(oflowVectors);
}


void VoxelOdometry::publishParticles()
{
    {
        geometry_msgs::PoseArray particles;
        
        particles.header.frame_id = m_mapFrame;
        particles.header.stamp = ros::Time();
            
        BOOST_FOREACH(const VoxelPtr & voxel, m_voxelList) {
            BOOST_FOREACH(const ParticlePtr & particle, voxel->getParticles()) {
                geometry_msgs::Pose pose;
                
                pose.position.x = particle->x();
                pose.position.y = particle->y();
                pose.position.z = particle->z();
                
                const double & vx = particle->vx();
                const double & vy = particle->vy();
                const double & vz = particle->vz();
                
                const tf::Quaternion & quat = particle->getQuaternion();
                pose.orientation.w = quat.w();
                pose.orientation.x = quat.x();
                pose.orientation.y = quat.y();
                pose.orientation.z = quat.z();
                
                particles.poses.push_back(pose);
            }
        }
        
        m_particlesSimplePub.publish(particles);
    }
    
    {
        
        geometry_msgs::PoseArray particles0, particles1, particles2, particles3, particlesD;
        
        particles0.header.frame_id = m_mapFrame;
        particles0.header.stamp = ros::Time();

        particles1.header.frame_id = m_mapFrame;
        particles1.header.stamp = ros::Time();

        particles2.header.frame_id = m_mapFrame;
        particles2.header.stamp = ros::Time();

        particles3.header.frame_id = m_mapFrame;
        particles3.header.stamp = ros::Time();
        
        particlesD.header.frame_id = m_mapFrame;
        particlesD.header.stamp = ros::Time();
        
        BOOST_FOREACH(const VoxelPtr & voxel, m_voxelList) {
            BOOST_FOREACH(const ParticlePtr & particle, voxel->getParticles()) {
                geometry_msgs::Pose pose;
                
                int X, Y, Z;
                
                particleToVoxel(particle, X, Y, Z);
                
                pose.position.x = voxel->centroidX(); // particle->x();
                pose.position.y = voxel->centroidY(); // particle->y();
                pose.position.z = voxel->centroidZ(); // particle->z();
                
                const double & vx = particle->vx();
                const double & vy = particle->vy();
                const double & vz = particle->vz();
                
                float magnitude = cv::norm(cv::Vec3f(vx, vy, vz));
                float maxMagnitude = cv::norm(cv::Vec3f(m_maxVelX, m_maxVelY, m_maxVelZ));
                
                pose.position.z += (m_cellSizeZ / 2.0) - (m_cellSizeZ * (magnitude / maxMagnitude));
                
                if (vx != 0) {
                    pose.position.x += (float)rand()/(float)(RAND_MAX/(m_cellSizeX / 20.0f));
                }
                if (vy != 0) {
                    pose.position.y += (float)rand()/(float)(RAND_MAX/(m_cellSizeX / 20.0f));
                }
                
                const tf::Quaternion & quat = particle->getQuaternion();
                pose.orientation.w = quat.w();
                pose.orientation.x = quat.x();
                pose.orientation.y = quat.y();
                pose.orientation.z = quat.z();
                
                switch (particle->age()) {
                    case 0:
                        particles0.poses.push_back(pose);
                        break;
                    case 1:
                        particles1.poses.push_back(pose);
                        break;
                    case 2:
                        particles2.poses.push_back(pose);
                        break;
                    case 3:
                        particles3.poses.push_back(pose);
                        break;
                    default:
                        particlesD.poses.push_back(pose);
                        break;
                }
            }
        }
        
        if (particles0.poses.size() != 0) m_particles0Pub.publish(particles0);
        if (particles1.poses.size() != 0) m_particles1Pub.publish(particles1);
        if (particles2.poses.size() != 0) m_particles2Pub.publish(particles2);
        if (particles3.poses.size() != 0) m_particles3Pub.publish(particles3);
        if (particlesD.poses.size() != 0) m_particlesDPub.publish(particlesD);
    }
    
    visualization_msgs::MarkerArray particlesCleaners;
    
    for (uint32_t i = 0; i < 1000; i++) {
        for (uint32_t age = 0; age < MAX_PARTICLE_AGE_REPRESENTATION; age++) {
            stringstream ss;
            ss << "age_" << age;
            
            visualization_msgs::Marker voxelMarker;
            voxelMarker.header.frame_id = m_mapFrame;
            voxelMarker.header.stamp = ros::Time();
            voxelMarker.id = i;
            voxelMarker.ns = ss.str();
            voxelMarker.type = visualization_msgs::Marker::ARROW;
            voxelMarker.action = visualization_msgs::Marker::DELETE;
            
            particlesCleaners.markers.push_back(voxelMarker);
        }
    }
    
    m_particlesPub.publish(particlesCleaners);
    
    visualization_msgs::MarkerArray particles;
    
    uint32_t idCount = 0;
    BOOST_FOREACH(const VoxelPtr & voxel, m_voxelList) {
        BOOST_FOREACH(const ParticlePtr & particle, voxel->getParticles()) {
            uint32_t age = particle->age();
            const uint32_t & id = particle->id();
            if (age >= MAX_PARTICLE_AGE_REPRESENTATION)
                age = MAX_PARTICLE_AGE_REPRESENTATION - 1;
            
            visualization_msgs::Marker particleVector;
            particleVector.header.frame_id = m_mapFrame;
            particleVector.header.stamp = ros::Time();
            particleVector.id = idCount++;
            stringstream ss;
            ss << "age_" << age;
            particleVector.ns = ss.str();
            particleVector.type = visualization_msgs::Marker::ARROW;
            particleVector.action = visualization_msgs::Marker::ADD;
            
            particleVector.pose.orientation.x = 0.0;
            particleVector.pose.orientation.y = 0.0;
            particleVector.pose.orientation.z = 0.0;
            particleVector.pose.orientation.w = 1.0;
            particleVector.scale.x = 0.01;
            particleVector.scale.y = 0.03;
            particleVector.scale.z = 0.1;
            particleVector.color.a = 1.0;
//                     particleVector.color.r = m_obstacleColors[age][2];
//                     particleVector.color.g = m_obstacleColors[age][1];
//                     particleVector.color.b = m_obstacleColors[age][0];
            particleVector.color.r = m_obstacleColors[id][2];
            particleVector.color.g = m_obstacleColors[id][1];
            particleVector.color.b = m_obstacleColors[id][0];
            
            //         orientation.lifetime = ros::Duration(5.0);
            
            const double & x = voxel->centroidX(); // particle->x();
            const double & y = voxel->centroidY(); // particle->y();
            const double & z = voxel->centroidZ(); // particle->z();
            const double & vx = particle->vx();
            const double & vy = particle->vy();
            const double & vz = particle->vz();
            
            geometry_msgs::Point origin, dest;
            origin.x = x;
            origin.y = y;
            origin.z = z;
            
            dest.x = x + vx * m_deltaTime;
            dest.y = y + vy * m_deltaTime;
            dest.z = z + vz * m_deltaTime;
            
            particleVector.points.push_back(origin);
            particleVector.points.push_back(dest);
            
            particles.markers.push_back(particleVector);
        }
    }
    
    m_particlesPub.publish(particles);
}

void VoxelOdometry::publishMainVectors()
{
    visualization_msgs::MarkerArray vectorCleaners;
    
    for (uint32_t i = 0; i < MAX_OBSTACLES_VISUALIZATION * 3; i++) {
        visualization_msgs::Marker voxelMarker;
        voxelMarker.header.frame_id = m_mapFrame;
        voxelMarker.header.stamp = ros::Time();
        voxelMarker.id = i;
        voxelMarker.ns = "mainVectors";
        voxelMarker.type = visualization_msgs::Marker::ARROW;
        voxelMarker.action = visualization_msgs::Marker::DELETE;
        
        vectorCleaners.markers.push_back(voxelMarker);
    }
    
    m_mainVectorsPub.publish(vectorCleaners);
    
    visualization_msgs::MarkerArray mainVectors;
    
    uint32_t idCount = 0;
    for (uint32_t x = 0; x < m_dimX; x++) {
        for (uint32_t y = 0; y < m_dimY; y++) {
            for (uint32_t z = 0; z < m_dimZ; z++) {
                const VoxelPtr & voxel = m_grid[x][y][z];
                
                if (voxel && (! voxel->empty())) {
                    
                    visualization_msgs::Marker mainVector;
                    mainVector.header.frame_id = m_mapFrame;
                    mainVector.header.stamp = ros::Time();
                    mainVector.id = idCount++;
                    mainVector.ns = "mainVectors";
                    mainVector.type = visualization_msgs::Marker::ARROW;
                    mainVector.action = visualization_msgs::Marker::ADD;
                    
                    mainVector.pose.orientation.x = 0.0;
                    mainVector.pose.orientation.y = 0.0;
                    mainVector.pose.orientation.z = 0.0;
                    mainVector.pose.orientation.w = 1.0;
                    mainVector.scale.x = 0.01;
                    mainVector.scale.y = 0.03;
                    mainVector.scale.z = 0.1;
                    
                    cv::Vec3f color(voxel->vx(), voxel->vy(), voxel->vz());
                    if (cv::norm(color) != 0.0) {
                        color = color / cv::norm(color);

                        mainVector.color.r = fabs(color[0]);
                        mainVector.color.g = fabs(color[1]);
                        mainVector.color.b = fabs(color[2]);
                    } else {
                        mainVector.color.r = (double)rand() / RAND_MAX;
                        mainVector.color.g = (double)rand() / RAND_MAX;
                        mainVector.color.b = (double)rand() / RAND_MAX;
                    }
                    mainVector.color.a = 1.0;
                    
                    mainVector.color.r = 0.0;
                    mainVector.color.g = 0.0;
                    mainVector.color.b = 0.0;
                    
                    //         orientation.lifetime = ros::Duration(5.0);
                    
                    geometry_msgs::Point origin, dest;
                    origin.x = voxel->centroidX();
                    origin.y = voxel->centroidY();
                    origin.z = voxel->centroidZ();
                    
//                     cout << cv::Vec4f(voxel->vx(), voxel->vy(), voxel->vz(), voxel->magnitude()) << endl;
                    
//                     dest.x = voxel->centroidX() + voxel->vx() * m_deltaTime * 5.0;
//                     dest.y = voxel->centroidY() + voxel->vy() * m_deltaTime * 5.0;
//                     dest.z = voxel->centroidZ() + voxel->vz() * m_deltaTime * 5.0;
                    
                    dest.x = voxel->centroidX() + voxel->vx() * voxel->magnitude();
                    dest.y = voxel->centroidY() + voxel->vy() * voxel->magnitude();
                    dest.z = voxel->centroidZ() + voxel->vz() * voxel->magnitude();
                    
                    mainVector.points.push_back(origin);
                    mainVector.points.push_back(dest);
                    
                    mainVectors.markers.push_back(mainVector);
                }
            }
        }
    }
    
    m_mainVectorsPub.publish(mainVectors);
    
}

void VoxelOdometry::publishObstacles()
{
    visualization_msgs::MarkerArray voxelCleaners;
    
    for (uint32_t i = 0; i < MAX_OBSTACLES_VISUALIZATION * 3; i++) {
        visualization_msgs::Marker voxelMarker;
        voxelMarker.header.frame_id = m_mapFrame;
        voxelMarker.header.stamp = ros::Time();
        voxelMarker.id = i;
        voxelMarker.ns = "obstacles";
        voxelMarker.type = visualization_msgs::Marker::CUBE;
        voxelMarker.action = visualization_msgs::Marker::DELETE;
        
        voxelCleaners.markers.push_back(voxelMarker);
    }
    
    m_obstaclesPub.publish(voxelCleaners);
    
    visualization_msgs::MarkerArray voxelMarkers;

    uint32_t idCount = 0;
    
    BOOST_FOREACH(const VoxelObstaclePtr & obstacle, m_obstacles) {
        const VoxelList & voxels = obstacle->voxels();
        visualization_msgs::Marker::_color_type color;
        color.r = (obstacle->vx() / m_maxVelX + 1.0) * 0.5f;
        color.g = (obstacle->vy() / m_maxVelY + 1.0) * 0.5f;
        color.b = 0.0;
        color.a = 0.5; //(0.8 - (0.4 * obstacle->magnitude() / m_maxMagnitude));
        
        if (obstacle->magnitude() == 0.0) {
            color.r = color.g = color.b = color.a = 0.0;
        }
        
//         cout << cv::Vec3f(obstacle->vx(), obstacle->vy(), obstacle->vz()) << " => " 
//             << cv::Vec3f((obstacle->vx() / m_maxVelX + 1.0) * 0.5f, 
//                         (obstacle->vy() / m_maxVelY + 1.0) * 0.5f, 0.0) << endl;
                        
        BOOST_FOREACH(const VoxelPtr & voxel, voxels) {
            visualization_msgs::Marker voxelMarker;
            voxelMarker.header.frame_id = m_mapFrame;
            voxelMarker.header.stamp = ros::Time();
            voxelMarker.id = idCount++;
            voxelMarker.ns = "obstacles";
            voxelMarker.type = visualization_msgs::Marker::CUBE;
            voxelMarker.action = visualization_msgs::Marker::ADD;
            
            voxelMarker.pose.position.x = voxel->centroidX();
            voxelMarker.pose.position.y = voxel->centroidY();
            voxelMarker.pose.position.z = voxel->centroidZ();
            
            voxelMarker.pose.orientation.x = 0.0;
            voxelMarker.pose.orientation.y = 0.0;
            voxelMarker.pose.orientation.z = 0.0;
            voxelMarker.pose.orientation.w = 1.0;
            voxelMarker.scale.x = m_cellSizeX;
            voxelMarker.scale.y = m_cellSizeY;
            voxelMarker.scale.z = m_cellSizeZ;
            
            voxelMarker.color = color;
            
            voxelMarkers.markers.push_back(voxelMarker);
        }
    }
    
    m_obstaclesPub.publish(voxelMarkers);
}

void VoxelOdometry::publishObstacleCubes()
{
    visualization_msgs::MarkerArray obstacleCubesCleaners;
    
    for (uint32_t i = 0; i < 1000; i++) {
        visualization_msgs::Marker obstacleCubeMarker;
        obstacleCubeMarker.header.frame_id = m_mapFrame;
        obstacleCubeMarker.header.stamp = ros::Time();
        obstacleCubeMarker.id = i;
        obstacleCubeMarker.ns = "obstacleCubes";
        obstacleCubeMarker.type = visualization_msgs::Marker::CUBE;
        obstacleCubeMarker.action = visualization_msgs::Marker::DELETE;
        
        obstacleCubesCleaners.markers.push_back(obstacleCubeMarker);
    }
    m_obstacleCubesPub.publish(obstacleCubesCleaners);
    obstacleCubesCleaners.markers.clear();
    for (uint32_t i = 0; i < 1000; i++) {
        visualization_msgs::Marker obstacleCubeMarker;
        obstacleCubeMarker.header.frame_id = m_mapFrame;
        obstacleCubeMarker.header.stamp = ros::Time();
        obstacleCubeMarker.id = i;
        
        obstacleCubeMarker.ns = "speedVector";
        obstacleCubeMarker.type = visualization_msgs::Marker::ARROW;
        obstacleCubeMarker.action = visualization_msgs::Marker::DELETE;
        
        obstacleCubesCleaners.markers.push_back(obstacleCubeMarker);
    }
    m_obstacleCubesPub.publish(obstacleCubesCleaners);
   
    obstacleCubesCleaners.markers.clear();
    for (uint32_t i = 0; i < 1000; i++) {
        visualization_msgs::Marker obstacleCubeMarker;
        obstacleCubeMarker.header.frame_id = m_mapFrame;
        obstacleCubeMarker.header.stamp = ros::Time();
        obstacleCubeMarker.id = i;
        
        obstacleCubeMarker.ns = "speedText";
        obstacleCubeMarker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        obstacleCubeMarker.action = visualization_msgs::Marker::DELETE;
        
        obstacleCubesCleaners.markers.push_back(obstacleCubeMarker);
    }
    m_obstacleCubesPub.publish(obstacleCubesCleaners);
    
    visualization_msgs::MarkerArray obstacleCubeMarkers;
    visualization_msgs::MarkerArray obstacleSpeedMarkers;
    visualization_msgs::MarkerArray obstacleSpeedTextMarkers;
    
    uint32_t idCount = 0;
    
    for (uint32_t i = 0; i < m_obstacles.size(); i++) {
        const VoxelObstaclePtr & obstacle = m_obstacles[i];
        
        visualization_msgs::Marker obstacleCubeMarker;
        obstacleCubeMarker.header.frame_id = m_mapFrame;
        obstacleCubeMarker.header.stamp = ros::Time();
        obstacleCubeMarker.id = idCount++;
        obstacleCubeMarker.ns = "obstacleCubes";
        obstacleCubeMarker.type = visualization_msgs::Marker::CUBE;
        obstacleCubeMarker.action = visualization_msgs::Marker::ADD;
        
        obstacleCubeMarker.pose.position.x = obstacle->centerX();
        obstacleCubeMarker.pose.position.y = obstacle->centerY();
        obstacleCubeMarker.pose.position.z = obstacle->centerZ();
        
        obstacleCubeMarker.pose.orientation.x = 0.0;
        obstacleCubeMarker.pose.orientation.y = 0.0;
        obstacleCubeMarker.pose.orientation.z = 0.0;
        obstacleCubeMarker.pose.orientation.w = 1.0;
        obstacleCubeMarker.scale.x = 2 * max(fabs(obstacle->maxX() - obstacle->centerX()),
                                             fabs(obstacle->minX() - obstacle->centerX()));
        obstacleCubeMarker.scale.y = 2 * max(fabs(obstacle->maxY() - obstacle->centerY()),
                                             fabs(obstacle->minY() - obstacle->centerY()));
        obstacleCubeMarker.scale.z = 2 * max(fabs(obstacle->maxZ() - obstacle->centerZ()),
                                             fabs(obstacle->minZ() - obstacle->centerZ()));
        obstacleCubeMarker.color.r = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][0];
        obstacleCubeMarker.color.g = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][1];
        obstacleCubeMarker.color.b = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][2];
        obstacleCubeMarker.color.a = 0.4;
        
        obstacleCubeMarkers.markers.push_back(obstacleCubeMarker);
        
        // ********************************************************
        // Publication of the speed of the obstacle
        // ********************************************************
        
        visualization_msgs::Marker speedVector;
        speedVector.header.frame_id = m_mapFrame;
        speedVector.header.stamp = ros::Time();
        speedVector.id = idCount;
        speedVector.ns = "speedVector";
        speedVector.type = visualization_msgs::Marker::ARROW;
        speedVector.action = visualization_msgs::Marker::ADD;
        
        speedVector.pose.orientation.x = 0.0;
        speedVector.pose.orientation.y = 0.0;
        speedVector.pose.orientation.z = 0.0;
        speedVector.pose.orientation.w = 1.0;
        speedVector.scale.x = 0.01;
        speedVector.scale.y = 0.03;
        speedVector.scale.z = 0.1;
        speedVector.color.a = 1.0;
        speedVector.color.r = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][0];
        speedVector.color.g = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][1];
        speedVector.color.b = m_obstacleColors[i % MAX_OBSTACLES_VISUALIZATION][2];
        
        //         orientation.lifetime = ros::Duration(5.0);
        
        geometry_msgs::Point origin, dest;
        origin.x = obstacle->centerX();
        origin.y = obstacle->centerY();
        origin.z = obstacle->centerZ();        

        // NOTE: This makes vectors longer than they actually are. 
        // For a realistic visualization, multiply by m_deltaTime
//         dest.x = obstacle.centerX() + obstacle.vx();
//         dest.y = obstacle.minY() + obstacle.vy();
//         dest.z = obstacle.centerZ() + obstacle.vz();
        
        dest.x = obstacle->centerX() + obstacle->vx() * obstacle->magnitude() * 5.0;
        dest.y = obstacle->centerY() + obstacle->vy() * obstacle->magnitude() * 5.0;
        dest.z = obstacle->centerZ() + obstacle->vz() * obstacle->magnitude() * 5.0;

//         dest.x = obstacle->centerX() + obstacle->vx() / obstacle->numVoxels() * m_deltaTime;
//         dest.y = obstacle->minY() + obstacle->vy() / obstacle->numVoxels() * m_deltaTime;
//         dest.z = obstacle->centerZ() + obstacle->vz() / obstacle->numVoxels() * m_deltaTime;
        
        speedVector.points.push_back(origin);
        speedVector.points.push_back(dest);
        
        obstacleSpeedMarkers.markers.push_back(speedVector);
        
        // ********************************************************
        // Publication of the speed text of the obstacle
        // ********************************************************
        
        visualization_msgs::Marker speedTextVector;
        speedTextVector.header.frame_id = m_mapFrame;
        speedTextVector.header.stamp = ros::Time();
        speedTextVector.id = idCount;
        speedTextVector.ns = "speedText";
        speedTextVector.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        speedTextVector.action = visualization_msgs::Marker::ADD;
        
        speedTextVector.pose.position.x = obstacle->centerX();
        speedTextVector.pose.position.y = obstacle->centerY();
        speedTextVector.pose.position.z = obstacle->maxZ() + (m_cellSizeZ / 2.0);
        
        speedTextVector.pose.orientation.x = 0.0;
        speedTextVector.pose.orientation.y = 0.0;
        speedTextVector.pose.orientation.z = 0.0;
        speedTextVector.pose.orientation.w = 1.0;
        speedTextVector.scale.z = 0.25;
        speedTextVector.color.a = 1.0;
        speedTextVector.color.r = 0.0;
        speedTextVector.color.g = 1.0;
        speedTextVector.color.b = 0.0;
        
        const double speedInKmH = obstacle->magnitude() * 3.6;
        stringstream ss;
        ss << m_currentId << " => " << std::setprecision(3) << speedInKmH << " Km/h" << " - " << obstacle->idx() << endl;//obstacle->winnerNumberOfParticles();
        speedTextVector.text = ss.str();
                
        obstacleSpeedTextMarkers.markers.push_back(speedTextVector);
    }
    
    m_obstacleCubesPub.publish(obstacleCubeMarkers);
    m_obstacleSpeedPub.publish(obstacleSpeedMarkers);
    m_obstacleSpeedTextPub.publish(obstacleSpeedTextMarkers);
}

tf::Quaternion getQuaternion(const double &vx, const double &vy, const double &vz)
{
    if (vx == vy == vz == 0.0) {
        return tf::Quaternion(0.0, 0.0, 0.0, 0.0);
    }
    
    Eigen::Vector3d zeroVector, currVector;
    zeroVector << 1.0, 0.0, 0.0;
    currVector << vx, vy, vz;
    currVector.normalize();
    Eigen::Quaterniond eigenQuat;
    eigenQuat.setFromTwoVectors(zeroVector, currVector);
    
    tf::Quaternion quat(eigenQuat.x(), eigenQuat.y(), eigenQuat.z(), eigenQuat.w());
    
    return quat;
}

void VoxelOdometry::publishFakePointCloud()
{
    
    PointCloudPtr fakePointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    geometry_msgs::PoseArray fakeParticles;
    
    BOOST_FOREACH(const VoxelObstaclePtr & obstacle, m_obstacles) {
        
//         if ((obstacle->minZ() - (m_cellSizeZ / 2.0)) == m_minZ) {
        if (obstacle->numVoxels() > 1) {
            const double tColission = 3.0;
            const double deltaTime = 0.3;
            
            BOOST_FOREACH(const VoxelPtr & voxel, obstacle->voxels()) {
                pcl::PointXYZRGB currPoint;
                currPoint.x = voxel->centroidX();
                currPoint.y = voxel->centroidY();
                currPoint.z = voxel->centroidZ();
                    
                currPoint.r = 255.0;
                currPoint.g = 0.0;
                currPoint.b = 0.0;
                
                fakePointCloud->push_back(currPoint);
                
                geometry_msgs::Pose currPose;
                currPose.position.x = voxel->centroidX();
                currPose.position.y = voxel->centroidY();
                currPose.position.z = voxel->centroidZ();
                
                const double & vx = obstacle->vx();
                const double & vy = obstacle->vy();
                const double & vz = obstacle->vz();
                
                const tf::Quaternion & quat = getQuaternion(vx, vy, vz);
                currPose.orientation.w = quat.w();
                currPose.orientation.x = quat.x();
                currPose.orientation.y = quat.y();
                currPose.orientation.z = quat.z();
                
                fakeParticles.poses.push_back(currPose);
                
                fakePointCloud->push_back(currPoint);   
                for (double t = 0; t <= tColission; t += deltaTime) {
//                     double t = 1.0;

                    pcl::PointXYZRGB newPoint;
                    newPoint.x = currPoint.x + obstacle->vx() * t;
                    newPoint.y = currPoint.y + obstacle->vy() * t;
                    newPoint.z = currPoint.z + obstacle->vz() * t;
                    newPoint.r = 255.0;
                    newPoint.g = 0.0;
                    newPoint.b = 0.0;
                    
                    fakePointCloud->push_back(newPoint);   
                    
                    geometry_msgs::Pose newPose;
                    newPose.position.x = currPoint.x + obstacle->vx() * t;
                    newPose.position.y = currPoint.y + obstacle->vy() * t;
                    newPose.position.z = currPoint.z + obstacle->vz() * t;
                    
                    const double & vx = obstacle->vx();
                    const double & vy = obstacle->vy();
                    const double & vz = obstacle->vz();
                    
                    const tf::Quaternion & quat = getQuaternion(vx, vy, vz);
                    newPose.orientation.w = quat.w();
                    newPose.orientation.x = quat.x();
                    newPose.orientation.y = quat.y();
                    newPose.orientation.z = quat.z();
                    
                    fakeParticles.poses.push_back(newPose);
                }
            }
        }
    }
        
    fakeParticles.header.frame_id = m_mapFrame;
    fakeParticles.header.stamp = ros::Time();
    
    sensor_msgs::PointCloud2 cloudMsg;
    pcl::toROSMsg (*fakePointCloud, cloudMsg);
    cloudMsg.header.frame_id = m_mapFrame;
    cloudMsg.header.stamp = ros::Time::now();
    cloudMsg.header.seq = m_currentId;
    
    m_fakePointCloudPub.publish(cloudMsg);
    
    m_fakeParticlesPub.publish(fakeParticles);
}

void VoxelOdometry::publishOdom()
{
    if (m_obstacles.size() == 0)
        return;
    
    const VoxelObstaclePtr & obst = m_obstacles[0];
    
    double vx = -obst->vx();
    double vy = -obst->vy();
    double vz = -obst->vz();

    tf::Quaternion tmpQuat = getQuaternion(vx, vy, vz);
    
//     double angleDiff = atan2(sin(m_currTheta - tmpQuat.getAngle()), cos(m_currTheta - tmpQuat.getAngle()));
//     if (m_currTheta == std::numeric_limits<double>::infinity())
//         angleDiff = 0.0;
    
//     cout << (m_currTheta / CV_PI * 180.0) << " - " << (tmpQuat.getAngle() / CV_PI * 180.0) << " = " << (angleDiff / CV_PI * 180.0) << endl;
//     m_currTheta = (m_currTheta + 2.0 * tmpQuat.getAngle()) / 3.0;
    
//     if (angleDiff > CV_PI / 4.0)
//         return;
    
    
    if ((vx < 0.3) && (vy < 0.3))
        vx=vy=vz=0.0;
    
    m_currX += vx * m_deltaTime;
    m_currY += vy * m_deltaTime;
    m_currTheta = tmpQuat.getAngle();
    
    geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(m_currTheta);
    
    cout << "PUBLISHING" << endl;
    //first, we'll publish the transform over tf
    geometry_msgs::TransformStamped odom_trans;
    odom_trans.header.stamp = m_lastPointCloudTime;
    odom_trans.header.frame_id = "odom";
    odom_trans.child_frame_id = m_poseFrame;
    
    odom_trans.transform.translation.x = m_currX;
    odom_trans.transform.translation.y = m_currY;
    odom_trans.transform.translation.z = 0.0;
    odom_trans.transform.rotation = odom_quat;
    
    //send the transform
//     m_odomBroadcaster.sendTransform(odom_trans);
    
    //next, we'll publish the odometry message over ROS
    nav_msgs::Odometry odom;
    odom.header.stamp = m_lastPointCloudTime;
    odom.header.frame_id = "odom";
    
    //set the position
    odom.pose.pose.position.x = m_currX;
    odom.pose.pose.position.y = m_currY;
    odom.pose.pose.position.z = 0.0;
    odom.pose.pose.orientation = odom_quat;
    
    odom.pose.covariance.assign(0.1f);
    
    //set the velocity
    odom.child_frame_id = m_poseFrame;
    odom.twist.twist.linear.x = vx;
    odom.twist.twist.linear.y = vy;
    odom.twist.twist.angular.z = tmpQuat.getAngle() / m_deltaTime;
    
    odom.twist.covariance.assign(0.1f);
    
    //publish the message
    m_odomPub.publish(odom);
}


}