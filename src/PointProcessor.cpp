#include "Profiler.hpp"
#include "PointProcessor.h"


template<typename PointT>
PointCloud<PointT> PointProcessor<PointT>::filterCloud(PointCloud<PointT> inputCloud, float filterRes, Eigen::Vector4f minPoint, Eigen::Vector4f maxPoint)
{
    PROFILE_FUNCTION();
    // voxel grid point reduction and region based filtering
    PointCloud<PointT> cloudFiltered(new pcl::PointCloud<PointT>);
    pcl::VoxelGrid<PointT> filter;
    filter.setInputCloud(inputCloud);
    filter.setLeafSize(filterRes, filterRes, filterRes);
    filter.filter(*cloudFiltered);

    PointCloud<PointT> cloudRegion(new pcl::PointCloud<PointT>);
    pcl::CropBox<PointT> region(true);
    region.setMin(minPoint);
    region.setMax(maxPoint);
    region.setInputCloud(cloudFiltered);
    region.filter(*cloudRegion);

    // Roof Points
    std::vector<int> indices;
    pcl::CropBox<PointT> roof(true);
    region.setMin(Eigen::Vector4f(-1.5,-1.7,-1,1));
    region.setMax(Eigen::Vector4f(2.6,1.7,-.4,1));
    region.setInputCloud(cloudRegion);
    region.filter(indices);
    pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
    for(int point: indices)
        inliers->indices.push_back(point);
    
    pcl::ExtractIndices<PointT> extract;
    extract.setInputCloud(cloudRegion);
    extract.setIndices(inliers);
    extract.setNegative(true);
    extract.filter(*cloudRegion);

    return cloudRegion;
}

template<typename PointT>
Eigen::Vector4f PointProcessor<PointT>::fitPlane(std::vector<int>& inliers, PointCloud<PointT> cloud)
{
    // PROFILE_FUNCTION();
    Eigen::Array4f p1 = cloud->points[inliers[0]].getArray4fMap();
    Eigen::Array4f p2 = cloud->points[inliers[1]].getArray4fMap();
    Eigen::Array4f p3 = cloud->points[inliers[2]].getArray4fMap();
    Eigen::Vector4f v1 = p2 - p1;
    Eigen::Vector4f v2 = p3 - p1;
    Eigen::Vector4f model;

    model.head<3>() = v1.head<3>().cross(v2.head<3>());
    model[3] = (- model[0] * p1[0] - model[1] * p1[1] - model[2] * p1[2]);
    return model;
}

template<typename PointT>
std::vector<int> PointProcessor<PointT>::ransac(PointCloud<PointT> cloud, int maxIterations, float distanceTol)
{
    PROFILE_FUNCTION();
    std::vector<int> inliersResult;
    std::random_device rd;
    std::mt19937 gen(rd());

    #pragma omp parallel for
    for(int i=0; i < maxIterations; i++)
    {
        std::vector<int> inliers;
        std::vector<int> samplePoints;
        std::uniform_int_distribution<> dist(1, cloud->points.size());
        while(samplePoints.size() < 3)
        {
            samplePoints.push_back(dist(gen));
        }
        // Compute Plane Model
        auto model = fitPlane(samplePoints, cloud);
        float n = sqrt(pow(model[0],2) + pow(model[1],2) + pow(model[2],2));

        // Measure distance between every point and fitted plane
        for( int idx = 0; idx < cloud->points.size(); idx++)
        {
            float distance = fabs((model[0] * cloud->points[idx].x + model[1] * cloud->points[idx].y + model[2] * cloud->points[idx].z + model[3])/n);
            if(distance <= distanceTol)
            {
                inliers.push_back(idx);
            }
        }
        #pragma omp critical
		if(inliers.size() > inliersResult.size())
		{
			inliersResult = inliers;
		}
    }
    return inliersResult;   
}

template<typename PointT>
PointCloudPair<PointT> PointProcessor<PointT>::seperateClouds(std::vector<int> inliers, PointCloud<PointT> cloud)
{
    PROFILE_FUNCTION();
    PointCloud<PointT> cloudInliers(new pcl::PointCloud<PointT>());
	PointCloud<PointT> cloudOutliers(new pcl::PointCloud<PointT>());

    pcl::PointIndices::Ptr in {new pcl::PointIndices};
    in->indices = inliers;

    pcl::ExtractIndices<PointT> extract;
    extract.setInputCloud(cloud);
    extract.setIndices(in);
    extract.setNegative(false);
    extract.filter(*cloudInliers);
    extract.setNegative(true);
    extract.filter(*cloudOutliers);
    PointCloudPair<PointT> cloudPair(cloudInliers, cloudOutliers);
    return cloudPair;
}

template<typename PointT>
PointCloudPair<PointT> PointProcessor<PointT>::segmentCloud(PointCloud<PointT> inputCloud, int maxIterations, float distanceThreshold)
{
    PROFILE_FUNCTION();
    // pcl::SACSegmentation<PointT> seg;
	// pcl::PointIndices::Ptr inliers {new pcl::PointIndices};
    // pcl::ModelCoefficients::Ptr coefficients {new pcl::ModelCoefficients};
    // seg.setOptimizeCoefficients(true);
    // seg.setModelType(pcl::SACMODEL_PLANE);
    // seg.setMethodType(pcl::SAC_RANSAC);
    // seg.setMaxIterations(maxIterations);
    // seg.setDistanceThreshold(distanceThreshold);
    // seg.setInputCloud(inputCloud);
    // seg.segment(*inliers, *coefficients);
    // if(inliers->indices.size() == 0)
    // {
    //     std::cerr << "Could not estimate a planar model for the given dataset." << std::endl;
    // }

    auto inliers = ransac(inputCloud, maxIterations, distanceThreshold);
    auto segResult = seperateClouds(inliers, inputCloud);
    return segResult;
}

template<typename PointT>
PointCloud<PointT> PointProcessor<PointT>::loadPCD(std::string file)
{
    PROFILE_FUNCTION();
    PointCloud<PointT> cloud(new pcl::PointCloud<PointT>);
    if (pcl::io::loadPCDFile<PointT> (file, *cloud) == -1) //* load the file
    {
        PCL_ERROR ("Couldn't read file \n");
    }
    return cloud;
}

template<typename PointT>
std::vector<boost::filesystem::path> PointProcessor<PointT>::streamPCD(std::string dataPath)
{
    std::vector<boost::filesystem::path> paths(boost::filesystem::directory_iterator{dataPath}, boost::filesystem::directory_iterator{});
    sort(paths.begin(), paths.end());
    return paths;
}