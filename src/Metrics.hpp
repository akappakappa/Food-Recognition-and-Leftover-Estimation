#pragma once

#include <vector>

#include <opencv2/opencv.hpp>

class Metrics
{
public:
	Metrics(std::vector<std::vector<std::tuple<cv::Mat,std::vector<std::pair<int, cv::Rect>>,cv::Mat,std::vector<std::pair<int, cv::Rect>>>>>& m);

private:
	const std::vector<std::vector<std::tuple<cv::Mat, std::vector<std::pair<int, cv::Rect>>, cv::Mat, std::vector<std::pair<int, cv::Rect>>>>> metrics;
	std::vector<double> false_positives;
	std::vector<double> false_negatives;
	std::vector<double> true_positives;
	std::vector<std::vector<double>> precision;
	std::vector<std::vector<double>> recall;
	std::vector<double> average_precision;
};