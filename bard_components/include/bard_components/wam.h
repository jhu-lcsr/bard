#ifndef __BARD_COMPONENTS_WAM_H
#define __BARD_COMPONENTS_WAM_H

#include <ros/ros.h>
#include <Eigen/Dense>
#include <kdl/jntarray.hpp>

#include <barrett_direct/WAM.h>
#include <leoCAN/RTSocketCAN.h>

#include <rtt/RTT.hpp>
#include <rtt/Port.hpp>

#include <sensor_msgs/JointState.h>

#include <iostream>

namespace bard_components {
  class WAM : public RTT::TaskContext
  {
    // RTT Interface
    RTT::InputPort<KDL::JntArray> torques_in_port_;
    RTT::OutputPort<KDL::JntArray> positions_out_port_;
    RTT::OutputPort<sensor_msgs::JointState> joint_state_out_port_;

    // See: http://eigen.tuxfamily.org/dox/TopicStructHavingEigenMembers.html
    // See: http://www.orocos.org/forum/orocos/orocos-users/some-info-eigen-and-orocos
    // EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

  public:
    WAM(string const& name) :
      TaskContext(name, RTT::base::TaskCore::PreOperational)
      // Properties
      ,n_wam_dof_(0)
      ,can_dev_name_("")
      ,robot_model_xml_("")
      ,joint_prefix_("")
      ,initial_positions_(7,0.0)
      // Throttle joint state publisher
      ,joint_state_throttle_max_(10)
      ,joint_state_throttle_counter_(0)
      // Internal variables
      ,canbus_(NULL)
      ,robot_(NULL)
      ,torques_()
      ,positions_()
      ,joint_state_()
    {
      // Declare properties (configuration variables)
      this->addProperty("n_wam_dof",n_wam_dof_).doc("The number of degrees-of-freedom of the WAM robot (4 or 7).");
      this->addProperty("can_dev_name",can_dev_name_).doc("The name of the RTCAN device to which this WAM robot is connected.");
      this->addProperty("robot_model_xml",robot_model_xml_).doc("The WAM URDF xml string.");
      this->addProperty("joint_prefix",joint_prefix_).doc("The joint name prefix used in the WAM URDF.");
      this->addProperty("initial_positions",initial_positions_).doc("The calibration position of the robot.");

      // Configure data ports
      this->ports()->addEventPort("torques_in", torques_in_port_).doc("Input Event port: nx1 vector of joint torques. (n joints)");
      this->ports()->addPort("positions_out", positions_out_port_).doc("Output port: nx1 vector of joint positions. (n joints)");
      this->ports()->addPort("joint_state_out", joint_state_out_port_).doc("Output port: sensor_msgs::JointState.");

      // Add operation for setting the encoder values
      this->addOperation("calibrate_position", &WAM::calibrate_position, this, RTT::OwnThread)
        .doc("Set the angles that the encoders should read with the arm in the current configuration. This is used for calibrating the robot.")
        .arg("angles","The new joint angles.");

      // Add operations for setting warnings and faults
      this->addOperation("setVelocityWarning", &barrett_direct::WAM::SetVelocityWarning, robot_, RTT::OwnThread)
        .doc("Set the velocities above which the WAM pendant will illumiate a warning light.")
        .arg("thresh","Velocity Warning Threshold");
      this->addOperation("setVelocityFault", &barrett_direct::WAM::SetVelocityFault, robot_, RTT::OwnThread)
        .doc("Set the velocities above which the WAM pendant will abruptly shut down the arm and illumiate a fault light.")
        .arg("thresh","Velocity Fault Threshold");
      this->addOperation("setTorqueWarning", &barrett_direct::WAM::SetTorqueWarning, robot_, RTT::OwnThread)
        .doc("Set the velocities above which the WAM pendant will illumiate a warning light.")
        .arg("thresh","Torque Warning Threshold");
      this->addOperation("setTorqueFault", &barrett_direct::WAM::SetTorqueFault, robot_, RTT::OwnThread)
        .doc("Set the velocities above which the WAM pendant will abruptly shut down the arm and illumiate a fault light.")
        .arg("thresh","Torque Fault Threshold");
      
      std::cout << "WAM \""<<name<<"\" constructed !" <<std::endl;
    }

    void calibrate_position(std::vector<double> &actual_positions) {
      // Make sure we have a connection to the robot
      if(this->isConfigured()) {
        // Assign the positions to the current robot configuration
        if(robot_->SetPositions(Eigen::Map<Eigen::VectorXd>(&actual_positions[0], actual_positions.size()))
            != barrett_direct::WAM::ESUCCESS)
        {
          std::cerr<<"Failed to calibrate encoders!"<<std::endl;
        }
        std::cerr<<"Calibrated encoders."<<std::endl;
      } else {
        std::cerr<<"Cannot calibrate encoders! The connection to the WAM robot on device "<<can_dev_name_<<" is not open."<<std::endl;
      }
    }

    bool configureHook() {
      // TODO: load URDF from  rosparam
      // TODO: get base link of THIS arm from rosparam

      // TODO: get #DOF from URDF
      // TODO: get joint names from URDF

      // Resize joint arrays
      torques_ = KDL::JntArray(n_wam_dof_);
      positions_ = KDL::JntArray(n_wam_dof_);

      // Zero out torques and positions
      torques_.data.setZero();
      positions_.data.setZero();

      // Construct ros JointState message
      joint_state_.name.push_back(joint_prefix_+"/YawJoint");
      joint_state_.name.push_back(joint_prefix_+"/ShoulderPitchJoint");
      joint_state_.name.push_back(joint_prefix_+"/ShoulderYawJoint");
      joint_state_.name.push_back(joint_prefix_+"/ElbowJoint");
      joint_state_.name.push_back(joint_prefix_+"/UpperWristYawJoint");
      joint_state_.name.push_back(joint_prefix_+"/UpperWristPitchJoint");
      joint_state_.name.push_back(joint_prefix_+"/LowerWristYawJoint");

      joint_state_.position.resize(n_wam_dof_);
      joint_state_.velocity.resize(n_wam_dof_);
      joint_state_.effort.resize(n_wam_dof_);
      
      // Prepare ports for realtime processing
      positions_out_port_.setDataSample(positions_);
      joint_state_out_port_.setDataSample(joint_state_);
      
      // Try to connect and initialize hardware
      try{
        // Construct CAN structure
        canbus_ = new leoCAN::RTSocketCAN(can_dev_name_, leoCAN::CANBus::RATE_1000 );

        // Open the canbus
        if( canbus_->Open() != leoCAN::CANBus::ESUCCESS ){
          std::cerr<<"Failed to open CAN device \""<<can_dev_name_<<"\""<<std::endl;
          throw std::exception();
        }

        // Construct WAM structure
        robot_ = new barrett_direct::WAM(canbus_, (barrett_direct::WAM::Configuration)n_wam_dof_);

        // Initialize the WAM robot
        if( robot_->Initialize() != barrett_direct::WAM::ESUCCESS ){
          std::cerr<<"Failed to initialize WAM"<<std::endl;
          throw std::exception();
        }

        // Set the joints to the calibration position
        this->calibrate_position(initial_positions_);

      } catch(std::exception &ex) {
        // Free the device handles
        this->cleanup_lowlevel();
        return false;
      }

      std::cout << "WAM connected on CAN device \""<<can_dev_name_<<"\"!" <<std::endl;

      return true;
    }

    bool startHook() {
      // Check the data ports
      if ( !torques_in_port_.connected() ) {
        std::cerr<<"ERROR: No connection to \"torques_in\" for WAM on \""<<can_dev_name_<<"\"!"<<std::endl;
        return false;
      }

      if ( !positions_out_port_.connected() ) {
        std::cerr<<"WARNING: No connection to \"positions_out\" for WAM on \""<<can_dev_name_<<"\"!"<<std::endl;
      }
      
      // Set the robot to Activated
      if( robot_->SetMode(barrett_direct::WAM::MODE_ACTIVATED) != barrett_direct::WAM::ESUCCESS ){
        std::cerr<<"Failed to ACTIVATE WAM Robot on CAN device \""<<can_dev_name_<<"\""<<std::endl;
      }

      std::cout << "WAM started on CAN device \""<<can_dev_name_<<"\"!" <<std::endl;
      return true;
    }

    void updateHook() {
      // Get joint positions
      if( robot_->GetPositions( positions_.data ) != barrett_direct::WAM::ESUCCESS) {
          std::cerr<<"Failed to get positions of WAM Robot on CAN device \""<<can_dev_name_<<"\""<<std::endl;
      }

      // Only send joint torques if new data is coming in
      if( torques_in_port_.read( torques_ ) == RTT::NewData ) {
        if( robot_->SetTorques( torques_.data ) != barrett_direct::WAM::ESUCCESS ) {
          std::cerr<<"Failed to set torques of WAM Robot on CAN device \""<<can_dev_name_<<"\""<<std::endl;
        }
      }

      // Send joint positions
      positions_out_port_.write( positions_ );

      // Copy joint positions into joint state
      if(joint_state_throttle_counter_++ == joint_state_throttle_max_) {
        joint_state_.header.stamp = ros::Time::now();
        for(int i=0; i<n_wam_dof_; i++) {
          joint_state_.position[i] = positions_(i);
          joint_state_.effort[i] = torques_(i);
        }
        joint_state_out_port_.write( joint_state_ );
        joint_state_throttle_counter_ = 0;
      } 
    }

    void stopHook() {
      // Set the robot to IDLE
      if( robot_->SetMode(barrett_direct::WAM::MODE_IDLE) != barrett_direct::WAM::ESUCCESS ){
        std::cerr<<"Failed to IDLE WAM Robot on CAN device \""<<can_dev_name_<<"\""<<std::endl;
      }
    }

    void cleanupHook() {
      // Close the CANBus
      if( canbus_->Close() != leoCAN::CANBus::ESUCCESS ){
        std::cerr<<"Failed to close CAN device \""<<can_dev_name_<<"\""<<std::endl;
      }

      // Free the device handles
      this->cleanup_lowlevel();
    }

  private:

    void cleanup_lowlevel() {
      if(robot_) {
        delete robot_;
        robot_ = NULL;
      }
      if(canbus_) {
        delete canbus_;
        canbus_ = NULL;
      }
    }

    // Configuration properties
    int n_wam_dof_;
    std::string can_dev_name_;
    std::string robot_model_xml_;
    std::string joint_prefix_;
    std::vector<double> initial_positions_;
    size_t joint_state_throttle_max_;
    size_t joint_state_throttle_counter_;

    // Hardware hooks
    leoCAN::RTSocketCAN *canbus_;
    barrett_direct::WAM *robot_;

    // Working variables
    KDL::JntArray torques_;
    KDL::JntArray positions_;
    sensor_msgs::JointState joint_state_;

  };
}


#endif // ifndef __BARD_COMPONENTS_WAM_H
