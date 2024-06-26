// Alessio Cocco 2087635, Andrea Valentinuzzi 2090451, Giovanni Brejc 2096046
// Computer Vision final project 2022/2023 University of Padua
// Food Recognition and Leftover Estimation
//
// 1. Detect plates            --> OpenAI CLIP to find k classes per plate --> k+1 means segmentation
// 2. Detect salad (if exists) --> grabCut segmentation
// 3. Detect bread (if exists) --> grabCut segmentation

#include "BoundingBoxes.hpp"
#include "Segmentation.hpp"
#include "Metrics.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <cmath>
#include <format>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>

#include <Python.h>

#define DEBUG false   // debug mode to check code logic
#define SKIP false    // avoid CLIP processing to save time while developing

using namespace std;

// IMPORTANT NOTICE:
// While reading the code make sure to collapse lambda functions.
// They were intended for such purpose and for you to hide away things that are not important to the main logic.

int main()
{	
	// Variables
	const string           DATASET_PATH      =   "./Food_leftover_dataset/";						        // 
	const int              NUMBER_OF_TRAYS   =   8;														    //     ____        __  __        
	const vector<string>   IMAGE_NAMES       =   { "food_image", "leftover1", "leftover2", "leftover3" };   //    / __ \____ _/ /_/ /_  _____
	const string           PLATES_PATH       =   "./plates/";											    //   / /_/ / __ `/ __/ __ \/ ___/
	const string           BREAD_PATH        =   "./bread/";											    //  / ____/ /_/ / /_/ / / (__  ) 
	const string           LABELS_PATH       =   "./labels/";											    // /_/    \__,_/\__/_/ /_/____/  
	const string           BREAD_OUT_PATH    =   "./bread_output/";										    //                               
	const string           OUTPUT_PATH       =   "./output/";											    // 
	vector<vector<tuple<                   // for each tray, for each image, tuple that contains:
			cv::Mat,                       // found mask
			vector<pair<int, cv::Rect>>,   // found boxes = vector of <class, bounding box>
			cv::Mat,                       // ground truth mask
			vector<pair<int, cv::Rect>>    // ground truth boxes = vector of <class, bounding box>
		>>> metrics;                       // name of this abomination is metrics
	auto cutout = [](const cv::Mat& image, const cv::Vec3f& circle) -> cv::Mat
	{
		const int x = cvRound(circle[0] - circle[2]) > 0 ? cvRound(circle[0] - circle[2]) : 0;
		const int y = cvRound(circle[1] - circle[2]) > 0 ? cvRound(circle[1] - circle[2]) : 0;
		const int w = x + cvRound(2 * circle[2]) < image.cols ? cvRound(2 * circle[2]) : image.cols - x;
		const int h = y + cvRound(2 * circle[2]) < image.rows ? cvRound(2 * circle[2]) : image.rows - y;
		
		//return image inside circle
		cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
		cv::circle(mask, cv::Point(cvRound(circle[0]), cvRound(circle[1])), cvRound(circle[2]), cv::Scalar(255), -1);
		cv::Mat res;
		image.copyTo(res, mask);
		return res(cv::Rect(x,y,w,h));

	};
	auto display = [](const cv::Mat& image) -> void
	{
		cv::namedWindow("Display window", cv::WINDOW_AUTOSIZE);
		cv::imshow("Display window", image);
		cv::waitKey(0);
	};
	auto process = [](cv::Mat& mask) -> cv::Mat
	{
		// Variables
		cv::Mat output = cv::Mat::zeros(mask.size(), CV_8UC1);
		const unsigned int BLUR_STRENGTH = 5;
		const unsigned int INITIAL_KERNEL_SIZE = 40;
		const unsigned int AREA_THRESHOLD = 8000;
		const unsigned int KERNEL_SIZE = 15;
		auto filterAreas = [](const cv::Mat& input, cv::Mat& output, const unsigned int threshold) -> void
		{
			vector<vector<cv::Point>> c;

			cv::findContours(input.clone(), c, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
			for (int i = 0; i < c.size(); i++)
				if (cv::contourArea(c[i]) > threshold)
					cv::drawContours(output, c, i, 255, -1);
		};
		auto fillHoles = [](cv::Mat& input) -> void
		{
			cv::Mat ff = input.clone();
			cv::floodFill(ff, cv::Point(0, 0), cv::Scalar(255));
			cv::Mat inversed_ff;
			cv::bitwise_not(ff, inversed_ff);
			input = (input | inversed_ff);
		};

		// Morphological operations
		cv::medianBlur(mask, mask, BLUR_STRENGTH);
		cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(INITIAL_KERNEL_SIZE, INITIAL_KERNEL_SIZE)));
		filterAreas(mask, output, AREA_THRESHOLD);
		cv::dilate(output, output, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(KERNEL_SIZE, KERNEL_SIZE)));
		cv::morphologyEx(output, output, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(KERNEL_SIZE, KERNEL_SIZE)));
		fillHoles(output);

		return output;
	};

	// Python initialization for CLIP
	Py_Initialize();											   //
	PyEval_InitThreads();										   //
	PyRun_SimpleString("import sys");							   //     ____        __  __
	PyRun_SimpleString("sys.path.append('./Python/')");	           //    / __ \__  __/ /_/ /_  ____  ____
	PyRun_SimpleString("sys.argv = ['CLIP_interface.py']");		   //   / /_/ / / / / __/ __ \/ __ \/ __ \*
	PyObject* pName = PyUnicode_FromString("CLIP_interface");	   //  / ____/ /_/ / /_/ / / / /_/ / / / /
	PyObject* pModule = PyImport_ImportModule("CLIP_interface");   // /_/    \__, /\__/_/ /_/\____/_/ /_/
	PyObject* pFunc = PyObject_GetAttrString(pModule, "plates");   //       /____/
	PyObject* pArgs = PyTuple_New(1);							   //

	// START OF THE MAIN LOOP
	if (!filesystem::exists(PLATES_PATH)) filesystem::create_directory(PLATES_PATH); 
	if (!filesystem::exists(OUTPUT_PATH)) filesystem::create_directory(OUTPUT_PATH);
	if (!filesystem::exists(BREAD_PATH)) filesystem::create_directory(BREAD_PATH);

	for (int i = 1; i <= NUMBER_OF_TRAYS; i++)
	{	// For each tray [i]
		metrics.push_back(vector<tuple<cv::Mat, vector<pair<int, cv::Rect>>, cv::Mat, vector<pair<int, cv::Rect>>>>());   // Create a vector of metrics for each tray [i]
		if (!filesystem::exists(PLATES_PATH + "tray" + to_string(i) + "/")) filesystem::create_directory(PLATES_PATH + "tray" + to_string(i) + "/");
		if (!filesystem::exists(OUTPUT_PATH + "tray" + to_string(i) + "/")) filesystem::create_directory(OUTPUT_PATH + "tray" + to_string(i) + "/");
		if (!filesystem::exists(BREAD_PATH + "tray" + to_string(i) + "/")) filesystem::create_directory(BREAD_PATH + "tray" + to_string(i) + "/");

		queue<BoundingBoxes> bb;   // Queue of BoundingBoxes objects: create them now and pop them later when processing

		//    __	 
		//  /    \	 First informative STOP sign.
		// | STOP |	 The following for loop stands at the basis of the whole program:
		//  \ __ /	 it reads the images and creates the BoundingBoxes objects.
		//    ||	 
		//    ||	 NOTE: the BoundingBoxes objects do not really contain proper bounding boxes (look at the class definition).
		//    ||	     - plates: vector that contains circles computed with HoughCircles
		//    ||	     - salad: pair <bool, circle> that tells if there is a salad or not and its position
		//    ||	     - bread: pair <bool, image> that tells if there is a bread or not and the segmented mask
		//  ~~~~~~~	 

		// Read images and create BoundingBoxes objects
		for (const auto& imgname : IMAGE_NAMES)
		{	// For each image 'imgname' in tray [i]
			if (!filesystem::exists(PLATES_PATH + "tray" + to_string(i) + "/" + imgname + "/")) filesystem::create_directory(PLATES_PATH + "tray" + to_string(i) + "/" + imgname + "/");
			if (!filesystem::exists(BREAD_PATH + "tray" + to_string(i) + "/" + imgname + "/")) filesystem::create_directory(BREAD_PATH + "tray" + to_string(i) + "/" + imgname + "/");
			
			cv::Mat image = cv::imread(DATASET_PATH + "tray" + to_string(i) + "/" + imgname + ".jpg");   // Read the image
			bb.push(BoundingBoxes(image));                                                               // Push the BoundingBoxes object into the queue
			
			// Save plates cutouts
			vector<cv::Vec3f> plates = bb.back().getPlates();
			for (int j = 0; j < plates.size(); j++)
				cv::imwrite(PLATES_PATH + "tray" + to_string(i) + "/" + imgname + "/plate" + to_string(j) + ".jpg", cutout(image, plates[j]));
		}

		// Just ugly testing (slightly faster with pre-computed stuff), we ALWAYS want to enter here!
		if (!SKIP)
		{	// Plates segmentation using CLIP
			if (DEBUG) cout << "Running Python script..." << endl;
			PyObject* pValue = PyLong_FromLong(i);
			PyTuple_SetItem(pArgs, 0, pValue);
			PyObject_CallObject(pFunc, pArgs);
			if (DEBUG) cout << "Python script finished" << endl;
		}

		// Compute final masks and bounding boxes for each image
		for (const auto& imgname : IMAGE_NAMES)
		{	// For each image 'imgname' in tray [i]
			cv::Mat image = cv::imread(DATASET_PATH + "tray" + to_string(i) + "/" + imgname + ".jpg");   // Read the image
			vector<cv::Vec3f> plates = bb.front().getPlates();                                           // Get the plates from the queue
			pair<bool, cv::Vec3f> salad = bb.front().getSalad();                                         // Get the salad from the queue
			pair<bool, cv::Mat> bread = bb.front().getBread();                                           // Get the bread from the queue
			bb.pop();                                                                                    // Pop the BoundingBoxes object from the queue
			
			vector<string> files;                                                              // Vector of strings containing the paths of the plates in the image
			cv::glob(PLATES_PATH + "tray" + to_string(i) + "/" + imgname + "/*.jpg", files);   // Get the paths of the plates in the image

			cv::Mat tray_mask = cv::Mat::zeros(image.size(), CV_8UC1);   // Create the tray mask
			vector<string> boxes;                                        // Vector of strings containing the bounding boxes of the plates in the image
			vector<pair<int, cv::Rect>> tray_boxes;                      // Final bounding boxes of the plates in the image

			//     ____  __      __           
			//    / __ \/ /___ _/ /____  _____
			//   / /_/ / / __ `/ __/ _ \/ ___/
			//  / ____/ / /_/ / /_/  __(__  ) 
			// /_/   /_/\__,_/\__/\___/____/  
			// 
			// PLATES: Process each plate in the image
			for (int j = 0; j < files.size(); j++)
			{	// For each plate [j] in the image 'imgname' of tray [i]
				ifstream infile(LABELS_PATH + files[j].substr(PLATES_PATH.length(), files[j].length() - 1) + ".txt");

				vector<int> labels;                                                  // Vector of integers containing the labels of the segments in the plate [j]
				string category;	                                                 // String containing the label of each segment in the plate [j]
				while (getline(infile, category)) labels.push_back(stoi(category));  // Read the labels from the file previously computed by CLIP
				infile.close();                                                      // Close the file

				// Segmentate the plate [j] and get the bounding boxes of the segments
				cv::Mat plate_image = cv::imread(files[j]);         // Read the plate [j]
				Segmentation seg(plate_image, labels);		        // Create a Segmentation object
				cv::Mat mask = seg.getSegments();			        // Get the mask of the segments
				vector<pair<int, cv::Rect>> box = seg.getBoxes();   // Get the bounding boxes of the segments
				for (int k = 0; k < box.size(); k++)
				{   // For each bounding box [k] in the plate [j]
					int label = box[k].first;                                // Get the label of the segment
					int x = box[k].second.x + plates[j][0] - plates[j][2];   // Get the x coordinate of the bounding box wrt the true image
					int y = box[k].second.y + plates[j][1] - plates[j][2];   // Get the y coordinate of the bounding box wrt the true image
					int w = box[k].second.width;                             // Get the width of the bounding box
					int h = box[k].second.height;                            // Get the height of the bounding box

					boxes.push_back("ID: " + to_string(label) + "; [" + to_string(x) + ", " + to_string(y) + ", " + to_string(w) + ", " + to_string(h) + "]");
					tray_boxes.push_back(make_pair(label, cv::Rect(x, y, w, h)));
				}

				// Add the mask of the plate [j] to the tray mask
				for (int k = 0; k < mask.rows; k++)   // For each row [k] in the mask
					for (int l = 0; l < mask.cols; l++)   // For each column [l] in the mask
						if (pow(k - plates[j][2], 2) + pow(l - plates[j][2], 2) <= pow(plates[j][2], 2))   // Replace in the tray mask only the pixels inside the plate [j], not the whole rectangle
							if (k + plates[j][1] - plates[j][2] >= 0 && k + plates[j][1] - plates[j][2] < tray_mask.rows && l + plates[j][0] - plates[j][2] >= 0 && l + plates[j][0] - plates[j][2] < tray_mask.cols)
								tray_mask.at<uchar>(k + plates[j][1] - plates[j][2], l + plates[j][0] - plates[j][2]) = mask.at<uchar>(k,l);
			}

			if (DEBUG) { cv::imshow("tray_mask", tray_mask * 15); cv::waitKey(0); }

			//    _____       __          __
			//   / ___/____ _/ /___ _____/ /
			//   \__ \/ __ `/ / __ `/ __  / 
			//  ___/ / /_/ / / /_/ / /_/ /  
			// /____/\__,_/_/\__,_/\__,_/   
			//                              
			// SALAD: Process the salad in the image
			if (salad.first)
			{	// If the salad is present in the image
				const int LABEL = 12;                                // Label of the salad
				const unsigned int SATURATION_THRESHOLD = 206;       // Saturation threshold
				cv::Mat salad_image = cutout(image, salad.second);   // Cut out the salad from the image

				// Gamma correction
				cv::Mat gamma;
				cv::Mat lookUpTable(1, 256, CV_8U);
				uchar* p = lookUpTable.ptr();
				double gamma_ = 0.5;
				for (int i = 0; i < 256; ++i)
					p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma_) * 255.0);
				cv::LUT(salad_image, lookUpTable, gamma);

				// HSV equalization
				cv::Mat hsv;
				cv::cvtColor(gamma, hsv, cv::COLOR_BGR2HSV);
				vector<cv::Mat> hsv_channels;
				cv::split(hsv, hsv_channels);
				cv::equalizeHist(hsv_channels[1], hsv_channels[1]);

				// Thresholding
				cv::Mat mask;
				cv::threshold(hsv_channels[1], mask, SATURATION_THRESHOLD, LABEL, cv::THRESH_BINARY);
				
				// Morphological operations
				mask = process(mask);
				cv::threshold(mask, mask, 0, LABEL, cv::THRESH_BINARY);   // Thresholding again to the correct label

				// Find the bounding box of the salad
				vector<vector<cv::Point>> contours;                                             // Vector of vectors of points containing the contours of the salad
				cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);   // Find the contours of the salad
				vector<cv::Rect> box(contours.size());									        // Vector of rectangles containing the bounding boxes of the salad
				
				for (int i = 0; i < contours.size(); i++)
					box[i] = cv::boundingRect(contours[i]);  // Get the bounding boxes of the salad
				auto min = min_element(box.begin(), box.end(), [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });   // Get the smallest bounding box

				int x = min->x + salad.second[0] - salad.second[2];   // Get the x coordinate of the bounding box wrt the true image
				int y = min->y + salad.second[1] - salad.second[2];   // Get the y coordinate of the bounding box wrt the true image
				int w = min->width;                                   // Get the width of the bounding box
				int h = min->height;                                  // Get the height of the bounding box

				boxes.push_back("ID: " + to_string(LABEL) + "; [" + to_string(x) + ", " + to_string(y) + ", " + to_string(w) + ", " + to_string(h) + "]");
				tray_boxes.push_back(make_pair(LABEL, cv::Rect(x, y, w, h)));

				// Add the mask of the salad to the tray mask
				for (int k = 0; k < mask.rows; k++)   // For each row [k] in the mask
					for (int l = 0; l < mask.cols; l++)   // For each column [l] in the mask
						if (pow(k - salad.second[2],2) + pow(l - salad.second[2],2) <= pow(salad.second[2],2))   // Replace in the tray mask only the pixels inside the salad, not the whole rectangle
							if (k + salad.second[1] - salad.second[2] >= 0 && k + salad.second[1] - salad.second[2] < tray_mask.rows && l + salad.second[0] - salad.second[2] >= 0 && l + salad.second[0] - salad.second[2] < tray_mask.cols)
								tray_mask.at<uchar>(k + salad.second[1] - salad.second[2], l + salad.second[0] - salad.second[2]) = mask.at<uchar>(k, l);

				if (DEBUG) { cv::imshow("w/salad", tray_mask * 15); cv::waitKey(0); }
			}

			//     ____                      __
			//    / __ )________  ____ _____/ /
			//   / __  / ___/ _ \/ __ `/ __  / 
			//  / /_/ / /  /  __/ /_/ / /_/ /  
			// /_____/_/   \___/\__,_/\__,_/   
			//                                 
			// BREAD: Process the bread in the image
			if (bread.first)
			{	// If the bread is present in the image
				const int LABEL = 13;   // Label of the bread, as defined in the assignment

				cv::Mat bread_mask;
				cv::threshold(bread.second, bread_mask, 0, LABEL, cv::THRESH_BINARY);   // Thresholding to the correct label
				
				// Bounding box
				vector<vector<cv::Point>> contours;
				cv::findContours(bread_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
				cv::Rect box = cv::boundingRect(contours[0]);

				// Coordinates
				int x = box.x;
				int y = box.y;
				int w = box.width;
				int h = box.height;

				// Add the bounding box to the list of bounding boxes
				boxes.push_back("ID: " + to_string(LABEL) + "; [" + to_string(x) + ", " + to_string(y) + ", " + to_string(w) + ", " + to_string(h) + "]");
				tray_boxes.push_back(make_pair(LABEL, cv::Rect(x, y, w, h)));
				tray_mask = tray_mask + bread_mask;

				if (DEBUG) cv::imshow("w/bread", tray_mask * 15); cv::waitKey(0);
			}

			//    __	 
			//  /    \	 Second informative STOP sign.
			// | STOP |	 You have now finished processing the image and getting all the information you need.
			//  \ __ /	 No more plates, salad or bread to process.
			//    ||	 
			//    ||	 The code below writes stuff to files, then pushes info that will be used later to the metrics vector.
			//    ||	 
			//    ||	 easy stuff
			//    ||	 don't worry about it
			//  ~~~~~~~	 
			// Write bounding boxes to file
			if (!filesystem::exists(OUTPUT_PATH + "tray" + to_string(i) + "/bounding_boxes/")) filesystem::create_directory(OUTPUT_PATH + "tray" + to_string(i) + "/bounding_boxes/");
			for (int k = 0; k < boxes.size(); k++)
			{   // For each bounding box [k] in the image 'imgname' in tray [i]
				if (DEBUG) cout << imgname + " " + boxes[k] << endl;

				ofstream file;
				string path = OUTPUT_PATH + "tray" + to_string(i) + "/bounding_boxes/" + imgname + "_bounding_boxes.txt";

				k == 0 ? file = ofstream(path) : file = ofstream(path, ios_base::app);
				if (file.is_open())
					k < boxes.size() - 1 ? file << boxes[k] << endl : file << boxes[k];
			}

			// Write tray mask to file
			if (!filesystem::exists(OUTPUT_PATH + "tray" + to_string(i) + "/masks/")) filesystem::create_directory(OUTPUT_PATH + "tray" + to_string(i) + "/masks/");
			cv::imwrite(OUTPUT_PATH + "tray" + to_string(i) + "/masks/" + imgname + "_mask.png", tray_mask);

			// METRICS: update the 'metrics' vector
			const string BOXES_PATH = DATASET_PATH + "tray" + to_string(i) + "/bounding_boxes/" + imgname + "_bounding_box.txt";
			string MASK_PATH = DATASET_PATH + "tray" + to_string(i) + "/masks/" + imgname;
			if (imgname == "food_image") MASK_PATH += "_mask";
			MASK_PATH += ".png";

			vector<pair<int, cv::Rect>> original_boxes;   // Vector of pairs to store the original boxes from the assignment
			ifstream file(BOXES_PATH);
			if (file.is_open())
			{
				string line;   // Line read from the file
				while (getline(file, line))
				{   // For each line 'line' in the file
					int id_start = line.find(":") + 2;    // Start of the id in the line
					int id_end = line.find(";");          // End of the id in the line
					int box_start = line.find("[") + 1;   // Start of the box in the line
					int box_end = line.find("]");         // End of the box in the line

					string id = line.substr(id_start, id_end - id_start);      // Extract the id
					string box = line.substr(box_start, box_end - box_start);  // Extract the box

					istringstream iss(box);                                                               // Create a string stream from the box
					vector<string> tokens{ istream_iterator<string>{iss}, istream_iterator<string>{} };   // Split the box into tokens
					cv::Rect tmp(stoi(tokens[0]), stoi(tokens[1]), stoi(tokens[2]), stoi(tokens[3]));     // Create a rectangle from the box
					original_boxes.push_back(make_pair(stoi(id), tmp));                                   // Add the pair to the vector
				}
				file.close();
			}
			cv::Mat original_mask = cv::imread(MASK_PATH, cv::IMREAD_GRAYSCALE);                          // Read the mask in GRAYSCALE mode
			metrics.back().push_back(make_tuple(tray_mask, tray_boxes, original_mask, original_boxes));   // Add the metric to the vector
		}
	}

	//       (                 ,&&&.    
	//        )                .,.&&    Welcome traveler, you finally made it to the end!
	//       (  (              \=__/    But your journey is not over yet, there is still one more thing to do.
	//           )             ,'-'.    Before venturing into the Metrics class, take some well deserved rest.
	//     (    (  ,,      _.__|/ /|    
	//      ) /\ -((------((_|___/ |    Alessio Cocco, Andrea Valentinuzzi, Giovanni Brejc
	//    (  // | (`'      ((  `'--|    wish you the best of luck for this final stretch.
	//  _ -.;_/ \\--._      \\ \-._/.   
	// (_;-// | \ \-'.\    <_,\_\`--'|  GLHF!
	// ( `.__ _  ___,')      <_,-'__,'  :)
	//  `'(_ )_)(_)_)'				    
	//								    
	// METRICS: compute the metrics
	Metrics m(metrics);

	// Python finalization
	Py_Finalize();

	cout << "You got here, all is good :)" << endl;

	return 0;
}