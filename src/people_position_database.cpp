/*
2015.4.3----------------
メッセージをサブスクライブして、データベースに書き込むモジュール
現時点では書き込むのみ

*/

#include <ros/ros.h>
#include <iostream>
#include <mysql/mysql.h>
#include <time.h>

#include <tf/transform_listener.h>
#include <geometry_msgs/PointStamped.h>

#include "picojson.h"

#include "humans_msgs/PersonPoseImgArray.h"
#include "humans_msgs/Humans.h"
#include "humans_msgs/HumanSrv.h"
#include "humans_msgs/HumansSrv.h"

//#include "Util.hpp"

#include "okao_client/OkaoStack.h"
//#include "MsgToMsg.hpp"

#include <std_msgs/String.h>
#define HEAD 3

using namespace std;


class PeoplePositionDatabase
{
private:
  ros::NodeHandle nh;
  ros::NodeHandle nh_param;

  ros::Subscriber db_sub;
  //ros::ServiceServer tracking_id_srv;
  //ros::ServiceServer okao_id_srv;
  //ros::ServiceServer name_srv;
  tf::TransformListener tl;

  MYSQL *connector;

  string dbhost;
  string dbuser;
  string dbpass;
  string dbname;
  string dbtable;
  //string dbdata[100];

  time_t now;
  struct tm *pnow;

public:
  PeoplePositionDatabase():
    nh_param("~"),
    dbhost("127.0.0.1"),
    dbuser("root"),
    dbpass("robot15"),
    dbname("data1"),
    dbtable("mapping_db_test")
  {

    //init parameter
    nh_param.param("dbhost", dbhost, dbhost);
    nh_param.param("dbuser", dbuser, dbuser);
    nh_param.param("dbpass", dbpass, dbpass);
    nh_param.param("dbname", dbname, dbname);
    nh_param.param("dbtable", dbtable, dbtable);

    db_sub = nh.subscribe("/humans/recog_info", 1, 
			  &PeoplePositionDatabase::dbCallback, this);
    /*
    tracking_id_srv 
      = nh.advertiseService("tracking_id_srv", 
			  &PeoplePositionDatabase::resTrackingId, this);
    okao_id_srv 
      = nh.advertiseService("okao_id_srv", 
			    &PeoplePositionDatabase::resOkaoId, this);
    name_srv
      = nh.advertiseService("name_srv", 
			    &PeoplePositionDatabase::resName, this);
    */
    ROS_ASSERT(initDBConnector());
  }
  ~PeoplePositionDatabase()
  {
    ROS_ASSERT(shutdownDBConnector());
  }

private:
  bool initDBConnector()
  {
    ROS_INFO("connetor : init ok");

    connector = mysql_init(NULL);
    if(!mysql_real_connect(connector, dbhost.c_str(), dbuser.c_str(), dbpass.c_str(), dbname.c_str(), 3306, NULL, CLIENT_MULTI_STATEMENTS)) 
      {
	fprintf(stderr, "%s\n", mysql_error(connector));
	return false;
      }

    ROS_INFO("MySQL opend.");

    return true;
  }

  bool shutdownDBConnector()
  {
    mysql_close(connector);
    ROS_INFO("MySQL closed.");
    return true;
  }


  void transformPoint(geometry_msgs::PointStamped src, 
		      geometry_msgs::PointStamped *dst)
  {
   
    try
      {
	//notice ros::time!!
	
	tl.waitForTransform(dst->header.frame_id, src.header.frame_id, 
			    src.header.stamp, ros::Duration(3.0));
	
	tl.transformPoint(dst->header.frame_id, ros::Time(), 
			  src, src.header.frame_id, *dst);
      }
    catch(tf::TransformException& ex)
      {
	ROS_ERROR("Received an exception trying to transform a point to /map: %s", 
		  ex.what());
	//++waittime;
      }

  }

  void dbCallback(const humans_msgs::HumansConstPtr& msg)
  {
    now = time(NULL);
    pnow = localtime(&now);
    stringstream time_buff;
    
    time_buff << pnow->tm_year+1900 
	      <<"-"<< pnow->tm_mon+1 
	      << "-" << pnow->tm_mday
	      << " " << pnow->tm_hour 
	      <<":" << pnow->tm_min 
	      << ":" << pnow->tm_sec; 
    
    //data time long type	 
    /*
    stringstream mon, day, hour, min, sec;

    if(pnow->tm_mon+1 < 10)
      mon << "0" << pnow->tm_mon+1;
    else
      mon << pnow->tm_mon+1;

    if(pnow->tm_mday < 10)
      day << "0" << pnow->tm_mday;
    else
      day << pnow->tm_mday;

    if(pnow->tm_hour < 10)
      hour << "0" << pnow->tm_hour;
    else
      hour << pnow->tm_hour;

    if(pnow->tm_min < 10)
      min << "0" << pnow->tm_min;
    else
      min << pnow->tm_min;

    if(pnow->tm_sec < 10)
      sec << "0" << pnow->tm_sec;
    else
      sec << pnow->tm_sec;
    

    time_buff << pnow->tm_year+1900 
	      << mon.str() 
	      << day.str()
	      << hour.str() 
	      << min.str() 
	      << sec.str(); 

	      atol(time_buff.str().c_str());
    */

    //cout << time_buff.str().c_str() << endl;
    for(int i = 0; i < msg->human.size(); ++i)
      {
	geometry_msgs::PointStamped src, dst;
	src.header = msg->human[i].header;
	src.point = msg->human[i].p;
	dst.header.frame_id = "map";
	dst.header.stamp = ros::Time::now();
	transformPoint(src, &dst);

	if(msg->human[i].face.persons.size() != 0)
	  {
	    query_execute(time_buff.str(), dst.point, msg->human[i] );
	  }
      }
  }

  void query_execute(string datetime, geometry_msgs::Point pt, humans_msgs::Human src)
  {
    string joints_data;
    picojson::object obj_joints;

    //変換用
    double diff_x = pt.x - src.body.joints[HEAD].position.x;
    double diff_y = pt.y - src.body.joints[HEAD].position.y;
    double diff_z = pt.z - src.body.joints[HEAD].position.z;

    for(int j = 0; j < src.body.joints.size() ; ++j)
      {
	picojson::object position, orientation, joint;//, jx, jy,jz;
	
	joint.insert(make_pair("j_name", src.body.joints[j].joint_name));
	joint.insert(make_pair("t_state", src.body.joints[j].tracking_state));

	double jx = src.body.joints[j].position.x + diff_x;
	double jy = src.body.joints[j].position.y + diff_y;
	double jz = src.body.joints[j].position.z + diff_z;
	position.insert(make_pair("x", jx));
	position.insert(make_pair("y", jy));
	position.insert(make_pair("z", jz));
	joint.insert(make_pair("position", position));
	
	/*
	orientation.insert(make_pair("x", src.body.joints[j].orientation.x));
	orientation.insert(make_pair("y", src.body.joints[j].orientation.y));
	orientation.insert(make_pair("z", src.body.joints[j].orientation.z));
	orientation.insert(make_pair("w", src.body.joints[j].orientation.w));
	joint.insert(make_pair("orientation", orientation));
	*/
	stringstream j_name;
	j_name << "joint" << j;

	obj_joints.insert(make_pair(j_name.str(), joint));
      }
    picojson::value joints = picojson::value(obj_joints);
    joints_data = joints.serialize();

    stringstream insert_query;
    insert_query << "INSERT INTO "<< dbname.c_str() << "."
		 << dbtable.c_str() 
		 <<" (d_id, okao_id, hist, time_stamp, name, " 
		 << "laboratory, grade, tracking_id, px, py, pz, magni, "
		 << "joints"
		 << ") VALUES ("
		 << src.d_id<< ", "
		 << src.max_okao_id << ", " 
		 << src.max_hist << ", "
      //<< datetime << ", "
		 << "'" << datetime << "'" << ", " 
		 << "'" << src.face.persons[0].name.c_str() << "'" << ", "
		 << "'" << src.face.persons[0].laboratory.c_str() << "'" << ", "
		 << "'" << src.face.persons[0].grade.c_str() << "'" <<", "
		 << src.body.tracking_id << ", "
		 << pt.x <<", " 
		 << pt.y <<", "
		 << pt.z <<", "
		 << src.magni << ", "
		 << "'" << joints_data << "'"
		 << ");"; 
    
    if(mysql_query( connector, insert_query.str().c_str()))
      {
	ROS_ERROR("%s", mysql_error(connector));
	ROS_ERROR("DB write error.");
      }
    ROS_INFO("QUERY: %s", insert_query.str().c_str());
  }

};

int main(int argc, char** argv)
{
  ros::init(argc, argv, "people_position_database");
  PeoplePositionDatabase PPDObj;
  ros::spin();

  return 0;
}
