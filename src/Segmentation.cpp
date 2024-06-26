# include "Segmentation.hpp"

#define DEBUG false

Segmentation::Segmentation(cv::Mat& p, std::vector<int> l)
	: plate(p), labels(l)
{
	segments = cv::Mat::zeros(plate.size(), CV_8UC1);

	// Correction
	cv::Mat corrected;
	correction(plate, corrected);
	if (DEBUG) cv::imshow("corrected", corrected);

	// Segmentation
	for (const auto label : labels)
	{
		if (label == 12) continue;

		cv::Mat ranged, mask;
		cv::inRange(corrected, c_ranges[label].first, c_ranges[label].second, ranged);
		process(ranged, mask);

		// If label seafood salad and beans are both present
		if (label == 9 and std::find(labels.begin(), labels.end(), 10) != labels.end())
		{
			cv::Mat tmp, beans;
			cv::inRange(corrected, c_ranges[10].first, c_ranges[10].second, tmp);
			process(tmp, beans);

			// Remove beans from mask
			cv::bitwise_not(beans, beans);
			cv::bitwise_and(mask, beans, mask);
			
			// Morphological opening
			cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
			cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

			// Keep only largest connected component
			std::vector<std::vector<cv::Point>> contours;
			cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
			std::vector<double> areas(contours.size());
			for (size_t i = 0; i < contours.size(); i++)
				areas[i] = cv::contourArea(contours[i]);
			auto max = std::max_element(areas.begin(), areas.end());
			cv::Mat1b mask_tmp = cv::Mat::zeros(mask.size(), CV_8UC1);
			cv::drawContours(mask_tmp, contours, max - areas.begin(), cv::Scalar(255), cv::FILLED);
			mask = mask_tmp;
		}

		cv::threshold(mask, mask, 0, label, cv::THRESH_BINARY);
		mask = mask - segments * 255;
		segments = segments | mask;
		
		// If all black, skip
		if (cv::countNonZero(mask) != 0) {
			// Find bounding box of mask
			std::vector<std::vector<cv::Point>> contours;
			cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
			std::vector<cv::Rect> box(contours.size());
			for (size_t i = 0; i < contours.size(); i++)
				box[i] = cv::boundingRect(contours[i]);
			auto min = std::min_element(box.begin(), box.end(), [](const cv::Rect& a, const cv::Rect& b) {return a.area() < b.area(); });
			boxes.push_back(std::make_pair(label, *min));

			// Show bounding box
			if (DEBUG)
			{
				cv::Mat tmp = plate.clone();
				cv::rectangle(tmp, *min, cv::Scalar(0, 255, 0), 2);
				cv::imshow("bounding box", tmp);
				cv::waitKey(0);
			}
		}
	}

	if (DEBUG)
	{
		cv::imshow("segments", segments * 15);
		cv::waitKey(0);
		cv::destroyAllWindows();
	}
}

void Segmentation::correction(cv::Mat& in, cv::Mat& out)
{
	// Gamma transform
	cv::Mat gamma;
	cv::Mat lookUpTable(1, 256, CV_8U);
	uchar* p = lookUpTable.ptr();
	double gamma_ = 0.5;
	for (int i = 0; i < 256; ++i)
		p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma_) * 255.0);
	cv::LUT(in, lookUpTable, gamma);

	// Image to hsv
	cv::Mat hsv;
	cv::cvtColor(gamma, hsv, cv::COLOR_BGR2HSV);
	std::vector<cv::Mat> hsv_channels;
	cv::split(hsv, hsv_channels);
	cv::equalizeHist(hsv_channels[1], hsv_channels[1]);
	cv::merge(hsv_channels, out);
	cv::cvtColor(out, out, cv::COLOR_HSV2BGR);

	return;
}

void Segmentation::process(cv::Mat& in, cv::Mat& out)
{
	auto filterAreas = [](const cv::Mat& input, cv::Mat& output, const unsigned int threshold) -> void
	{
		std::vector<std::vector<cv::Point>> c;

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
	// Median
	cv::medianBlur(in, in, 5);

	// Closing
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(50, 50));   //changed from 40x40
	cv::morphologyEx(in, in, cv::MORPH_CLOSE, kernel);

	// Dilation
	out = cv::Mat::zeros(in.size(), CV_8UC1);
	filterAreas(in, out, 8000);
	cv::dilate(out, out, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(25, 25)));   //changed from 15x15

	// Closing
	kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(40, 40));   //changed from 15x15
	cv::morphologyEx(out, out, cv::MORPH_CLOSE, kernel);

	// Filling holes
	fillHoles(out);

	return;
}