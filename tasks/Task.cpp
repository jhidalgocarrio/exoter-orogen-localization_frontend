/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#ifndef D2R
#define D2R M_PI/180.00 /** Convert degree to radian **/
#endif
#ifndef R2D
#define R2D 180.00/M_PI /** Convert radian to degree **/
#endif

//#define DEBUG_PRINTS 1

#include <base/Matrix.hpp>

using namespace localization_frontend;

Task::Task(std::string const& name)
    : TaskBase(name)
{

    /******************************/
    /*** Control Flow Variables ***/
    /******************************/
    initPosition = false;
    initAttitude = false;

    counter.reset();
    number.reset();
    flag.reset();

    /***************************/
    /** Output port variables **/
    /***************************/
    world2navigationRbs.invalidate();
    world_osg2worldRbs.invalidate();
    referenceOut.invalidate();
    delta_referenceOut.invalidate();

    /**********************************/
    /*** Internal Storage Variables ***/
    /**********************************/

    /** Default size for the circular_buffer of the raw port samples **/
    cbJointsSamples = boost::circular_buffer<base::samples::Joints>(DEFAULT_CIRCULAR_BUFFER_SIZE);
    cbImuSamples = boost::circular_buffer<base::samples::IMUSensors> (DEFAULT_CIRCULAR_BUFFER_SIZE);
    cbOrientationSamples = boost::circular_buffer<base::samples::RigidBodyState> (DEFAULT_CIRCULAR_BUFFER_SIZE);
    cbReferencePoseSamples = boost::circular_buffer<base::samples::RigidBodyState> (DEFAULT_CIRCULAR_BUFFER_SIZE);

    /** Default size for the circular_buffer for the filtered port samples **/
    jointsSamples = boost::circular_buffer<base::samples::Joints>(DEFAULT_CIRCULAR_BUFFER_SIZE);
    imuSamples = boost::circular_buffer<base::samples::IMUSensors> (DEFAULT_CIRCULAR_BUFFER_SIZE);
    orientationSamples = boost::circular_buffer<base::samples::RigidBodyState> (DEFAULT_CIRCULAR_BUFFER_SIZE);
    referencePoseSamples = boost::circular_buffer<base::samples::RigidBodyState> (DEFAULT_CIRCULAR_BUFFER_SIZE);

}

Task::Task(std::string const& name, RTT::ExecutionEngine* engine)
    : TaskBase(name, engine)
{
}

Task::~Task()
{
}

void Task::pose_reference_samplesTransformerCallback(const base::Time &ts, const ::base::samples::RigidBodyState &pose_reference_samples_sample)
{
    cbReferencePoseSamples.push_front(pose_reference_samples_sample);

    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER REFERENCE-POSE]Received Reference Pose Samples at("<<pose_reference_samples_sample.time.toMicroseconds()<<") **\n";
    #endif

    #ifdef DEBUG_PRINTS
    Eigen::Vector3d euler = base::getEuler(cbReferencePoseSamples[0].orientation);
    std::cout<< "******** [EXOTER REFERENCE_POSE]\n";
    std::cout<< "******** RECEIVED ATTITUDE *******"<<"\n";
    std::cout<< "Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
    #endif


    if (!initPosition)
    {
        /** Set pose Tworld_navigation. First time is Tworld_body **/
        world2navigationRbs.position = cbReferencePoseSamples[0].position;
        world2navigationRbs.velocity.setZero();

        /** Assume well known starting position **/
        world2navigationRbs.cov_position = Eigen::Matrix3d::Zero();
        world2navigationRbs.cov_velocity = Eigen::Matrix3d::Zero();
    }

    if (!initAttitude)
    {
        world2navigationRbs.orientation = cbReferencePoseSamples[0].orientation;
	
        #ifdef DEBUG_PRINTS
        Eigen::Matrix <double,3,1> euler = base::getEuler(cbReferencePoseSamples[0].orientation); /** In Euler angles **/
        std::cout<<"** [EXOTER REFERENCE_POSE]cbReferencePoseSamples at ("<<cbReferencePoseSamples[0].time.toMicroseconds()<< ")**\n";
        std::cout<<"** position(world_frame)\n"<< cbReferencePoseSamples[0].position<<"\n";
        std::cout<<"** Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
        #endif

        /** Initial angular velocity **/
        world2navigationRbs.angular_velocity.setZero();

        /** Assume very well know initial attitude **/
        world2navigationRbs.cov_orientation = Eigen::Matrix <double, 3 , 3>::Zero();
        world2navigationRbs.cov_angular_velocity = Eigen::Matrix <double, 3 , 3>::Zero();

        initPosition = true;
    }
    else if (initAttitude)
    {
        /** Transform the reference pose world_body to navigation_body. Transform the pose transformation. **/
        Eigen::Affine3d Tworld_body = cbReferencePoseSamples[0].getTransform();
        cbReferencePoseSamples[0].setTransform(world2navigationRbs.getTransform().inverse() * Tworld_body);//Tnavigation_body = (Tworld_navigation)^-1 * Tworld_body
        cbReferencePoseSamples[0].orientation.normalize();

        /** At this point reference pose does not have info regarding linear or angular velocity **/

        #ifdef DEBUG_PRINTS
        Eigen::Matrix <double,3,1> euler = base::getEuler(cbReferencePoseSamples[0].orientation); /** In Euler angles **/
        std::cout<<"** [EXOTER REFERENCE_POSE] CURRENT **\n";
        std::cout<<"** position(world_frame)\n"<< cbReferencePoseSamples[0].position<<"\n";
        std::cout<<"** Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
        #endif

    }

   flag.referencePoseSamples = true;
}

void Task::inertial_samplesTransformerCallback(const base::Time &ts, const ::base::samples::IMUSensors &inertial_samples_sample)
{
    Eigen::Affine3d tf; /** Transformer transformation **/
    Eigen::Quaternion <double> qtf; /** Rotation part of the transformation in quaternion form **/

    /** Get the transformation (transformation) Tbody_imu which is body = Tbody_imu imu **/
    if (!_imu2body.get(ts, tf, false))
	return;

    qtf = Eigen::Quaternion <double> (tf.rotation());//!Quaternion from Body to imu (transforming samples from imu to body)

    /** Increment counter **/
    counter.imuSamples++;

    /** Set the flag of Inertial samples values valid to true **/
    if (counter.imuSamples == cbImuSamples.capacity())
        flag.imuSamples = true;

    base::samples::IMUSensors imusample;

    /** A new sample arrived to the port **/
    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER INERTIAL_SAMPLES] counter.imuSamples("<<counter.imuSamples<<") at ("<<inertial_samples_sample.time.toMicroseconds()<< ")**\n";
    std::cout<<"acc(imu_frame):\n"<<inertial_samples_sample.acc<<"\n";
    std::cout<<"acc(quat body_frame ):\n"<<qtf * inertial_samples_sample.acc<<"\n";
    std::cout<<"acc(Rot body_frame):\n"<< tf.rotation() * inertial_samples_sample.acc<<"\n";
    std::cout<<"acc(Trans body_frame):\n"<< tf * inertial_samples_sample.acc<<"\n";
    std::cout<<"gyro(imu_frame):\n"<<inertial_samples_sample.gyro<<"\n";
    std::cout<<"gyro(quat body_frame):\n"<<qtf * inertial_samples_sample.gyro<<"\n";
    std::cout<<"mag(imu_frame):\n"<<inertial_samples_sample.mag<<"\n";
    std::cout<<"mag(quat body_frame):\n"<<qtf * inertial_samples_sample.mag<<"\n";
    #endif

    /** Convert the IMU values in the body frame **/
    imusample.time = inertial_samples_sample.time;
    imusample.acc = qtf * inertial_samples_sample.acc;
    imusample.gyro = qtf * inertial_samples_sample.gyro;
    imusample.mag = qtf * inertial_samples_sample.mag;

    /** Push the corrected inertial values into the buffer **/
    cbImuSamples.push_front(imusample);

    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER INERTIAL_SAMPLES] Corrected inertial ("<<counter.imuSamples<<") at ("<<inertial_samples_sample.time.toMicroseconds()<< ")**\n";
    std::cout<<"acc(body_frame):\n"<<imusample.acc<<"\n";
    std::cout<<"gyro(body_frame):\n"<<imusample.gyro<<"\n";
    std::cout<<"mag(body_frame):\n"<<imusample.mag<<"\n";
    #endif

}

void Task::orientation_samplesTransformerCallback(const base::Time &ts, const ::base::samples::RigidBodyState &orientation_samples_sample)
{
    Eigen::Affine3d tf; /** Transformer transformation **/
    Eigen::Quaternion <double> qtf; /** Rotation of the transformation in quaternion form **/

    /** Get the transformation (transformation) Tbody_imu**/
    if (!_imu2body.get(ts, tf, false))
	return;

    qtf = Eigen::Quaternion <double> (tf.rotation());//!Quaternion from Body to imu (transforming samples from imu to body)

    /** Increment counter **/
    counter.orientationSamples++;

    /** Set the flag of orientation samples values valid to true **/
    if (counter.orientationSamples == cbOrientationSamples.capacity())
        flag.orientationSamples = true;

    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER ORIENTATION_SAMPLES] counter.orientationSamples("<<counter.orientationSamples<<") at ("<<orientation_samples_sample.time.toMicroseconds()<< ")**\n";
    #endif

    /** Push one sample into the buffer **/
    cbOrientationSamples.push_front(orientation_samples_sample);

    #ifdef DEBUG_PRINTS
    Eigen::Vector3d euler = base::getEuler(cbOrientationSamples[0].orientation);
    std::cout<< "******** [EXOTER ORIENTATION_SAMPLES]\n";
    std::cout<< "******** RECEIVED ATTITUDE *******"<<"\n";
    std::cout<< "Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
    #endif


    /** Transform the orientation world(osg)_imu to world(osg)_body **/
    cbOrientationSamples[0].orientation = orientation_samples_sample.orientation * qtf.inverse(); // Tworld(osg)_body = Tworld(osg)_imu * (Tbody_imu)^-1
    cbOrientationSamples[0].cov_orientation = tf.inverse().rotation() * orientation_samples_sample.cov_orientation * tf.inverse().rotation().transpose(); // Tworld(osg)_body = Tworld(osg)_imu * (Tbody_imu)^-1
    base::guaranteeSPD< Eigen::Matrix<double, 3, 3> > (cbOrientationSamples[0].cov_orientation);

    #ifdef DEBUG_PRINTS
    euler = base::getEuler(cbOrientationSamples[0].orientation);
    std::cout<< "******** [EXOTER ORIENTATION_SAMPLES]\n";
    std::cout<< "******** RECEIVED ATTITUDE IN BODY*******"<<"\n";
    std::cout<< "Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
    #endif


    if(!initAttitude)
    {
        Eigen::Quaterniond attitude = cbOrientationSamples[0].orientation; //Tworld(osg)_body

        /** Check if there is initial pose connected **/
        if (_pose_reference_samples.connected() && initPosition)
        {
            double heading = 0.00;

            /** Check if the reference pose has a valid orientation **/
            if(base::samples::RigidBodyState::isValidValue(cbReferencePoseSamples[0].orientation))
            {
                heading = base::getEuler(cbReferencePoseSamples[0].orientation)[0];// vector[0] is z-axis
            }
            else
            {
                RTT::log(RTT::Warning)<<"Initial Heading from External Reference is not Valid."<<RTT::endlog();
            }

            /** Align the Yaw from the Reference Pose Samples, Pitch and Roll from orientationSamples **/
            attitude = Eigen::Quaternion <double>(
                    Eigen::AngleAxisd(heading, Eigen::Vector3d::UnitZ())*
                    Eigen::AngleAxisd(base::getEuler(attitude)[1], Eigen::Vector3d::UnitY()) *
                    Eigen::AngleAxisd(base::getEuler(attitude)[2], Eigen::Vector3d::UnitX()));

            attitude.normalize(); //Tworld_body

            /** Compute the world_osg to world frame **/
            world_osg2worldRbs.orientation = cbOrientationSamples[0].orientation * attitude.inverse();//Tworld_osg_world = Tworld_osg_body * (Tworld_body)^-1

            initAttitude = true;
        }
        else if (!_pose_reference_samples.connected())
        {
            /** Set zero position **/
            world2navigationRbs.position.setZero();

            /** Assume well known starting position **/
            world2navigationRbs.cov_position = Eigen::Matrix <double, 3 , 3>::Zero();
            world2navigationRbs.cov_velocity = Eigen::Matrix <double, 3 , 3>::Zero();

            /** Compute the world_osg to world frame. Make world_osg and world coincident **/
            world_osg2worldRbs.orientation.setIdentity();

            initPosition = true;
            initAttitude = true;
        }

        /** Set the initial attitude to the world to navigation transform **/
        if (initAttitude)
        {
            /** Store the value as the initial one for the world to navigation **/
            world2navigationRbs.orientation = attitude; //At the first sample Tworld_body is Tworld_navigation
            world2navigationRbs.angular_velocity.setZero();

            /** Assume very well know initial attitude **/
            world2navigationRbs.cov_orientation = Eigen::Matrix <double, 3 , 3>::Zero();
            world2navigationRbs.cov_angular_velocity = Eigen::Matrix <double, 3 , 3>::Zero();

            /** Position for the world_osg to world **/
            world_osg2worldRbs.position.setZero();

            /** Assume very well know initial attitude **/
            world_osg2worldRbs.cov_position = Eigen::Matrix <double, 3 , 3>::Zero();
            world_osg2worldRbs.cov_velocity = Eigen::Matrix <double, 3 , 3>::Zero();
            world_osg2worldRbs.cov_orientation = Eigen::Matrix <double, 3 , 3>::Zero();
            world_osg2worldRbs.cov_angular_velocity = Eigen::Matrix <double, 3 , 3>::Zero();

            #ifdef DEBUG_PRINTS
            Eigen::Vector3d euler = base::getEuler(attitude);
            std::cout<< "******** [EXOTER ORIENTATION_SAMPLES]\n";
            std::cout<< "******** Initial Attitude *******"<<"\n";
            std::cout<< "Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
            #endif


            if (initPosition)
            {
                if (state() != RUNNING)
                    state(RUNNING);
            }
        }
    }
    else
    {
        /** We want the orientation with respect to the fixed local frame Navigation frame **/
        Eigen::Quaterniond qnavigation_world_osg(world2navigationRbs.orientation.inverse() * world_osg2worldRbs.orientation.inverse());//Tnavigation_world(osg) = (Tworld_navigation)^-1 * (Tworld(osg)_world)^-1

        /** Transform the orientation world(osg)_body to navigation_body **/
        cbOrientationSamples[0].orientation = qnavigation_world_osg * cbOrientationSamples[0].orientation; // Tnavigation_body = (Tworld(osg)_navigation)^-1 * Tworld(osg)_body
        cbOrientationSamples[0].orientation.normalize();
        cbOrientationSamples[0].cov_orientation = qnavigation_world_osg.toRotationMatrix() * cbOrientationSamples[0].cov_orientation * qnavigation_world_osg.toRotationMatrix().transpose(); // Tnavigation_body = (Tworld(osg)_navigation)^-1 * Tworld(osg)_body

        #ifdef DEBUG_PRINTS
        Eigen::Vector3d euler = base::getEuler(cbOrientationSamples[0].orientation);
        std::cout<< "******** [EXOTER ORIENTATION_SAMPLES]\n";
        std::cout<< "******** Current Attitude *******"<<"\n";
        std::cout<< "Roll: "<<euler[2]*R2D<<" Pitch: "<<euler[1]*R2D<<" Yaw: "<<euler[0]*R2D<<"\n";
        #endif
    }
}

void Task::joints_samplesTransformerCallback(const base::Time &ts, const ::base::samples::Joints &joints_samples_sample)
{
    base::samples::Joints joints;
    joints.resize(all_joint_names.size());
    joints.time = joints_samples_sample.time;
    joints.names = all_joint_names;

    /** A new sample arrived to the Input port **/
    for(std::vector<std::string>::const_iterator it = all_joint_names.begin(); it != all_joint_names.end(); it++)
    {
        try
        {
            joints[*it] = joints_samples_sample.getElementByName(*it);
        } catch(base::samples::Joints::InvalidName ex){
            joints[*it].position = 0.00;
            joints[*it].speed = base::NaN<double>();
        }
    }

    cbJointsSamples.push_front(joints);

    /** Increment counter **/
    counter.jointsSamples++;

    /** Set the flag of joints samples values valid to true **/
    if (counter.jointsSamples == cbJointsSamples.capacity())
    	flag.jointsSamples = true;


    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER JOINTS-SAMPLES] counter.jointsSamples("<<counter.jointsSamples<<") at ("<<joints_samples_sample.time.toMicroseconds()
        <<") received FR position ("<<joints_samples_sample[0].position<<")**\n";
    #endif

    #ifdef DEBUG_PRINTS
    std::cout<<"** [EXOTER JOINTS-SAMPLES] [COUNTERS] jointsCounter ("<<counter.jointsSamples<<") imuCounter("<<counter.imuSamples<<") orientationSamples("<<counter.orientationSamples<<") **\n";
    std::cout<<"** [EXOTER JOINTS-SAMPLES] [FLAGS] initAttitude ("<<initAttitude<<") initPosition("<<initPosition<<") **\n";
    std::cout<<"** [EXOTER JOINTS-SAMPLES] [FLAGS] flagJoints ("<<flag.jointsSamples<<") flagIMU("<<flag.imuSamples<<") flagOrient("<<flag.orientationSamples<<") **\n";
    #endif

    if (state() == RUNNING)
    {
        if (flag.imuSamples && flag.orientationSamples && flag.jointsSamples)
        {
            #ifdef DEBUG_PRINTS
            std::cout<<"[ON] ** [EXOTER JOINTS-SAMPLES] ** [ON] ("<<jointsSamples[0].time.toMicroseconds()<<")\n";
       	    #endif

            /** Get the correct values from the input ports buffers  **/
            this->inputPortSamples();

            /** Calculate velocities from the input ports **/
            this->calculateVelocities();

            /** Out port the information of the proprioceptive sensors **/
            this->outputPortSamples ();

            /** Reset back the counters and the flags **/
            counter.reset();
            flag.reset();

        }

        /** Sanity check: Reset counter in case of inconsistency **/
        if (counter.jointsSamples > cbJointsSamples.size())
            counter.jointsSamples = 0;
        if (counter.imuSamples > cbImuSamples.size())
            counter.imuSamples = 0;
        if (counter.orientationSamples > cbOrientationSamples.size())
            counter.orientationSamples = 0;
    }
}

void Task::left_frameTransformerCallback(const base::Time &ts, const ::RTT::extras::ReadOnlyPointer< ::base::samples::frame::Frame > &left_frame_sample)
{
    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER LEFT-CAMERA] Frame at: "<<left_frame_sample->time.toMicroseconds()<<"\n";
    #endif

    if (state() == RUNNING)
    {

        /** Undistorted image depending on meta data information **/
        ::base::samples::frame::Frame *frame_ptr = leftFrame.write_access();
        frame_ptr->time = left_frame_sample->time;
        frame_ptr->init(left_frame_sample->size.width, left_frame_sample->size.height, left_frame_sample->getDataDepth(), left_frame_sample->getFrameMode());
        frameHelperLeft.convert(*left_frame_sample, *frame_ptr, 0, 0, frame_helper::INTER_LINEAR, true);
        leftFrame.reset(frame_ptr);

        /** Write the camera frame into the port **/
        _left_frame_out.write(leftFrame);

    }

    return;
}

void Task::right_frameTransformerCallback(const base::Time &ts, const ::RTT::extras::ReadOnlyPointer< ::base::samples::frame::Frame > &right_frame_sample)
{
    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER RIGHT-CAMERA] Frame at: "<<right_frame_sample->time.toMicroseconds()<<"\n";
    #endif

    if (state() == RUNNING)
    {
        /** Undistorted image depending on meta data information **/
        ::base::samples::frame::Frame *frame_ptr = rightFrame.write_access();
        frame_ptr->time = right_frame_sample->time;
        frame_ptr->init(right_frame_sample->size.width, right_frame_sample->size.height, right_frame_sample->getDataDepth(), right_frame_sample->getFrameMode());
        frameHelperRight.convert(*right_frame_sample, *frame_ptr, 0, 0, frame_helper::INTER_LINEAR, true);
        rightFrame.reset(frame_ptr);

        /** Write the camera frame into the port **/
        _right_frame_out.write(rightFrame);
    }

    return;
}

void Task::point_cloud_samplesTransformerCallback(const base::Time &ts, const ::base::samples::Pointcloud &point_cloud_samples_sample)
{
    /** Get the transformation from the transformer **/
    Eigen::Affine3d tf;

    /** Get the transformation (transformation) Tbody_laser which transforms laser pints two body points **/
    if(!_laser2body.get( ts, tf ))
    {
        RTT::log(RTT::Warning)<<"[ EXOTER POINT CLOUD FATAL ERROR] No transformation provided for the transformer."<<RTT::endlog();
        return;
    }

    if (state() == RUNNING)
    {
        base::samples::Pointcloud pointcloud;

        /** Transform the point cloud in body frame **/
        pointcloud.time = point_cloud_samples_sample.time;
        pointcloud.points.resize(point_cloud_samples_sample.points.size());
        pointcloud.colors = point_cloud_samples_sample.colors;
        register int k = 0;
        for (std::vector<base::Point>::const_iterator it = point_cloud_samples_sample.points.begin();
            it != point_cloud_samples_sample.points.end(); it++)
        {
            pointcloud.points[k] = tf * (*it);
            k++;
        }

        /** Write the point cloud into the port **/
        _point_cloud_samples_out.write(pointcloud);
    }

    return;
}

/// The following lines are template definitions for the various state machine
// hooks defined by Orocos::RTT. See Task.hpp for more detailed
// documentation about them.

bool Task::configureHook()
{
    if (! TaskBase::configureHook())
        return false;

    /************************/
    /** Read configuration **/
    /************************/
    proprioceptive_output_frequency = _proprioceptive_output_frequency.value();
    all_joint_names = _all_joint_names.value();
    zero_position_joint_names = _zero_position_joint_names.value();
    zero_speed_joint_names = _zero_speed_joint_names.value();
    mimic_joint_names = _mimic_joint_names.value();
    translation_joint_names = _translation_joint_names.value();
    filterConfig = _filter_config.value();
    filter_joint_names = _filter_joint_names.value();

    /*******************************************/
    /** Initial world to navigation transform **/
    /*******************************************/

    /** Set the initial world to navigation frame transform (transformation for transformer) **/
    world2navigationRbs.invalidate();
    world2navigationRbs.sourceFrame = _navigation_source_frame.get();
    world2navigationRbs.targetFrame = _navigation_target_frame.get();

    /******************************************/
    /** Initial world_osg to world transform **/
    /******************************************/
    world_osg2worldRbs.invalidate();
    world_osg2worldRbs.sourceFrame = _world_source_frame.get();
    world_osg2worldRbs.targetFrame = _world_target_frame.get();

    /******************************************/
    /** Reference Ground truth transform     **/
    /******************************************/
    referenceOut.invalidate();
    referenceOut.sourceFrame = _reference_source_frame.get();
    referenceOut.targetFrame = _reference_target_frame.get();

    delta_referenceOut.invalidate();
    delta_referenceOut.sourceFrame = _delta_reference_source_frame.get();
    delta_referenceOut.targetFrame = _delta_reference_target_frame.get();

    /******************************************/
    /** Use properties to Configure the Task **/
    /******************************************/

    /** Set the Input ports counter to Zero **/
    counter.reset();

    /** Set the number of samples between each sensor input (if there are not coming at the same sampling rate) */
    if (proprioceptive_output_frequency != 0.00)
    {
        number.imuSamples = (1.0/_inertial_samples_period.value())/proprioceptive_output_frequency;
        number.jointsSamples = (1.0/_joints_samples_period.value())/proprioceptive_output_frequency;
        number.orientationSamples = (1.0/_orientation_samples_period.value())/proprioceptive_output_frequency;
        number.referencePoseSamples = std::max((1.0/_pose_reference_samples_period.value())/proprioceptive_output_frequency, 1.0);
    }

    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER CONFIGURE] cbJointsSamples has init capacity "<<cbJointsSamples.capacity()<<" and size "<<cbJointsSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbImuSamples has init capacity "<<cbImuSamples.capacity()<<" and size "<<cbImuSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbOrientationSamples has capacity "<<cbOrientationSamples.capacity()<<" and size "<<cbOrientationSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbReferencePoseSamples has capacity "<<cbReferencePoseSamples.capacity()<<" and size "<<cbReferencePoseSamples.size()<<"\n";
    #endif

    /** Set the capacity of the circular_buffer according to the sampling rate **/
    cbJointsSamples.set_capacity(number.jointsSamples);
    cbImuSamples.set_capacity(number.imuSamples);
    cbOrientationSamples.set_capacity(number.orientationSamples);
    cbReferencePoseSamples.set_capacity(number.referencePoseSamples);

    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER CONFIGURE] cbJointsSamples has capacity "<<cbJointsSamples.capacity()<<" and size "<<cbJointsSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbImuSamples has capacity "<<cbImuSamples.capacity()<<" and size "<<cbImuSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbOrientationSamples has capacity "<<cbOrientationSamples.capacity()<<" and size "<<cbOrientationSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] cbReferencePoseSamples has capacity "<<cbReferencePoseSamples.capacity()<<" and size "<<cbReferencePoseSamples.size()<<"\n";
    #endif

    for(register unsigned int i=0; i<cbJointsSamples.size(); ++i)
    {
    	cbJointsSamples[i].resize(all_joint_names.size());
    }

    /** Initialize the samples for the filtered buffer joint values **/
    for(register unsigned int i=0; i<jointsSamples.size(); i++)
    {
    	/** Sizing the joints **/
	    jointsSamples[i].resize(all_joint_names.size());
    }

    /** Initialize the samples for the filtered buffer imuSamples values **/
    for(register unsigned int i=0; i<imuSamples.size();++i)
    {
        /** IMU Samples **/
        imuSamples[i].acc[0] = base::NaN<double>();
        imuSamples[i].acc[1] = base::NaN<double>();
        imuSamples[i].acc[2] = base::NaN<double>();
        imuSamples[i].gyro = imuSamples[0].acc;
        imuSamples[i].mag = imuSamples[0].acc;
    }

    /** Initialize the samples for the filtered buffer Reference Pose Samples values **/
    for(register unsigned int i=0; i<referencePoseSamples.size(); ++i)
    {
        /** Pose Init **/
        referencePoseSamples[i].invalidate();
    }

    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER CONFIGURE] jointsSamples has capacity "<<jointsSamples.capacity()<<" and size "<<jointsSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] imuSamples has capacity "<<imuSamples.capacity()<<" and size "<<imuSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] orientationSamples has capacity "<<orientationSamples.capacity()<<" and size "<<orientationSamples.size()<<"\n";
    std::cout<<"[EXOTER CONFIGURE] referencePoseSamples has capacity "<<referencePoseSamples.capacity()<<" and size "<<referencePoseSamples.size()<<"\n";
    #endif

    /** Output Joints state vector **/
    jointsSamplesOut.resize(all_joint_names.size());
    jointsSamplesOut.names = all_joint_names;


    /** Output images **/
    ::base::samples::frame::Frame *lFrame = new ::base::samples::frame::Frame();
    ::base::samples::frame::Frame *rFrame = new ::base::samples::frame::Frame();

    leftFrame.reset(lFrame);
    rightFrame.reset(rFrame);

    lFrame = NULL; rFrame = NULL;

    /** Frame Helper **/
    frameHelperLeft.setCalibrationParameter(_left_camera_parameters.value());
    frameHelperRight.setCalibrationParameter(_right_camera_parameters.value());

    /**************************/
    /** Exteroceptive Sensor **/
    /**************************/

    /**************************/
    /***** Read URDF file *****/
    /**************************/
    std::string urdf_file = _urdf_file.value();
    std::string xml_string;
    const char * urdf_char = urdf_file.c_str();
    std::fstream xml_file(urdf_char, std::fstream::in);
    while ( xml_file.good() )
    {
        std::string line;
        std::getline( xml_file, line);
        xml_string += (line + "\n");
    }
    xml_file.close();

    urdf::ModelInterfaceSharedPtr robot = urdf::parseURDF(xml_string);
    if (!robot)
    {
        throw std::runtime_error("[Localization Front-End] [Info] Configuration could not parse URDF model\n");
    }

    /******************/
    /** Wheel Radius **/
    /******************/
    Eigen::Vector3d wheel_radius_offset;
    if (this->searchURDFJointNames(robot->getRoot(), _wheel_radius_joint_names.value()[0], wheel_radius_offset))
    {
        /** Minus sign because it is in z-axis negative direction with respect to body in URDF **/
        this->wheel_radius = -wheel_radius_offset[2];
        RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Wheel radius [meter]: "<<this->wheel_radius<<RTT::endlog();
    }
    else
    {
        throw std::runtime_error("[Localization Front-End] [Info] Unable to find Wheel radius joint names in given URDF\n");
    }

    /*********************/
    /** Reaction Forces **/
    /*********************/
    Eigen::Vector3d passive_joint_offset;
    passive_joint_offset.setZero();
    if (this->searchURDFJointNames(robot->getRoot(), _passive_offset_joint_name.value(), passive_joint_offset))
    {
        passive_joint_offset[1] = passive_joint_offset[2] = 0.00;
    }

    this->exoter_rf.passive_joint_offset = passive_joint_offset;
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Passive joint offset for the Reaction forces computation:\n"<<this->exoter_rf.passive_joint_offset<<RTT::endlog();

    /****************************/
    /** Robot Kinematics Model **/
    /****************************/
    this->number_robot_joints =  _all_joint_names.value().size() - _slip_joint_names.value().size() - _contact_joint_names.value().size();
    this->robot_kinematics.reset(new threed_odometry::KinematicKDL (urdf_file, _contact_point_segments.value(),
                            _contact_angle_segments.value(), this->number_robot_joints,
                            _slip_joint_names.value().size(), _contact_joint_names.value().size()));

    /*********************/
    /** Low-Pass Filter **/
    /*********************/
    Eigen::Matrix <double, FILTER_ORDER+1, 1> bCoeff;
    bCoeff =  filterConfig.feedForwardCoeff;

    /** Create the Low-pass filter with the right coefficients **/
    low_pass_filter.reset(new localization::FIR<FILTER_ORDER, FILTER_VECTOR_SIZE> (bCoeff));

    /** Information of the configuration **/
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Frequency of IMU samples[Hertz]: "<<(1.0/_inertial_samples_period.value())<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Frequency of Orientation samples[Hertz]: "<<(1.0/_orientation_samples_period.value())<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Frequency of Joints samples[Hertz]: "<<(1.0/_joints_samples_period.value())<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Frequency of Reference System [Hertz]: "<<(1.0/_pose_reference_samples_period.value())<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Output Frequency for Proprioceptive Inputs[Hertz]: "<<proprioceptive_output_frequency<<RTT::endlog();

    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] number.jointsSamples: "<<number.jointsSamples<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] number.imuSamples: "<<number.imuSamples<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] number.orientationSamples: "<<number.orientationSamples<<RTT::endlog();
    RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] number.referencePoseSamples: "<<number.referencePoseSamples<<RTT::endlog();

    if (filterConfig.filterOn)
        RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Low-Pass Filter [ON]"<<RTT::endlog();
    else
        RTT::log(RTT::Warning)<<"[Localization Front-End] [Info] Low-Pass Filter [OFF]"<<RTT::endlog();

    if (filter_joint_names.size() != FILTER_VECTOR_SIZE)
    {
        RTT::log(RTT::Warning)<<"[Localization Front-End] [FATAL ERROR] The number of joints to perform the filter has to be "<<FILTER_VECTOR_SIZE<<RTT::endlog();
        RTT::log(RTT::Warning)<<"[Localization Front-End] [FATAL ERROR] Otherwise change the FILTER_VECTOR_SIZE constant in the code."<<RTT::endlog();
        RTT::log(RTT::Warning)<<"[Localization Front-End] [FATAL ERROR] This is because low_pass_filter was designed as template class ."<<RTT::endlog();
        return false;
    }

    for(std::vector<std::string>::const_iterator it_name = filter_joint_names.begin(); it_name != filter_joint_names.end(); it_name++)
    {
        std::vector<std::string>::const_iterator it = find(all_joint_names.begin(), all_joint_names.end(), *it_name);
        if (it == all_joint_names.end())
            throw std::runtime_error("[Localization Front-End] [FATAL ERROR]: Joints names for filter must be contained in all joints names property.");
    }


    if ((number.jointsSamples == 0)||(number.imuSamples == 0))
    {
        RTT::log(RTT::Warning)<<"[Localization Front-End] [FATAL ERROR] Output frequency cannot be higher than sensors frequency."<<RTT::endlog();
        return false;
    }


    return true;
}

bool Task::startHook()
{
    if (! TaskBase::startHook())
        return false;

    if (!initPosition && !initAttitude)
        state(INITIAL_POSITIONING);

    return true;
}
void Task::updateHook()
{
    /** Initial position state **/
    if (state() != INITIAL_POSITIONING)
    {
        if (!initPosition && !initAttitude)
            state(INITIAL_POSITIONING);
    }

    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
}


void Task::inputPortSamples()
{
    double cbJointsSize = static_cast<double>(cbJointsSamples.size());
    double cbImuSize =  static_cast<double>(cbImuSamples.size());
    double cbOrientationSize =  static_cast<double>(cbOrientationSamples.size());
    double cbReferencePoseSize =  static_cast<double>(cbReferencePoseSamples.size());

    /** Local variable of the ports **/
    base::samples::Joints joint;
    base::samples::IMUSensors imu;
    base::samples::RigidBodyState orientation;
    base::samples::RigidBodyState reference_pose;

    /** Sizing the joints **/
    joint.resize(all_joint_names.size());

    #ifdef DEBUG_PRINTS
    std::cout<<"[GetInportValue] cbJointsSamples has capacity "<<cbJointsSamples.capacity()<<" and size "<<cbJointsSamples.size()<<"\n";
    std::cout<<"[GetInportValue] cbImuSamples has capacity "<<cbImuSamples.capacity()<<" and size "<<cbImuSamples.size()<<"\n";
    std::cout<<"[GetInportValue] cbOrientationSamples has capacity "<<cbOrientationSamples.capacity()<<" and size "<<cbOrientationSamples.size()<<"\n";
    std::cout<<"[GetInportValue] cbReferencePoseSamples has capacity "<<cbReferencePoseSamples.capacity()<<" and size "<<cbReferencePoseSamples.size()<<"\n";
    #endif

    /** ********* **/
    /**  Joints   **/
    /** ********* **/

    /** Process the buffer **/
    for (register size_t j = 0; j<joint.size(); ++j)
    {
        joint[j].position = 0.00;
        joint[j].speed = 0.00;
        joint[j].effort = 0.00;
        joint[j].raw = 0.00;

        for (register size_t i = 0; i<cbJointsSamples.size(); ++i)
        {
	        joint[j].speed += cbJointsSamples[i][j].speed;
    	    joint[j].effort += cbJointsSamples[i][j].effort;
    	    joint[j].raw += cbJointsSamples[i][j].raw;
	    }

        joint[j].position = cbJointsSamples[0][j].position;
        joint[j].speed /= cbJointsSize;
        joint[j].effort /= cbJointsSize;
        joint[j].raw /= cbJointsSize;
    }

    if (cbJointsSize > 0.0)
    {
        /** Get the joints names **/
        joint.names = cbJointsSamples[0].names;

        /** Set the time **/
        joint.time = (cbJointsSamples[cbJointsSize-1].time + cbJointsSamples[0].time)/2.0;

        /** ********* **/
        /**  FILTER   **/
        /** ********* **/

        /** Low-pass Filter **/
        if (filterConfig.filterOn)
        {
            /** Get the joints to filter **/
            register int idx = 0;
            Eigen::Matrix<double, FILTER_VECTOR_SIZE, 1> filter_jointVector;
            for(std::vector<std::string>::const_iterator it = filter_joint_names.begin(); it != filter_joint_names.end(); it++)
            {
                filter_jointVector[idx] = joint.getElementByName(*it).position;
                idx++;
            }

            /** Filter step **/
            filter_jointVector = this->low_pass_filter->perform(filter_jointVector);

            /** Set the filtered joints **/
            idx = 0;
            for(std::vector<std::string>::const_iterator it = filter_joint_names.begin(); it != filter_joint_names.end(); it++)
            {
                joint[*it].position = filter_jointVector[idx];
                idx++;
            }
        }

        /** *************** **/
        /**  MIMIC JOINTS   **/
        /** *************** **/
        register int idx = 0;
        for(std::vector<std::string>::const_iterator it = mimic_joint_names.names.begin(); it != mimic_joint_names.names.end(); it++)
        {
            joint[*it].position = -joint.getElementByName(mimic_joint_names.elements[idx]).position;
            idx++;
        }

        /** ********************* **/
        /**  TRANSLATION JOINTS   **/
        /** ********************* **/
        idx = 0;
        for(std::vector<std::string>::const_iterator it = translation_joint_names.names.begin(); it != translation_joint_names.names.end(); it++)
        {
            joint[*it].position = this->wheel_radius * joint.getElementByName(translation_joint_names.elements[idx]).position;
            joint[*it].speed = this->wheel_radius * joint.getElementByName(translation_joint_names.elements[idx]).speed;
            idx++;
        }

        /** Push the result in the buffer **/
        jointsSamples.push_front(joint);
    }


    /** ******************* **/
    /** Orientation samples **/
    /** ******************* **/
    if (cbOrientationSize > 0.0)
    {
        Eigen::Matrix3d cov_orientation; cov_orientation.setZero();
        double w = 0.00;
        double x = 0.00;
        double y = 0.00;
        double z = 0.00;

        /** Process the buffer **/
        for (register unsigned int i=0; i<cbOrientationSamples.size(); ++i)
        {
            w += cbOrientationSamples[i].orientation.w();
            x += cbOrientationSamples[i].orientation.x();
            y += cbOrientationSamples[i].orientation.y();
            z += cbOrientationSamples[i].orientation.z();

            cov_orientation += cbOrientationSamples[i].cov_orientation;
        }

        w = w/cbOrientationSize; y = y/cbOrientationSize;
        x = x/cbOrientationSize; z = z/cbOrientationSize;
        orientation.orientation = Eigen::Quaterniond(w, x, y, z);
        orientation.orientation.normalize();

        orientation.cov_orientation = cov_orientation/cbOrientationSize;
        orientation.cov_orientation(2,2) = orientation.cov_orientation(2,2)/_attitude_covariance_adjustment.value(); // fixed error in the covariance computation coming from imu filter
        //base::guaranteeSPD< Eigen::Matrix<double, 3, 3> > (orientation.cov_orientation);

        /** Set the time **/
        orientation.time = (cbOrientationSamples[cbOrientationSize-1].time + cbOrientationSamples[0].time)/2.0;

        /** Push the result into the buffer **/
        orientationSamples.push_front(orientation);

    }

    /** ********************** **/
    /** Reference Pose samples **/
    /** ********************** **/
    if (cbReferencePoseSize > 0.0)
    {
        Eigen::Vector3d position; position.setZero();
        Eigen::Matrix3d cov_position; cov_position.setZero();
        Eigen::Matrix3d cov_orientation; cov_orientation.setZero();
        double w = 0.00;
        double x = 0.00;
        double y = 0.00;
        double z = 0.00;

        reference_pose.invalidate();

        /** Process the buffer **/
        for (register unsigned int i=0; i<cbReferencePoseSamples.size(); ++i)
        {
            /** Position **/
            position += cbReferencePoseSamples[i].position;
            cov_position += cbReferencePoseSamples[i].cov_position;

            /** Orientation **/
            w += cbReferencePoseSamples[i].orientation.w();
            x += cbReferencePoseSamples[i].orientation.x();
            y += cbReferencePoseSamples[i].orientation.y();
            z += cbReferencePoseSamples[i].orientation.z();

            cov_orientation += cbReferencePoseSamples[i].cov_orientation;
        }

        /** Mean Position **/
        reference_pose.position = position/cbReferencePoseSize;
        reference_pose.cov_position = cov_position/cbReferencePoseSize;
        //base::guaranteeSPD< Eigen::Matrix<double, 3, 3> > (reference_pose.cov_position);

        /** Mean Orientation **/
        w = w/cbReferencePoseSize; y = y/cbReferencePoseSize;
        x = x/cbReferencePoseSize; z = z/cbReferencePoseSize;
        reference_pose.orientation = Eigen::Quaterniond(w, x, y, z);
        reference_pose.orientation.normalize();

        reference_pose.cov_orientation = cov_orientation/cbReferencePoseSize;
        //base::guaranteeSPD< Eigen::Matrix<double, 3, 3> > (reference_pose.cov_orientation);

        /** Set the time **/
        reference_pose.time = (cbReferencePoseSamples[cbReferencePoseSize-1].time + cbReferencePoseSamples[0].time)/2.0;

        /** Push the result into the buffer **/
        referencePoseSamples.push_front(reference_pose);
    }


    /** ************ **/
    /** IMU samples **/
    /** ************ **/
    imu.acc.setZero();
    imu.gyro.setZero();
    imu.mag.setZero();

    /** Process the buffer **/
    for (register unsigned int i=0; i<cbImuSamples.size(); ++i)
    {
	imu.acc += cbImuSamples[i].acc;
	imu.gyro += cbImuSamples[i].gyro;
	imu.mag += cbImuSamples[i].mag;
    }

    /** Set the time **/
    if (cbImuSamples.size() > 0)
    {
        imu.time = (cbImuSamples[cbImuSize-1].time + cbImuSamples[0].time)/2.0;

        /** Set the mean of this time interval **/
        imu.acc /= cbImuSize;
        imu.gyro /= cbImuSize;
        imu.mag /= cbImuSize;

        /** Push the result into the buffer **/
        imuSamples.push_front(imu);
    }


    /** Set all counters to zero **/
    counter.reset();

    return;
}

void Task::calculateVelocities()
{
    double delta_t = (1.0/proprioceptive_output_frequency);

    /** Joint velocities for the vector **/
    register int jointIdx = 0;
    base::samples::Joints joints = jointsSamples[0];
    base::samples::Joints prev_joints;

    if (static_cast<int>(jointsSamples.size()) > 1)
    {
        prev_joints = jointsSamples[1];
    }
    else
    {
        prev_joints = jointsSamples[0];
    }

    for(std::vector<std::string>::const_iterator it = joints.names.begin();
        it != joints.names.end(); it++)
    {

        base::JointState const &joint_state(joints[*it]);

        #ifdef DEBUG_PRINTS
        std::cout<<"[CALCULATING_VELO] ********************************************* \n";
        std::cout<<"[CALCULATING_VELO] Joint Name:"<<*it <<"\n";
        #endif

        /** Calculate speed in case there is not speed information **/
        if (!joint_state.hasSpeed())
        {
            base::JointState const &prev_joint_state(prev_joints[*it]);
            base::Time jointsDelta_t = joints.time - prev_joints.time;

            #ifdef DEBUG_PRINTS

            std::cout<<"[CALCULATING_VELO] Encoder Timestamp New: "<< joints.time.toMicroseconds() <<" Timestamp Prev: "<<prev_joints.time.toMicroseconds()<<"\n";
            std::cout<<"[CALCULATING_VELO] Delta time(joints): "<< jointsDelta_t.toSeconds()<<"\n";
            std::cout<<"[CALCULATING_VELO] Delta proprioceptive_output_frequency: "<< delta_t<<"\n";
            std::cout<<"[CALCULATING_VELO] Delta position(joints): "<< prev_joint_state.position<<" - "<<joint_state.position<<" = "<< prev_joint_state.position-joint_state.position <<"\n";
            #endif

            /** At least two values to perform the derivative **/
            joints[jointIdx].speed = (joint_state.position - prev_joint_state.position)/std::max(jointsDelta_t.toSeconds(),delta_t);
        }
        else
        {
            joints[jointIdx].speed = joint_state.speed;
        }

        #ifdef DEBUG_PRINTS
        std::cout<<"[CALCULATING_VELO] ["<<jointIdx<<"] joint speed: "<< jointsSamplesOut[jointIdx].speed <<"\n";
        std::cout<<"[CALCULATING_VELO] ["<<jointIdx<<"] jointsSamples old velocity method: "<<(joints[jointIdx].position - prev_joints[jointIdx].position)/delta_t<<"\n";
        std::cout<<"[CALCULATING_VELO] ********************************************* \n";
        #endif

        /** Set to Zero position joints **/
        std::vector<std::string>::const_iterator zerop = find(zero_position_joint_names.begin(), zero_position_joint_names.end(), *it);
        if (zerop != zero_position_joint_names.end())
        {
            joints[jointIdx].position = 0.00;
        }

        /** Set to Zero speed joints **/
        std::vector<std::string>::const_iterator zeros = find(zero_speed_joint_names.begin(), zero_speed_joint_names.end(), *it);
        if (zeros != zero_speed_joint_names.end())
        {
            joints[jointIdx].speed = 0.00;
        }


        jointIdx++;
    }
    /*****************************/
    /** Store the Joint values  **/
    /*****************************/
    jointsSamplesOut = joints;

    /** Mean velocities for the Reference Pose **/
    if (static_cast<int>(referencePoseSamples.size()) > 1)
    {
        /** Linear Velocities **/
        if (!::base::samples::RigidBodyState::isValidValue(referencePoseSamples[0].velocity))
        {
            /** Array of zero is the newest sample. delta_Tnavigation_body/delta_t **/
            referencePoseSamples[0].velocity = (referencePoseSamples[0].position - referencePoseSamples[referencePoseSamples.size()-1].position)/((referencePoseSamples.size()-1)*delta_t);
        }

        if (!::base::samples::RigidBodyState::isValidCovariance(referencePoseSamples[0].cov_velocity))
        {
            /** Array of zero is the newest sample. Velocity is the difference in pose but the uncertainty increases **/
            referencePoseSamples[0].cov_velocity = (referencePoseSamples[0].cov_position  + referencePoseSamples[referencePoseSamples.size()-1].cov_position)/((referencePoseSamples.size()-1) * delta_t * delta_t);
        }

        /** Angular Velocities **/
        if (!::base::samples::RigidBodyState::isValidValue(referencePoseSamples[0].angular_velocity))
        {
            /** Array of zero is the newest sample. delta_qnavigation_body = qnavigation_body_k-1 * angular_velocity_body_k-1_body **/
            Eigen::AngleAxisd deltaAngleaxis(referencePoseSamples[referencePoseSamples.size()-1].orientation.inverse() * referencePoseSamples[0].orientation);//quaternion_body_k-1_body
            referencePoseSamples[0].angular_velocity = referencePoseSamples[referencePoseSamples.size()-1].orientation * (deltaAngleaxis.angle() * deltaAngleaxis.axis())/((referencePoseSamples.size()-1)*delta_t);
        }

        if (!::base::samples::RigidBodyState::isValidCovariance(referencePoseSamples[0].cov_angular_velocity))
        {
            /** Array of zero is the newest sample. Velocity is the difference in pose but the uncertainty increases **/
            referencePoseSamples[0].cov_angular_velocity = (referencePoseSamples[0].cov_orientation  + referencePoseSamples[referencePoseSamples.size()-1].cov_orientation)/((referencePoseSamples.size()-1) * delta_t * delta_t);
        }
    }

    return;
}

bool Task::searchURDFJointNames(urdf::LinkConstSharedPtr link, const std::string &name_to_search,
                                Eigen::Vector3d &translation)
{
    double r, p, y;
    for (std::vector< std::shared_ptr <urdf::Link> >::const_iterator child =
            link->child_links.begin(); child != link->child_links.end();
            ++child)
    {
        (*child)->parent_joint->parent_to_joint_origin_transform.rotation.getRPY(r,p,y);

        //std::cout<<"segment name: "<<link->name<<"\n";
        //std::cout<<"joint name: "<<(*child)->parent_joint->name<<"\n";
        if (link->name.compare(name_to_search) == 0)
        {
            translation[0] = (*child)->parent_joint->parent_to_joint_origin_transform.position.x;
            translation[1] = (*child)->parent_joint->parent_to_joint_origin_transform.position.y;
            translation[2] = (*child)->parent_joint->parent_to_joint_origin_transform.position.z;

            //std::cout<<"FOUND\n"<<translation<<"\n";
            return true;
        }

        if(searchURDFJointNames(*child, name_to_search, translation))
            return true;
    }

    return false;
}

void Task::joints_samplesUnpack(const ::base::samples::Joints &original_joints,
                                const std::vector<std::string> &order_names,
                                std::vector<double> &joint_positions)
{
    for(std::vector<std::string>::const_iterator it = order_names.begin(); it != order_names.end(); it++)
    {
        base::JointState const &state(original_joints[*it]);

        /** Avoid NaN values in position **/
        if (std::isfinite(state.position))
            joint_positions.push_back(state.position);
        else
            joint_positions.push_back(0.00);

    }
    return;
}

void Task::computeWeightingMatrixDiagonal(const ::base::samples::Joints &robot_joints,
                                const Eigen::Quaterniond &orientation,
                                Eigen::Matrix<double, ::exoter_dynamics::NUMBER_OF_WHEELS, 1> &forces,
                                base::VectorXd &matrix_diagonal)
{
    /** Get joints position and velocity ordered by Motion Model joint names **/
    std::vector<double> joint_positions;
    this->joints_samplesUnpack(robot_joints, _all_joint_names.value(), joint_positions);

    /** Forward Kinematics in order to set Contact Points positions **/
    std::vector<Eigen::Affine3d> transformations;
    std::vector<base::Matrix6d> covariances;
    this->robot_kinematics->fkSolver(joint_positions, _contact_point_segments.value(), transformations, covariances);

    /** Store the translation part of the wheel position/contact points **/
    std::vector< Eigen::Matrix<double, 3, 1>,
        Eigen::aligned_allocator < Eigen::Matrix<double, 3, 1> > > wheel_positions;

    std::vector<Eigen::Affine3d>::const_iterator it_trans = transformations.begin();
    for (; it_trans != transformations.end(); ++it_trans)
    {
       wheel_positions.push_back((*it_trans).translation());
    }

    /** Compute the reaction forces **/
    this->exoter_rf.forceAnalysis(base::Vector3d::Zero(), wheel_positions, orientation, 1.0, forces);

    /** Form the weighting matrix diagonal (6 times the number of wheels) **/
    matrix_diagonal.resize(6*::exoter_dynamics::NUMBER_OF_WHEELS, 1);
    matrix_diagonal.setZero();

    for (register unsigned int i=0; i < ::exoter_dynamics::NUMBER_OF_WHEELS; ++i)
    {
        matrix_diagonal.block(6*i, 0, 6, 1) =  forces[i] * Eigen::Matrix<double, 6, 1>::Ones();
    }

    return;
}


void Task::outputPortSamples()
{
    std::vector<Eigen::Affine3d> fkRobot;

    /*******************************************/
    /** Port out the Output Ports information **/
    /*******************************************/

    /** Joint samples out **/
    base::samples::Joints jointsOut;
    jointsOut = jointsSamplesOut;
    jointsOut.time = jointsSamples[0].time;

    _joints_samples_out.write(jointsOut);

    /** Calibrated and compensate inertial values **/
    inertialSamplesOut = imuSamples[0];
    _inertial_samples_out.write(inertialSamplesOut);

    /** Orientation Samples  **/
    orientationSamples[0].sourceFrame = _orientation_source_frame.get();
    orientationSamples[0].targetFrame = _orientation_target_frame.get();
    _orientation_samples_out.write(orientationSamples[0]);

    /** Compute reaction forces weighting matrix **/
    base::VectorXd matrix_diagonal;
    Eigen::Matrix<double, ::exoter_dynamics::NUMBER_OF_WHEELS, 1> forces;
    this->computeWeightingMatrixDiagonal(jointsSamplesOut, orientationSamples[0].orientation, forces, matrix_diagonal);
    _reaction_forces_samples_out.write(forces);
    _weighting_samples_out.write(matrix_diagonal);

    /** Ground Truth if available **/
    if (_pose_reference_samples.connected())
    {
        /** Port Out the info coming from the ground truth **/
        /** NOTE: Position and orientation values are wrt the local navigation frame (where localization front-end started) **/
        referenceOut.time = referencePoseSamples[0].time;
        referenceOut.position = referencePoseSamples[0].position;
        referenceOut.cov_position = referencePoseSamples[0].cov_position;
        referenceOut.orientation = referencePoseSamples[0].orientation;
        referenceOut.cov_orientation = referencePoseSamples[0].cov_orientation;

        /** NOTE: Linear and angular velocities are wrt the local navigation frame (where localization front-end started) **/
        referenceOut.velocity = referencePoseSamples[0].velocity;
        referenceOut.cov_velocity = referencePoseSamples[0].cov_velocity;
        referenceOut.angular_velocity = referencePoseSamples[0].angular_velocity;
        referenceOut.cov_angular_velocity = referencePoseSamples[0].cov_angular_velocity;
        _pose_reference_samples_out.write(referenceOut);

        /** Delta increments of the ground truth at delta_t given by the output_frequency **/
        /** NOTE: Linear and Angular velocities are wrt the local robot body frame **/
        /** NOTE: Here we use subtraction between covariances because we are interested in the delta covariance. Dangerous, it might be negative variance in case it decreases **/
        delta_referenceOut.time = referencePoseSamples[0].time;
        delta_referenceOut.position = referencePoseSamples[1].orientation.inverse() * (referencePoseSamples[0].position - referencePoseSamples[1].position);//position_k-1_k = (qnavigation_body_k-1)^-1 * (position_navigation_k - position_navigation_k-1)
        Eigen::Affine3d qnavigation_body_k_1(referencePoseSamples[1].orientation);
        delta_referenceOut.cov_position = qnavigation_body_k_1.rotation() * (referencePoseSamples[0].cov_position - referencePoseSamples[1].cov_position) * qnavigation_body_k_1.rotation().transpose();
        delta_referenceOut.orientation = referencePoseSamples[1].orientation.inverse() * referencePoseSamples[0].orientation; //delta quaternion = (T_k-1)^-1 * Tk
        delta_referenceOut.cov_orientation = qnavigation_body_k_1.rotation() * (referencePoseSamples[0].cov_orientation - referencePoseSamples[1].cov_orientation) * qnavigation_body_k_1.rotation().transpose();
        delta_referenceOut.velocity = referencePoseSamples[0].orientation.inverse() * referencePoseSamples[0].velocity;
        Eigen::Affine3d qnavigation_body_k(referencePoseSamples[0].orientation);
        delta_referenceOut.cov_velocity = qnavigation_body_k.rotation() * referencePoseSamples[0].cov_velocity * qnavigation_body_k.rotation().transpose();
        delta_referenceOut.angular_velocity = referencePoseSamples[0].orientation.inverse() * referencePoseSamples[0].angular_velocity;
        delta_referenceOut.cov_angular_velocity = qnavigation_body_k.rotation() * referencePoseSamples[0].cov_velocity * qnavigation_body_k.rotation().transpose();
        _delta_pose_reference_samples_out.write(delta_referenceOut);
    }

    /** Port-out the estimated world 2 navigation transform **/
    world2navigationRbs.time = jointsSamplesOut.time;//timestamp;
    _world_to_navigation_out.write(world2navigationRbs);

    /** Port-out the estimated world_osg 2 world transform **/
    world_osg2worldRbs.time = jointsSamplesOut.time;//timestamp;
    _world_osg_to_world_out.write(world_osg2worldRbs);

    #ifdef DEBUG_PRINTS
    std::cout<<"[EXOTER OUTPUT_PORTS]: world2navigationRbs.position\n"<<world2navigationRbs.position<<"\n";
    std::cout<<"[EXOTER OUTPUT_PORTS]: world2navigationRbs.velocity\n"<<world2navigationRbs.velocity<<"\n";
    std::cout<<"[EXOTER OUTPUT_PORTS]: referenceOut.position\n"<<referenceOut.velocity<<"\n";
    std::cout<<"[EXOTER OUTPUT_PORTS] ******************** END ******************** \n";
    #endif

    /** The Debug OutPorts information **/
    if (_output_debug.value())
    {
        _angular_position.write(jointsSamplesOut[13].position);
        _angular_rate.write(jointsSamplesOut[13].speed); //!Front Left
    }

    return;
}

