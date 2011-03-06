// nu_skeletaltracker.cpp
// Jake Ware and Jarvis Schultz
// Winter 2011


//---------------------------------------------------------------------------
// Notes
//---------------------------------------------------------------------------

// changed sleep(10) to sleep(5)


//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------

#include <ros/ros.h>
#include <ros/package.h>

#include <tf/transform_broadcaster.h>
#include <kdl/frames.hpp>

#include <mapping_msgs/PolygonalMap.h>
#include <geometry_msgs/Polygon.h>
#include <nu_skeletonmsgs/Skeletons.h>

#include <XnOpenNI.h>
#include <XnCodecIDs.h>
#include <XnCppWrapper.h>

#include <math.h>

//---------------------------------------------------------------------------
// Global Variables
//---------------------------------------------------------------------------

using std::string;

xn::Context        g_Context;  
xn::DepthGenerator g_DepthGenerator;  
xn::UserGenerator  g_UserGenerator;  

XnBool g_bNeedPose   = false;  
XnChar g_strPose[20] = ""; 
XnBool g_bDrawBackground = true;
XnBool g_bDrawPixels = true;
XnBool g_bDrawSkeleton = true;
XnBool g_bPrintID = true;
XnBool g_bPrintState = true; 
XnBool g_bPause = false;
XnBool g_bRecord = false;

ros::Publisher pmap_pub;
ros::Publisher skel_pub;


//---------------------------------------------------------------------------
// NITE Functions
//---------------------------------------------------------------------------

// Callback: New user was detected
// mit and ros used slightly different versions of this function by eliminating different if/else components
void XN_CALLBACK_TYPE User_NewUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie) {
  printf("New User %d\n", nId); 

  if (g_bNeedPose)
    g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
  else
    g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}


// Callback: An existing user was lost
// same
void XN_CALLBACK_TYPE User_LostUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie) {
  printf("Lost user %d\n", nId);  
}


// Callback: Started calibration
// same
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(xn::SkeletonCapability& capability, XnUserID nId, void* pCookie) {
  printf("Calibration started for user %d\n", nId);
}


// Callback: Finished calibration
// slightly modified
void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(xn::SkeletonCapability& capability, XnUserID nId, XnBool bSuccess, void* pCookie) {
  if (bSuccess) {
    printf("Calibration complete, start tracking user %d\n", nId);  
    // missing two lines here
    g_UserGenerator.GetSkeletonCap().StartTracking(nId); 
  }
  else {
    printf("Calibration failed for user %d\n", nId); 

    if (g_bNeedPose)
      g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
    else
      g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
  }
}


// Callback: Detected a pose
// missing if/else statements and a few lines
void XN_CALLBACK_TYPE UserPose_PoseDetected(xn::PoseDetectionCapability& capability, XnChar const* strPose, XnUserID nId, void* pCookie) {
  printf("Pose %s detected for user %d\n", strPose, nId);
  g_UserGenerator.GetPoseDetectionCap().StopPoseDetection(nId);
  g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE); 
}


//---------------------------------------------------------------------------
// MIT Functions
//---------------------------------------------------------------------------

// Point is a triplet of 64 bit floats
geometry_msgs::Point vecToPt(XnVector3D pt) {
   geometry_msgs::Point ret;
   ret.x=pt.X/1000.0;
   ret.y=-pt.Y/1000.0;
   ret.z=pt.Z/1000.0;
   return ret;
}

// Point32 is a triplet of 32 bit floats
geometry_msgs::Point32 vecToPt3(XnVector3D pt) {
   geometry_msgs::Point32 ret;
   ret.x=pt.X/1000.0;
   ret.y=-pt.Y/1000.0;
   ret.z=pt.Z/1000.0;
   return ret;
}


void getPolygon(XnUserID user, XnSkeletonJoint eJoint1, XnSkeletonJoint eJoint2, mapping_msgs::PolygonalMap &pmap) {
   XnSkeletonJointPosition joint1, joint2;
   g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(user, eJoint1, joint1);
   g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(user, eJoint2, joint2);

   if (joint1.fConfidence < 0.5 || joint2.fConfidence < 0.5) return;

   geometry_msgs::Polygon p;
   p.points.push_back(vecToPt3(joint1.position));
   p.points.push_back(vecToPt3(joint2.position));

   // NOT SURE IF THIS IS RIGHT
   pmap.polygons.push_back(p);
}


//---------------------------------------------------------------------------
// NU Functions
//---------------------------------------------------------------------------

void getTransform(XnUserID user, XnSkeletonJoint name, string const& child_frame_id, nu_skeletonmsgs::SkeletonJoint &j) {
  static tf::TransformBroadcaster br;
  geometry_msgs::Point position;

  XnSkeletonJointPosition joint;
  g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(user, name, joint);
  position = vecToPt(joint.position);
  j.confidence = joint.fConfidence;

  XnSkeletonJointOrientation joint_orientation;
  g_UserGenerator.GetSkeletonCap().GetSkeletonJointOrientation(user, name, joint_orientation);

  XnFloat* m = joint_orientation.orientation.elements;
  m[0] = -m[0];
  m[1] = -m[1];
  m[2] = -m[2];
  m[6] = -m[6];
  m[7] = -m[7];
  m[8] = -m[8];
    
  KDL::Rotation rotation(m[0], m[1], m[2],
			 m[3], m[4], m[5],
			 m[6], m[7], m[8]);

  double qx, qy, qz, qw;
  rotation.GetQuaternion(qx, qy, qz, qw);

  tf::Transform transform;
  transform.setOrigin(tf::Vector3(position.x, position.y, position.z));
  transform.setRotation(tf::Quaternion(qx, qy, qz, qw));

  j.transform.translation.x = transform.getOrigin().x();
  j.transform.translation.y = transform.getOrigin().y();
  j.transform.translation.z = transform.getOrigin().z();
  
  j.transform.rotation.x = transform.getRotation().x();
  j.transform.rotation.y = transform.getRotation().y();
  j.transform.rotation.z = transform.getRotation().z();
  j.transform.rotation.w = transform.getRotation().w();

  br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "openni_depth_optical_frame", child_frame_id));
}


void getData(XnUserID const& user, nu_skeletonmsgs::Skeletons &skels, std::vector<mapping_msgs::PolygonalMap> &pmaps) {
  mapping_msgs::PolygonalMap pmap;
  getPolygon(user, XN_SKEL_HEAD, XN_SKEL_NECK, pmap);
  getPolygon(user, XN_SKEL_NECK, XN_SKEL_LEFT_SHOULDER, pmap);
  getPolygon(user, XN_SKEL_LEFT_SHOULDER, XN_SKEL_LEFT_ELBOW, pmap);
  getPolygon(user, XN_SKEL_LEFT_SHOULDER, XN_SKEL_RIGHT_SHOULDER, pmap);
  getPolygon(user, XN_SKEL_LEFT_ELBOW, XN_SKEL_LEFT_HAND, pmap);
  getPolygon(user, XN_SKEL_NECK, XN_SKEL_RIGHT_SHOULDER, pmap);
  getPolygon(user, XN_SKEL_RIGHT_SHOULDER, XN_SKEL_RIGHT_ELBOW, pmap);
  getPolygon(user, XN_SKEL_RIGHT_ELBOW, XN_SKEL_RIGHT_HAND, pmap);
  getPolygon(user, XN_SKEL_LEFT_SHOULDER, XN_SKEL_TORSO, pmap);
  getPolygon(user, XN_SKEL_RIGHT_SHOULDER, XN_SKEL_TORSO, pmap);
  getPolygon(user, XN_SKEL_TORSO, XN_SKEL_LEFT_HIP, pmap);
  getPolygon(user, XN_SKEL_LEFT_HIP, XN_SKEL_LEFT_KNEE, pmap);
  getPolygon(user, XN_SKEL_LEFT_KNEE, XN_SKEL_LEFT_FOOT, pmap);
  getPolygon(user, XN_SKEL_TORSO, XN_SKEL_RIGHT_HIP, pmap);
  getPolygon(user, XN_SKEL_RIGHT_HIP, XN_SKEL_RIGHT_KNEE, pmap);
  getPolygon(user, XN_SKEL_RIGHT_KNEE, XN_SKEL_RIGHT_FOOT, pmap);
  getPolygon(user, XN_SKEL_LEFT_HIP, XN_SKEL_RIGHT_HIP, pmap);
  pmaps.push_back(pmap);

  nu_skeletonmsgs::Skeleton skel;
  skel.userid=user;
  getTransform(user, XN_SKEL_HEAD, "head", skel.head);
  getTransform(user, XN_SKEL_NECK, "neck", skel.neck);
  getTransform(user, XN_SKEL_TORSO, "torso", skel.torso);
  getTransform(user, XN_SKEL_LEFT_SHOULDER, "left_shoulder", skel.left_shoulder);
  getTransform(user, XN_SKEL_LEFT_ELBOW, "left_elbow", skel.left_elbow);
  getTransform(user, XN_SKEL_LEFT_HAND, "left_hand", skel.left_hand);
  getTransform(user, XN_SKEL_RIGHT_SHOULDER, "right_shoulder", skel.right_shoulder);
  getTransform(user, XN_SKEL_RIGHT_ELBOW, "right_elbow", skel.right_elbow);
  getTransform(user, XN_SKEL_RIGHT_HAND, "right_hand", skel.right_hand);
  getTransform(user, XN_SKEL_LEFT_HIP, "left_hip", skel.left_hip);
  getTransform(user, XN_SKEL_LEFT_KNEE, "left_knee", skel.left_knee);
  getTransform(user, XN_SKEL_LEFT_FOOT,  "left_foot", skel.left_foot);
  getTransform(user, XN_SKEL_RIGHT_HIP, "right_hip", skel.right_hip);
  getTransform(user, XN_SKEL_RIGHT_KNEE, "right_knee", skel.right_knee);
  getTransform(user, XN_SKEL_RIGHT_FOOT, "right_foot", skel.right_foot);
  skels.skeletons.push_back(skel);
}

 
void publishData() {
  ros::Time tstamp=ros::Time::now();

  nu_skeletonmsgs::Skeletons skels;
  std::vector<mapping_msgs::PolygonalMap> pmaps;

  XnUserID users[15];
  XnUInt16 users_count = 15;
  g_UserGenerator.GetUsers(users, users_count);

  for (int i = 0; i < users_count; ++i) {
    XnUserID user = users[i];
    if (!g_UserGenerator.GetSkeletonCap().IsTracking(user)) continue;

    getData(user, skels, pmaps);

    if(pmaps.size()) {
      skels.header.stamp=tstamp;
      skels.header.frame_id="/openni_depth_optical_frame";
      skel_pub.publish(skels);
      pmaps.front().header.stamp=tstamp;
      pmaps.front().header.frame_id="/openni_depth_optical_frame";
      pmap_pub.publish(pmaps[0]);
    }
  }
}


//---------------------------------------------------------------------------
// Macros
//---------------------------------------------------------------------------

// NEEDS COMMENT
// this is a macro and needs the backslashes at the end of the line
#define CHECK_RC(nRetVal, what)						\
  if (nRetVal != XN_STATUS_OK)						\
    {									\
      printf("%s failed: %s\n", what, xnGetStatusString(nRetVal));	\
      return nRetVal;							\
    }


//---------------------------------------------------------------------------
// MAIN
//---------------------------------------------------------------------------

int main(int argc, char **argv) 
{
  sleep(5);  // delay for openni_camera to start

  // startup node
  ros::init(argc, argv, "nu_skeletontracker");
  ros::NodeHandle nh_;

  // define ros publishers for skels
  pmap_pub = nh_.advertise<mapping_msgs::PolygonalMap> ("skeletonpmaps", 100);
  skel_pub = nh_.advertise<nu_skeletonmsgs::Skeletons> ("skeletons", 100);

  // init from XML
  string configFilename = ros::package::getPath("nu_skeletontracker") + "/nu_skeletontracker.xml";
  XnStatus nRetVal = g_Context.InitFromXmlFile(configFilename.c_str());
  CHECK_RC(nRetVal, "InitFromXml"); 

  // find depth generator
  nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_DepthGenerator); 
  CHECK_RC(nRetVal, "Find depth generator");

  // find user generator
  nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_USER, g_UserGenerator);
  if (nRetVal != XN_STATUS_OK) {
    nRetVal = g_UserGenerator.Create(g_Context);
    CHECK_RC(nRetVal, "Find user generator");
  }

  // check user generator support
  if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON)) {
    printf("Supplied user generator doesn't support skeleton\n");
    return 1;
  }

  // needs comment
  XnCallbackHandle hUserCallbacks;
  g_UserGenerator.RegisterUserCallbacks(User_NewUser, User_LostUser, NULL, hUserCallbacks);
  XnCallbackHandle hCalibrationCallbacks;
  g_UserGenerator.GetSkeletonCap().RegisterCalibrationCallbacks(UserCalibration_CalibrationStart, UserCalibration_CalibrationEnd, NULL, hCalibrationCallbacks);

  // check if pose if supported
  if (g_UserGenerator.GetSkeletonCap().NeedPoseForCalibration()) {
    g_bNeedPose = TRUE;
    if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)) {
      printf("Pose required, but not supported\n");
      return 1;
    }

    // needs comment
    XnCallbackHandle hPoseCallbacks;
    g_UserGenerator.GetPoseDetectionCap().RegisterToPoseCallbacks(UserPose_PoseDetected, NULL, NULL, hPoseCallbacks);
    g_UserGenerator.GetSkeletonCap().GetCalibrationPose(g_strPose);
  }

  g_UserGenerator.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);  // needs comment

  nRetVal = g_Context.StartGeneratingAll();
  CHECK_RC(nRetVal, "StartGenerating");

  // define loop frequency and enter ros loop
  ros::Rate r(30);
  while (ros::ok()) {
    g_Context.WaitAndUpdateAll();
    publishData();  // everything important happens in this function
    r.sleep();
  }

  g_Context.Shutdown();
  return 0;
}