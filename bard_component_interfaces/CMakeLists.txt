#
# The find_package macro for Orocos-RTT works best with
# cmake >= 2.6.3
#
cmake_minimum_required(VERSION 2.6.3)

#
# This creates a standard cmake project. You may extend this file with
# any cmake macro you see fit.
#
project(bard_component_interfaces)

find_package(Eigen REQUIRED)
include_directories(${EIGEN_INCLUDE_DIRS})
add_definitions(${EIGEN_DEFINITIONS})


#
# Do setup in case of ros package, If ROS_ROOT is set, it is
# recommended to use RTT/OCL through the ros packages.
#
set (ROS_ROOT $ENV{ROS_ROOT} )
if (ROS_ROOT)
  include($ENV{ROS_ROOT}/core/rosbuild/rosbuild.cmake)
  rosbuild_init()
  rosbuild_find_ros_package( rtt )
  set( RTT_HINTS HINTS ${rtt_PACKAGE_PATH}/install )
endif()

# Set the CMAKE_PREFIX_PATH in case you're not using Orocos through ROS
# for helping these find commands find RTT.
find_package(OROCOS-RTT REQUIRED ${RTT_HINTS})

# Defines the orocos_* cmake macros. See that file for additional
# documentation.
include(${OROCOS-RTT_USE_FILE_PATH}/UseOROCOS-RTT.cmake)

#
# Components, types and plugins.
#
# The CMake 'target' names are identical to the first argument of the
# macros below, except for orocos_typegen_headers, where the target is fully
# controlled by generated code of 'typegen'.
#

# Creates a component library libbard_component_interfaces-<target>.so
# and installs in the directory lib/orocos/bard_component_interfaces/
#orocos_component(components
#  ) # ...you may add multiple source files
# You may add multiple orocos_component statements.

# Additional headers:
# Installs in the include/orocos/bard_component_interfaces/ directory
orocos_install_headers(
  include/bard_component_interfaces/wam_interface.h 
  ) # ...you may add multiple header files
# You may add multiple orocos_install_headers statements.

# Building a Plugin
# Creates a plugin library libbard_component_interfaces-plugin-<target>.so
# and installs in the directory lib/orocos/bard_component_interfaces/plugins/
# Be aware that a plugin may only have the loadRTTPlugin() function once defined in a .cpp file.
# This function is defined by the plugin and service CPP macros.
##orocos_plugin(bard_component_interfaces-plugin bard_component_interfaces-plugin.cpp) # ...only one plugin function per library !
# You may add multiple orocos_plugin statements.


# Building a Service:
# Creates a plugin library libbard_component_interfaces-service-<target>.so
# and installs in the directory lib/orocos/bard_component_interfaces/plugins/
##orocos_service(bard_component_interfaces-service bard_component_interfaces-service.cpp) # ...only one service per library !
# You may add multiple orocos_service statements.


# Building a typekit (recommended):
# Creates a typekit library libbard_component_interfaces-types-<target>.so
# and installs in the directory lib/orocos/bard_component_interfaces/types/
##orocos_typegen_headers(bard_component_interfaces-types.hpp) # ...you may add multiple header files
# You may only have *ONE* orocos_typegen_headers statement !


# Building a normal library (optional):
# Creates a library libsupport-<target>.so and installs it in
# lib/
##orocos_library(support support.cpp) # ...you may add multiple source files
# You may add multiple orocos_library statements.


# Generates and installs our package. Must be the last statement such
# that it can pick up all above settings.
orocos_generate_package()
