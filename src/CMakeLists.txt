###########################################################
# voxel_odometry
###########################################################
add_executable(voxel_odometry 
    voxelobstacle.cpp 
    utilspolargridtracking.cpp
    voxel.cpp 
    particle3d.cpp
    voxel_odometry.cpp
    main_voxel.cpp
)

include_directories(voxel_odometry
    ${PCL_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    /opt/ros/indigo/include
    /home/nestor/Dropbox/ros/indigo/nestormh/devel/include
)

link_directories(voxel_odometry
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu/
)

target_link_libraries(voxel_odometry
  ${EIGEN3_LIBRARIES}
  ${PCL_LIBRARIES}
  ${OpenCV_LIBS}
  ${Boost_LIBRARIES}
  ${catkin_LIBRARIES}
)