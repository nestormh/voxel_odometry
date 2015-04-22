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


#ifndef UTILS_H
#define UTILS_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <opencv2/opencv.hpp>
#include <Eigen/Core>
// #include <chrono>
#include <time.h>
// clock_t uptime = clock() / (CLOCKS_PER_SEC / 1000);

// #include "polargridtracking.h"
#include <image_geometry/stereo_camera_model.h>
#include <tf/transform_datatypes.h>

#include <geometry_msgs/Point32.h>
#include <voxel_odometry/point_2d.h>

#include "params_structs.h"

using namespace std;

#define IS_INBOUND_AND(x, minX, maxX) ((x >= minX) && (x <= maxX))
#define IS_INBOUND_AND_3D(x, minX, maxX, y, minY, maxY, z, minZ, maxZ) \
    IS_INBOUND_AND(x, minX, maxX) && \
    IS_INBOUND_AND(y, minY, maxY) && \
    IS_INBOUND_AND(z, minZ, maxZ)
    
#define LOG_BASE(base, x) log10(x) / log10(base)

#define INIT_CLOCK(start) clock_t start = clock();
#define RESET_CLOCK(start) start = clock();
#define END_CLOCK(time, start) float time = (clock() - start) / (float)(CLOCKS_PER_SEC);
#define END_CLOCK_2(time, start) time = (clock() - start) / (float)(CLOCKS_PER_SEC);

// #define INIT_CLOCK(start) auto start = std::chrono::high_resolution_clock::now();
// #define RESET_CLOCK(start) start = std::chrono::high_resolution_clock::now();
// #define END_CLOCK(time, start) float time = std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::high_resolution_clock::now()-start).count();
// #define END_CLOCK_2(time, start) time = std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::high_resolution_clock::now()-start).count();
// typedef Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> BinaryMap;

// cv::Mat getCvMatFromEigenBinary(const voxel_odometry::BinaryMap & map);
// cv::Mat getCvMatFromProbabilityMap(/*const*/ voxel_odometry::CellGrid & map);
double calculateDifferenceBetweenAngles(const double & ang1, const double & ang2);
void project3dTo2d(const pcl::PointXYZRGB & point3d, pcl::PointXYZRGB & point2d, 
                   const voxel_odometry::t_Camera_params & cameraParams);
void project3dTo2d(const pcl::PointXYZRGB & point3d, pcl::PointXYZRGB & point2d, 
                   const image_geometry::StereoCameraModel & stereoCameraModel,
                   const tf::StampedTransform & map2CamTransform);
void project3dTo2d(const pcl::PointXYZRGB & point3d, pcl::PointXYZRGB & point2d, 
                   const image_geometry::StereoCameraModel & stereoCameraModel);
geometry_msgs::Point32 toPoint32(const pcl::PointXYZRGB & point);
voxel_odometry::point_2d toPoint2D(const pcl::PointXYZRGB & point);


// From http://stackoverflow.com/questions/16451111/cvmat-conversion-to-eigen-matrix-and-back
template<typename _Tp, int _rows, int _cols, int _options, int _maxRows, int _maxCols>
void eigen2cv(const Eigen::Matrix<_Tp, _rows, _cols, _options, _maxRows, _maxCols>& src, cv::Mat& dst)
{
    if( !(src.Flags & Eigen::RowMajorBit) )
    {
        cv::Mat _src(src.cols(), src.rows(), cv::DataType<_Tp>::type,
                 (void*)src.data(), src.stride()*sizeof(_Tp));
        cv::transpose(_src, dst);
    }
    else
    {
        cv::Mat _src(src.rows(), src.cols(), cv::DataType<_Tp>::type,
                 (void*)src.data(), src.stride()*sizeof(_Tp));
        _src.copyTo(dst);
    }
}

// From http://stackoverflow.com/questions/16451111/cvmat-conversion-to-eigen-matrix-and-back
template<typename _Tp, int _rows, int _cols, int _options, int _maxRows, int _maxCols>
void cv2eigen( const cv::Mat& src,
               Eigen::Matrix<_Tp, _rows, _cols, _options, _maxRows, _maxCols>& dst )
{
    CV_DbgAssert(src.rows == _rows && src.cols == _cols);
    if( !(dst.Flags & Eigen::RowMajorBit) )
    {
        cv::Mat _dst(src.cols, src.rows, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        if( src.type() == _dst.type() )
            cv::transpose(src, _dst);
        else if( src.cols == src.rows )
        {
            src.convertTo(_dst, _dst.type());
            transpose(_dst, _dst);
        }
        else
            cv::Mat(src.t()).convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
    else
    {
        cv::Mat _dst(src.rows, src.cols, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        src.convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
}

// From http://stackoverflow.com/questions/16451111/cvmat-conversion-to-eigen-matrix-and-back
template<typename _Tp>
void cv2eigen( const cv::Mat& src,
               Eigen::Matrix<_Tp, Eigen::Dynamic, Eigen::Dynamic>& dst )
{
    dst.resize(src.rows, src.cols);
    if( !(dst.Flags & Eigen::RowMajorBit) )
    {
        cv::Mat _dst(src.cols, src.rows, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        if( src.type() == _dst.type() )
            cv::transpose(src, _dst);
        else if( src.cols == src.rows )
        {
            src.convertTo(_dst, _dst.type());
            cv::transpose(_dst, _dst);
        }
        else
            cv::Mat(src.t()).convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
    else
    {
        cv::Mat _dst(src.rows, src.cols, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        src.convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
}

// From http://stackoverflow.com/questions/16451111/cvmat-conversion-to-eigen-matrix-and-back
template<typename _Tp>
void cv2eigen( const cv::Mat& src,
               Eigen::Matrix<_Tp, Eigen::Dynamic, 1>& dst )
{
    CV_Assert(src.cols == 1);
    dst.resize(src.rows);
    
    if( !(dst.Flags & Eigen::RowMajorBit) )
    {
        cv::Mat _dst(src.cols, src.rows, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        if( src.type() == _dst.type() )
            cv::transpose(src, _dst);
        else
            cv::Mat(src.t()).convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
    else
    {
        cv::Mat _dst(src.rows, src.cols, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        src.convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
}

// From http://stackoverflow.com/questions/16451111/cvmat-conversion-to-eigen-matrix-and-back
template<typename _Tp>
void cv2eigen( const cv::Mat& src,
               Eigen::Matrix<_Tp, 1, Eigen::Dynamic>& dst )
{
    CV_Assert(src.rows == 1);
    dst.resize(src.cols);
    if( !(dst.Flags & Eigen::RowMajorBit) )
    {
        cv::Mat _dst(src.cols, src.rows, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        if( src.type() == _dst.type() )
            cv::transpose(src, _dst);
        else
            cv::Mat(src.t()).convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
    else
    {
        cv::Mat _dst(src.rows, src.cols, cv::DataType<_Tp>::type,
                 dst.data(), (size_t)(dst.stride()*sizeof(_Tp)));
        src.convertTo(_dst, _dst.type());
        CV_DbgAssert(_dst.data == (uchar*)dst.data());
    }
}

#endif // UTILS_H