#include "processing.hpp"

#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>

void plateBounds(const cv::Mat& src, cv::Mat& dst) {
  cv::Mat img = src.clone();

  // Threshold
  cv::threshold(img, img, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

  // Structuring
  cv::Mat rect = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 3));
  cv::erode(img, img, rect);

  // Find what's left
  std::vector<cv::Point> points;
  for (auto it = img.begin<uchar>(); it != img.end<uchar>(); it++) {
    if (*it) {
      points.push_back(it.pos());
    }
  }

  // Draw a rectangle around the remaining points
  cv::RotatedRect box = cv::minAreaRect(points);
  cv::Point2f vertices[4];
  box.points(vertices);
  for (int i = 0; i < 4; i++) {
    cv::line(img, vertices[i], vertices[(i+1)%4], cv::Scalar(255, 0, 0), CV_AA);
  }

  dst = img;
}

uchar intensity(const cv::Mat& img, cv::Point loc) {
  if (loc.x < 0 || loc.x >= img.cols || loc.y < 0 || loc.y >= img.rows) {
    return 0;
  }
  cv::Vec3b pix = img.at<cv::Vec3b>(loc);
  return 0.3 * pix[2] + 0.59 * pix[1] + 0.11 * pix[0];
}

bool hasTextRatio(const std::vector<cv::Point>& contour, cv::Size imgSize) {
  cv::Rect box = cv::boundingRect(contour);
  double ratio = static_cast<double>(box.width) / box.height;

  if (ratio < 0.1 || ratio > 10) {
    return false;
  }
  double boxArea = box.width * box.height;
  double imgArea = imgSize.width * imgSize.height;
  return boxArea >= 15 && boxArea <= imgArea / 5;
}

bool isConnected(const std::vector<cv::Point>& contour) {
  cv::Point first = contour[0], last = contour[contour.size() - 1];
  return std::abs(first.x - last.x) <= 1 && std::abs(first.y - last.y) <= 1;
}

bool keep(const std::vector<cv::Point>& contour, cv::Size imgSize) {
  return hasTextRatio(contour, imgSize) && isConnected(contour);
}

int countChildren(const std::vector<std::vector<cv::Point> >& contours,
                  const std::vector<cv::Vec4i>& hier,
                  int index,
                  cv::Size imgSize) {
  if (hier[index][2] < 0) {
    // no children
    return 0;
  }

  // Do I have a child?
  int child = hier[index][2];
  int count = 0;
  if (keep(contours[child], imgSize)) {
    count = 1;
  }

  // Count my children's children
  count += countChildren(contours, hier, child, imgSize);

  // Count siblings
  // Make sure it's the children's siblings
  for (int next = hier[child][0]; next > 0; next = hier[next][0]) {
    if (keep(contours[next], imgSize)) {
      count++;
    }
    count += countChildren(contours, hier, next, imgSize);
  }
  for (int prev = hier[child][0]; prev > 0; prev = hier[prev][0]) {
    if (keep(contours[prev], imgSize)) {
      count++;
    }
    count += countChildren(contours, hier, prev, imgSize);
  }

  return count;
}

void textBinary(const cv::Mat& src, cv::Mat& dst) {
  // Get edge image
  cv::Mat img, edges;

  cv::copyMakeBorder(src, img, 50, 50, 50, 50, cv::BORDER_CONSTANT);

  cv::Mat chans[3];
  cv::split(img, chans);
  for (int i = 0; i < 3; i++) {
    cv::Canny(chans[i], chans[i], 200, 250);
  }
  edges = chans[0] | chans[1] | chans[2];

  // Find the contours and filter based on its size and connectedness
  std::vector<std::vector<cv::Point> > contours;
  std::vector<cv::Vec4i> hierarchy;
  cv::findContours(edges, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_NONE);

  std::vector<std::pair<std::vector<cv::Point>, cv::Rect> > keptRegions;
  for (int i = 0; i < contours.size(); i++) {
    cv::Rect bound = cv::boundingRect(contours[i]);

    // find the parent contour, if there exists one
    int parent;
    for (parent = hierarchy[i][3];
         parent > 0 && keep(contours[parent], edges.size());
         parent = hierarchy[parent][3]);

    int numChildren = countChildren(contours, hierarchy, i, edges.size());
    if (keep(contours[i], edges.size()) &&
        !(parent > 0 && numChildren <= 2) &&
        numChildren <= 2) {
      keptRegions.push_back(std::make_pair(contours[i], bound));
      cv::rectangle(edges, bound, cv::Scalar(255, 0, 0));
      std::cout << "Region : parent = " << parent << "; numChildren = " << numChildren << "\n";
    }
    else {
      std::cout << "reject\n";
    }
  }

  cv::Mat filter(edges.size(), CV_8U, cv::Scalar(255));

  for (auto&& p: keptRegions) {
    auto contour = p.first;
    auto box = p.second;

    // Foreground thresholding
    double fgThresh = 0;
    for (auto&& point: contour) {
      fgThresh += intensity(src, point);
    }
    fgThresh /= contour.size();

    // Background thresholding
    double bgThresh = 0;
    std::vector<uchar> bgs {
      // Top left
      intensity(src, cv::Point(box.x - 1, box.y - 1)),
        intensity(src, cv::Point(box.x - 1, box.y)),
        intensity(src, cv::Point(box.x, box.y - 1)),
        // Top right
        intensity(src, cv::Point(box.x + box.width + 1, box.y - 1)),
        intensity(src, cv::Point(box.x + box.width + 1, box.y)),
        intensity(src, cv::Point(box.x + box.width, box.y - 1)),
        // Bottom left
        intensity(src, cv::Point(box.x - 1, box.y + box.height + 1)),
        intensity(src, cv::Point(box.x - 1, box.y + box.height)),
        intensity(src, cv::Point(box.x, box.y + box.height + 1)),
        // Bottom right
        intensity(src, cv::Point(box.x + box.width + 1, box.y + box.height + 1)),
        intensity(src, cv::Point(box.x + box.width, box.y + box.height + 1)),
        intensity(src, cv::Point(box.x + box.width + 1, box.y + box.height)),
        };
    // Find median for bg thresh
    int len = bgs.size();
    std::nth_element(bgs.begin(), bgs.begin() + len / 2, bgs.end());
    bgThresh = bgs[len/2];
    if (len % 2 == 0) {
      std::nth_element(bgs.begin(), bgs.begin() + len / 2 + 1, bgs.end());
      bgThresh += bgs[len/2+1];
      bgThresh /= 2;
    }

    // Adjust colors for thresholds
    uchar fg, bg;
    if (fgThresh >= bgThresh) {
      fg = 255;
      bg = 0;
    }
    else {
      fg = 0;
      bg = 255;
    }

    // fill in the box
    for (int x = box.x; x < box.x + box.width; x++) {
      for (int y = box.y; y < box.y + box.height; y++) {
        if (x >= 0 && x < img.cols && y >= 0 && img.rows) {
          cv::Point p(x, y);
          if (intensity(img, p) > fgThresh) {
            filter.at<uchar>(p) = bg;
          }
          else {
            filter.at<uchar>(p) = fg;
          }
        }
      }
    }
  }

  dst = filter;
}
