
#include <iostream>
#include <map>

#include <Eigen/Dense>

#include <kdl/jntarray.hpp>
#include <kdl/jntarrayvel.hpp>
#include <kdl/tree.hpp>
#include <kdl/chain.hpp>

#include <kdl_parser/kdl_parser.hpp>

#include <bard_components/util.h>
#include <bard_components/controller_mux.h>

using namespace bard_components;

ControllerMux::ControllerMux(std::string const& name) :
  RTT::TaskContext(name)
  // RTT properties
  ,robot_description_("")
  ,root_link_("")
  ,tip_link_("")
  ,joint_state_throttle_period_(0.1)
  // Internal members
  ,n_dof_(0)
  ,controller_torques_()
  ,positions_()
  ,torques_()
  ,enabled_(true)
  ,config_cmd_()
  ,joint_state_()
  ,joint_state_throttle_(joint_state_throttle_period_)
{

  // Declare properties
  this->addProperty("robot_description",robot_description_)
     .doc("The WAM URDF xml string.");
  this->addProperty("root_link",root_link_)
    .doc("The root link for the controller.");
  this->addProperty("tip_link",tip_link_)
    .doc("The tip link for the controller.");
  this->addProperty("joint_state_throttle_period",joint_state_throttle_period_)
    .doc("The period of the ROS sensor_msgs/JointState publisher.");

  // Configure RTT ports
  this->ports()->addEventPort("config_input", config_input_)
    .doc("Input Event port: nx1 vector of joint torques. (n joints)");
  this->ports()->addPort("state_output", state_output_)
    .doc("Output port: nx1 vector of joint positions. (n joints)");

  this->ports()->addPort("positions_in", positions_in_port_)
    .doc("Input port: nx1 vector of joint positions. (n joints)");
  this->ports()->addPort("joint_state_out", joint_state_out_port_)
    .doc("Output port: sensor_msgs/JointState of commanded joint state.");
  this->ports()->addPort("torques_out", torques_out_port_)
    .doc("Output port: nx1 vector of joint torques. (n joints)");

  // Configure operations
  this->addOperation("load", &ControllerMux::load_controller, this, RTT::OwnThread)
    .doc("Add a controller to the controller mux.")
    .arg("name","Name of controller to load.")
    .arg("dof","Number of degrees-of-freedom that the control outputs");

  this->addOperation("unload", &ControllerMux::unload_controller, this, RTT::OwnThread)
    .doc("Remove a controller from the controller mux.")
    .arg("name","Name of controller to unload.");

  this->addOperation("enable", &ControllerMux::enable, this, RTT::OwnThread)
    .doc("Enable multiplexer (output non-zero torques).");

  this->addOperation("disable", &ControllerMux::disable, this, RTT::OwnThread)
    .doc("Disable multiplexer (output zero torques).");

  this->addOperation("toggleControllers", &ControllerMux::toggle_controllers, this, RTT::OwnThread)
    .doc("Enable and disable controllers by name.")
    .arg("enable_controllers","Array of names of controllers to enable.")
    .arg("disable_controllers","Array of names of controllers to disable.");
}

bool ControllerMux::configureHook()
{
  // Initialize kinematics (KDL tree, KDL chain, and #DOF)
  if(!bard_components::util::initialize_kinematics_from_urdf(
        robot_description_, root_link_, tip_link_,
        kdl_tree_, kdl_chain_, n_dof_))
  {
    ROS_ERROR("Could not initialize robot kinematics!");
    return false;
  }

  // Initialize joint arrays
  torques_.resize(n_dof_);
  positions_.resize(n_dof_);
  
  // Construct ros JointState message with the appropriate joint names
  bard_components::util::joint_state_from_kdl_chain(kdl_chain_, joint_state_);

  // Prepare data samples
  torques_out_port_.setDataSample(torques_);
  joint_state_out_port_.setDataSample(joint_state_);

  return true;
}

bool ControllerMux::startHook()
{
  std::cerr<<"Starting controller mux!"<<std::endl;
  return true;
}

void ControllerMux::updateHook()
{
  // Check configure input for a new configure command
  if( config_input_.read( config_cmd_ ) == RTT::NewData ) {
    // Update the properties of the controllers referenced in the command
  }
  
  // Read in the current joint positions
  positions_in_port_.read( positions_ );

  // Zero out the output torques
  torques_.data.setZero();
 
  // Read in all control inputs
  for(ControllerInterface_iter it = controller_interfaces_.begin();
      it != controller_interfaces_.end();
      it++) 
  {
    // Combine control inputs based on gains
    if( it->second->enabled ) {
      // Read input from this controller
      if(it->second->in_port.read(controller_torques_) == RTT::NewData) {
        // Add this control input to the output torques
        for(unsigned int i=0; i < it->second->dof && i < n_dof_; i++) {
          torques_(i) += controller_torques_(i);
        }
      }
    }
  }

  // Only send non-zero torques if enabled
  if(enabled_) {
    torques_out_port_.write( torques_ );
  } else {
    KDL::JntArray zero_array(n_dof_);
    zero_array.data.setZero();
    torques_out_port_.write( zero_array ); 
  }
  
  // Copy the command into a sensor_msgs/JointState message
  if( joint_state_throttle_.ready(joint_state_throttle_period_)) {
    joint_state_.header.stamp = ros::Time::now();
    for(unsigned int i=0; i<n_dof_; i++) {
      joint_state_.position[i] = positions_.q(i);
      joint_state_.velocity[i] = positions_.qdot(i);
      joint_state_.effort[i] = torques_(i);
    }
    joint_state_out_port_.write( joint_state_ );
  } 
}

void ControllerMux::stopHook()
{

}

void ControllerMux::cleanupHook()
{
  // Unload all controllers
  for(ControllerInterface_iter it = controller_interfaces_.begin();
      it != controller_interfaces_.end(); it++) 
  {
    std::cerr<<"Deleting controller interface port for "<<it->first<<std::endl;
    it->second->in_port.disconnect();
    this->ports()->removePort(it->first);
    delete it->second;
  }
  controller_interfaces_.clear();
}

void ControllerMux::enable()
{
  enabled_ = true;
}

void ControllerMux::disable()
{
  enabled_ = false;
}

void ControllerMux::load_controller(std::string name, int dof)
{
  // Create a controller interface
  ControllerInterface *interface  = new ControllerInterface();
  interface->dof = dof;
  interface->enabled = false;
  // Add this interface port to the task
  this->ports()->addPort(name, interface->in_port).doc("Input torques from controller \""+name+"\"");
  // Add interface to the map of controller interfaces
  controller_interfaces_[name]=interface;
}

void ControllerMux::unload_controller(std::string name)
{
  if(controller_interfaces_.find(name) != controller_interfaces_.end()) {
    delete controller_interfaces_.find(name)->second;
    controller_interfaces_.erase(name);
  }
}

void ControllerMux::toggle_controllers(
    std::vector<std::string> enable_controllers, 
    std::vector<std::string> disable_controllers) 
{
  // Enable some controllers
  for(std::vector<std::string>::iterator it = enable_controllers.begin();
      it != enable_controllers.end(); it++) 
  {
    if(controller_interfaces_.find(*it) != controller_interfaces_.end()) {
      controller_interfaces_.find(*it)->second->enabled = true;
    }
  }

  // Disable some controllers
  for(std::vector<std::string>::iterator it = disable_controllers.begin();
      it != disable_controllers.end(); it++) 
  {
    if(controller_interfaces_.find(*it) != controller_interfaces_.end()) {
      controller_interfaces_.find(*it)->second->enabled = false;
    }
  }
}