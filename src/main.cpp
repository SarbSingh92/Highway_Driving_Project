#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h" 

// for convenience
using nlohmann::json;
using std::string;
using std::vector;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }
  
  int lane = 1; //the car begins in lane 1 (default by simulator) 
  double dist_inc = 0.5; 
  double max_speed = .224; //This is the starting speed for the car 
  string state = "KL"; // Car begins in the Keep lane state  
  
  h.onMessage([&state, &lane, &max_speed, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    
	
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];
        

          json msgJson;

          //vector<double> next_x_vals;
          //vector<double> next_y_vals;
          
          int prev_size = previous_path_x.size(); 

          /**
           * TODO: define a path made up of (x,y) points that the car will visit
           *   sequentially every .02 seconds
           */
          if (prev_size > 0)
          {
            car_s = end_path_s; 
          }
          
          if (state == "KL")
          {
            //std::cout<<"KL"<<std::endl;
          
          	bool too_close = false; //flag to indicate if car infront of vehicle is too close 
            double check_speed; //stores speed of vehicles around the car
          	//find max_speed to use
          	for(int i = 0; i < sensor_fusion.size(); i++)
          	{
            	//car is in my lane
            	float d = sensor_fusion[i][6]; 
              	
            	if (d < (2+4*lane+2) && d > (2+4*lane-2)) //checks if cars are in the same lane 
            	{
              		double vx = sensor_fusion[i][3];
              		double vy = sensor_fusion[i][4];
              		check_speed = sqrt(vx*vx+vy*vy);
              		double check_car_s = sensor_fusion[i][5]; 
              		//if using previous points can project s value out 
              		check_car_s += ((double)prev_size*0.02*check_speed); 
              		//check s values greater than mine and s gap
              		if((check_car_s > car_s) && ((check_car_s - car_s) < 30)) //if car is with 30m than set too close flag
              		{
                		too_close = true;   
              		}
            	}
          	}
          
          	if(too_close && car_speed > check_speed && car_speed > 30) // decrease speed if too close to the speed of the car ahead but not below 30MPH 
          	{
            	max_speed -= .4; 
          	}
          	else if (max_speed < 49.5) //increase speed if it is below the max limit 
          	{
           	 max_speed += .4; 
          	}
            //check if speed is below 20% of limit. If yes, change state to see if Lane change is possible 
            if (too_close == true && max_speed < 40)
            {
              state = "PLC"; //State change to prep lane change 
            }
          }
          
    
          //Points that represent the cars path. Will be fitted to a spline
          vector<double> ptsx; 
          vector<double> ptsy; 
          //reference variables set to where the car is but may be adjusted depending on how many previous points there are 
          double ref_x = car_x; 
          double ref_y = car_y; 
          double ref_yaw = deg2rad(car_yaw); 
          
          //if no previous points, than use cars location as starting poiint
          if (prev_size < 2)
          {
            double prev_car_x = car_x - cos(car_yaw); 
            double prev_car_y = car_y - sin(car_yaw); 
            
            ptsx.push_back(prev_car_x);
            ptsx.push_back(car_x); 
            
            ptsy.push_back(prev_car_y); 
            ptsy.push_back(car_y); 
          }
          
          else
          {
            ref_x = previous_path_x[prev_size-1]; 
            ref_y = previous_path_y[prev_size-1]; 
           
            double ref_x_prev = previous_path_x[prev_size-2];
            double ref_y_prev = previous_path_y[prev_size-2];
            
            ref_yaw = atan2(ref_y-ref_y_prev, ref_x-ref_x_prev); 
            
            ptsx.push_back(ref_x_prev);
            ptsx.push_back(ref_x); 
            
            ptsy.push_back(ref_y_prev); 
            ptsy.push_back(ref_y); 
          }
          
          double trans_car_s; 
          
          if ( state == "PLC") //Prepare lane change state 
          {
            //std::cout<<"PLC"<<std::endl;
            //Use Sensor Fusion data to check left and right lane for cars. If cars are not within a buffer zone, switch state to lane change left or right
           
            //track cost of lane change. Only a cost of zero will result in change 
            int right_cost = 0; 
            int left_cost = 0; 
            
            if (lane != 2) //if not in right most lane, check if right lane change is possible 
            {
               
                for(int i = 0; i < sensor_fusion.size(); i++)
              	{
                  //are there cars in lane to the right
                  float d = sensor_fusion[i][6]; 
                  //std::cout<<"car d for right lane = "<<d<<std::endl;
                  if (d < ((4*lane+8)) && d > ((4*lane+4)))
                  {
                      double vx = sensor_fusion[i][3];
                      double vy = sensor_fusion[i][4];
                      double check_speed = sqrt(vx*vx+vy*vy);
                      double check_car_s = sensor_fusion[i][5]; 
                      //if using previous points can project s value out 
                      check_car_s += ((double)prev_size*0.02*check_speed);
                      //std::cout<<"check cas s = "<<check_car_s<<std::endl;
                      //std::cout<<"car s = "<<car_s<<std::endl;
                      //check if car is outside of space needed for lane change 
                      if((check_car_s - car_s) < 40 && (check_car_s - car_s) > -10) //Increase cost if cars are within 40m ahead and 10m behind car
                      {
                          //increase cost if car in right lane is less than range expected
                          //std::cout<<"cost up"<<std::endl;
                          right_cost += 1;    
                      }            
                  }
               }
            }
            else //if in the right most lane, increase cost 
            {
              right_cost+=1; 
            }
            if (lane != 0)//if not in left most lane, check if left lane change is possible 
            {
             
              for(int i = 0; i < sensor_fusion.size(); i++)
              	{
                  //are there cars in lane to the left
                  float d = sensor_fusion[i][6]; 
                  //std::cout<<"car d for left lane = "<<d<<std::endl;
                  //std::cout<<"my lane is  = "<<lane<<std::endl;
                  if (d > ((4*lane-4)) && d < (4*lane))
                  {
                      double vx = sensor_fusion[i][3];
                      double vy = sensor_fusion[i][4];
                      double check_speed = sqrt(vx*vx+vy*vy);
                      double check_car_s = sensor_fusion[i][5]; 
                      //if using previous points can project s value out 
                      check_car_s += ((double)prev_size*0.02*check_speed); 
                      //check s values greater than mine and s gap
                      //std::cout<<"check cas s = "<<check_car_s<<std::endl;
                      //std::cout<<"car s = "<<car_s<<std::endl;
                      if((check_car_s - car_s) < 40 && (check_car_s - car_s) > -10)
                      {
                          //increase cost if car in right lane is less than range expected 
                          std::cout<<"cost up"<<std::endl;
                          left_cost += 1;    
                      }            
                  }
              }
            }
            else
            {
              left_cost +=1; 
            }
            //set appropriate state depending on costs caluclated and set lane variable 
            if(right_cost == 0)
            {
            	lane +=1;
                
                state = "LCR";
            }
            else if (left_cost == 0)
            {
                lane -=1; 
                
                state = "LCL";
            }
            else
            {
                state = "KL"; 
            }
          }
          
          if(state == "LCR")
          {
            //std::cout<<"LCR"<<std::endl; 
            //std::cout<<"car d = "<<car_d<<std::endl; 
            //std::cout<<"lane ="<<4*lane+2<<std::endl; 
            if(car_d < (2+4*lane+2) && car_d > (2+4*lane-2)) // exit lane change state once car is well into the next lane 
            {
              //std::cout<<"entered"<<std::endl;
              state = "KL";
            }
          }
          
          if(state == "LCL")
          {
            //std::cout<<"LCL"<<std::endl;
            //std::cout<<"car d = "<<car_d<<std::endl; 
            //std::cout<<"lane ="<<4*lane+2<<std::endl; 
            if(car_d < (2+4*lane+1) && car_d > (2+4*lane-1))
            {
              //std::cout<<"entered"<<std::endl;
              state = "KL";
            }
          }
         
          //In Frenet add 30m evenly spaced points ahead of the car 
          vector<double> waypoint_30 = getXY(car_s+30, (lane*4+2), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> waypoint_60 = getXY(car_s+60, (lane*4+2), map_waypoints_s, map_waypoints_x, map_waypoints_y);
          vector<double> waypoint_90 = getXY(car_s+90, (lane*4+2), map_waypoints_s, map_waypoints_x, map_waypoints_y);
 
          ptsx.push_back(waypoint_30[0]);
          ptsx.push_back(waypoint_60[0]); 
          ptsx.push_back(waypoint_90[0]); 
            
          ptsy.push_back(waypoint_30[1]); 
          ptsy.push_back(waypoint_60[1]); 
          ptsy.push_back(waypoint_90[1]);
          
          //shift car reference angle to 0 so that spline does not get multiple x values for single y (verticle spline) 
          
          for ( int i = 0; i < ptsx.size(); i++)
          {
            double shift_x = ptsx[i]-ref_x; 
            double shift_y = ptsy[i]-ref_y; 
            
            ptsx[i] = (shift_x*cos(0-ref_yaw)-shift_y*sin(0-ref_yaw));
            ptsy[i] = (shift_x*sin(0-ref_yaw)+shift_y*cos(0-ref_yaw)); 
          }
          
          tk::spline s; 
          //set (x,y) points to the spline 
          s.set_points(ptsx,ptsy);  
          
          //Define the actual (x,y) that the car will use
          vector<double> next_x_vals; 
          vector<double> next_y_vals; 
          
          for( int i = 0; i < previous_path_x.size(); i++)
          {
            next_x_vals.push_back(previous_path_x[i]); 
            next_y_vals.push_back(previous_path_y[i]); 
          }
          
          double target_x = 30.0; 
          double target_y = s(target_x); 
          double target_dist = sqrt((target_x)*(target_x)+(target_y)*(target_y)); 
          
          double x_add_on = 0; 
          //generate the trajectory 
          for ( int i = 1; i <= 50 - previous_path_x.size(); i++)
          {
            double N = (target_dist/(0.02*max_speed/2.24)); 
            double x_point = x_add_on+(target_x)/N; 
            double y_point = s(x_point); 
            
            x_add_on = x_point; 
            
            double x_ref = x_point; 
            double y_ref = y_point; 
            
            //rotate back
            x_point = (x_ref *cos(ref_yaw) -y_ref *sin(ref_yaw));
            y_point = (x_ref *sin(ref_yaw) +y_ref *cos(ref_yaw)); 
            
            x_point += ref_x; 
            y_point += ref_y; 
            //push values into vector for simuator 
            next_x_vals.push_back(x_point); 
            next_y_vals.push_back(y_point); 
          }
          
          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
    
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}