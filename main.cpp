
#include <stdio.h>
#include <iostream>
#include <string>
#include <cmath>
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/features2d.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/xfeatures2d.hpp"

using namespace cv;
using namespace cv::xfeatures2d;

#define MIN_HESSIAN 300

int main(int argc, char** argv)
{
  // get the name of the video file
  std::string fName;
  
  if(argc){
    std::cout << argc;
    fName = argv[1];
  } else {
    std::cout << "Enter a video file name: ";
    getline (std::cin, fName);
  }

  // camera
  VideoCapture cap(0);
  
  // video to render over QR code
  VideoCapture vid(fName); 

  Mat img_object, img_scene, descriptors_object, descriptors_scene, img_matches;;
  std::vector<KeyPoint> keypoints_object, keypoints_scene;
  FlannBasedMatcher matcher;

  // load QR code which we will track
  img_object = imread("QR.png", IMREAD_GRAYSCALE );
  resize(img_object, img_object, Size(400,400));

  if( !img_object.data)
  { std::cout<< " --(!) Error reading image " << std::endl; return -1; }

  // detect the keypoints and extract descriptors using SURF
  Ptr<SURF> detector = SURF::create(MIN_HESSIAN);

  // detect the SURF keypoints in the object image
  detector->detectAndCompute( img_object, Mat(), keypoints_object, descriptors_object );

  // H is used to transform the video mat over the scene mat. H_old is used to store the last reasonable transform
  Mat H_latest = Mat::eye(3, 3, CV_32F); 
  Mat scene_mask  = Mat::zeros(img_scene.rows, img_scene.cols, CV_8UC1);

  // main loop
  while(1){
    bool good_detection = false;

    cap >> img_scene;
    if(!img_scene.data)
    { std::cout<< "Error reading camera " << std::endl; return -1; }

    // We can now initialize scene_mask with proper dimentions
    scene_mask = Mat::zeros(img_scene.rows, img_scene.cols, CV_8UC1);

    detector->detectAndCompute( img_scene, Mat(), keypoints_scene, descriptors_scene );

    // match descriptor vectors using FLANN matcher
    std::vector< DMatch > matches;
    matcher.match( descriptors_object, descriptors_scene, matches );
    double max_dist = 0; double min_dist = 100;

    //  Quick calculation of max and min distances between keypoints
    for( int i = 0; i < descriptors_object.rows; i++ )
    { double dist = matches[i].distance;
      if( dist < min_dist ) min_dist = dist;
      if( dist > max_dist ) max_dist = dist;
    }

    // draw only "good" matches (i.e. whose distance is less than 3*min_dist )
    std::vector< DMatch > good_matches;
    for( int i = 0; i < descriptors_object.rows; i++ )
    { if( matches[i].distance < 3*min_dist )
       { good_matches.push_back( matches[i]); }
    }

    drawMatches( img_object, keypoints_object, img_scene, keypoints_scene,
                 good_matches, img_matches, Scalar::all(-1), Scalar::all(-1),
                 std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS );

    // check if there are any matches, if so then continue
    if(img_matches.data) 
    {
      // localize the object
      std::vector<Point2f> obj;
      std::vector<Point2f> scene;
      for( size_t i = 0; i < good_matches.size(); i++ )
      {
        // Get the keypoints from the good matches
        obj.push_back( keypoints_object[ good_matches[i].queryIdx ].pt );
        scene.push_back( keypoints_scene[ good_matches[i].trainIdx ].pt );
      }

      // this returns a 3x3 transformation matrix. If none could be found continue
      // to the next iteration.
      Mat H = findHomography( obj, scene, RANSAC );
      if (!H.data){
        continue;
      }

      // get the corners from the image_1 ( the object to be "detected" )
      std::vector<Point2f> obj_corners(4);
      obj_corners[0] = Point(0, 0);
      obj_corners[1] = Point( img_object.cols, 0 );
      obj_corners[2] = Point( img_object.cols, img_object.rows );
      obj_corners[3] = Point( 0, img_object.rows );
      std::vector<Point2f> scene_corners(4);
      perspectiveTransform( obj_corners, scene_corners, H);

      // check the determinent of the transormation matrix to make sure it's not crazy
      float hDet = abs(determinant(H));
      if (hDet < 200 && hDet > 0.05){ // Good detection, reasonable transform
        H_latest = H;
        good_detection = true;
      }

      // draw lines between the corners (the mapped object in the scene - image_2 )

      std::vector<Point2f> match_corners(4);
      match_corners[0] = scene_corners[0] + Point2f( img_object.cols, 0);
      match_corners[1] = scene_corners[1] + Point2f( img_object.cols, 0);
      match_corners[2] = scene_corners[1] + Point2f( img_object.cols, 0);
      match_corners[3] = scene_corners[2] + Point2f( img_object.cols, 0);

      // line( img_matches, scene_corners[0] + Point2f( img_object.cols, 0), scene_corners[1] + Point2f( img_object.cols, 0), Scalar(0, 255, 0), 4 );
      line( img_matches, scene_corners[1] + Point2f( img_object.cols, 0), scene_corners[2] + Point2f( img_object.cols, 0), Scalar( 0, 255, 0), 4 );
      line( img_matches, scene_corners[2] + Point2f( img_object.cols, 0), scene_corners[3] + Point2f( img_object.cols, 0), Scalar( 0, 255, 0), 4 );
      line( img_matches, scene_corners[3] + Point2f( img_object.cols, 0), scene_corners[0] + Point2f( img_object.cols, 0), Scalar( 0, 255, 0), 4 );

      Mat img_video;
      vid >> img_video;


      // create the mask for the video in the scene view
      std::vector<Point2f> vid_corners(4);
      vid_corners[0] = Point( 0, 0 );
      vid_corners[1] = Point( img_video.cols, 0 );
      vid_corners[2] = Point( img_video.cols, img_video.rows );
      vid_corners[3] = Point( 0, img_video.rows );

      cv::Point nonfloat_corners[4];
      for(int i=0; i<4; i++){
       nonfloat_corners[i] = vid_corners[i];
      }
      fillConvexPoly(scene_mask, nonfloat_corners, 4, cv::Scalar(255));
      warpPerspective( scene_mask, scene_mask, H_latest, Size(img_scene.cols,img_scene.rows));

      // apply transformation to video image and copy with mask
      warpPerspective( img_video, img_video, H_latest, Size(img_scene.cols,img_scene.rows));

      if(   good_detection ){
        img_video.copyTo(img_scene, scene_mask);
      }
      
      // render scene
      imshow( "Test", img_scene);

      // if ESC is pressed, break loop     
      if ( (char)27 == (char) waitKey(1) ) break;
 
    }
  }
  return 0;
}
