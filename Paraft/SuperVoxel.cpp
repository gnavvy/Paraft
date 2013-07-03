#include "SuperVoxel.h"

SuperVoxel::SuperVoxel(const Metadata &meta) {
    pImage_ = cvLoadImage(meta.tfPath().c_str(), CV_LOAD_IMAGE_COLOR);
    if (!pImage_ || pImage_->nChannels != 3 || pImage_->depth != IPL_DEPTH_8U) {
        cerr << "Error - Unsupport image format." << endl;
        exit(EXIT_FAILURE);
    }

    dim_ = Vector3i(pImage_->width, pImage_->height, 1);
    kNumElements_ = dim_.Product();

    pClusters_      = new int[kNumElements_];
    pClustersTmp_   = new int[kNumElements_];
    pGradients_     = new float[kNumElements_];

    for (int i = 0; i < kNumElements_; i++) {
        pClusters_[i] = -1;
        pClustersTmp_[i] = -1;
        pGradients_[i] = 0.0f;
    }

    pLs = new float[kNumElements_];
    pAs = new float[kNumElements_];
    pBs = new float[kNumElements_];
}

SuperVoxel::SuperVoxel(const Vector3i dim) : dim_(dim) {
    pData_ = nullptr;
    pMask_ = nullptr;
}

SuperVoxel::~SuperVoxel() {
    if (pImage_)        { cvReleaseImage(&pImage_); pImage_ = nullptr; }
    if (pClusters_)     { delete [] pClusters_; pClusters_ = nullptr; }
    if (pLs)            { delete [] pLs; pLs = nullptr; }
    if (pAs)            { delete [] pAs; pAs = nullptr; }
    if (pBs)            { delete [] pBs; pBs = nullptr; }
    if (pGradients_)    { delete [] pGradients_; pGradients_ = nullptr; }
    // ----
    if (pData_)         { delete [] pData_; pData_ = nullptr; }
    if (pMask_)         { delete [] pMask_; pMask_ = nullptr; }
}

void SuperVoxel::ClusterByNumber(const int numClusters, const float compactness) {
    int segLength = static_cast<int>(sqrt(kNumElements_ / numClusters));
    ClusterBySize(segLength, compactness);
}

void SuperVoxel::ClusterBySize(const int segLength, const float compactness) {
    // todo: load data

    calculateGradientsForEachVoxel();

    dispatchInitialSeeds(segLength);

    perturbSeedsToLocalMinGradient();

    clustering(segLength, compactness);
}

// ----

// 1.
void SuperVoxel::calculateGradientsForEachVoxel() {
    for (int z = 1; z < dim_.z - 1; z++) {
        for (int y = 1; y < dim_.y - 1; y++) {
            for (int x = 1; x < dim_.x - 1; x++) {
                int self   = GetVoxelIndex(Vector3i(x, y, z));
                int left   = GetVoxelIndex(Vector3i(x-1,y,z));
                int right  = GetVoxelIndex(Vector3i(x+1,y,z));
                int top    = GetVoxelIndex(Vector3i(x,y+1,z));
                int bottom = GetVoxelIndex(Vector3i(x,y-1,z));
                int front  = GetVoxelIndex(Vector3i(x,y,z-1));
                int back   = GetVoxelIndex(Vector3i(x,y,z+1));
                float dx   = (pData_[right] - pData_[left]) * (pData_[right] - pData_[left]);
                float dy   = (pData_[top] - pData_[bottom]) * (pData_[top] - pData_[bottom]);
                float dz   = (pData_[back] - pData_[front]) * (pData_[back] - pData_[front]);
                pGradients_[self] = dx + dy + dz;
            }
        }
    }
}

// 2.
void SuperVoxel::dispatchInitialSeeds(int segLength) {
    Vector3i numSeg = dim_ / segLength;            // |--1--|--2--|--3--|-- numSeg = 3
    Vector3i remains = dim_ - numSeg * segLength;  //                    -- remains = 2
    float roundFactor = static_cast<float>(segLength) / 2.0f;

    for (int z = 0; z < numSeg.z; z++) {
        float zErr = static_cast<float>(z * remains.z) / numSeg.z;      // avg remain per segment
        for (int y = 0; y < numSeg.y; y++) {
            float yErr = static_cast<float>(y * remains.y) / numSeg.y;
            for (int x = 0; x < numSeg.x; x++) {
                float xErr = static_cast<float>(x * remains.x) / numSeg.x;

                Vector3i segIndex; {
                    segIndex.x = std::min(util::round(x * segLength + roundFactor + xErr), dim_.x - 1);
                    segIndex.y = std::min(util::round(y * segLength + roundFactor + yErr), dim_.y - 1);
                    segIndex.z = std::min(util::round(z * segLength + roundFactor + zErr), dim_.z - 1);
                }
                int index = GetVoxelIndex(Vector3i(x,y,z));

                centroids_.push_back(segIndex);
                centroidsValues_.push_back(pData_[index]);
            }
        }
    }
}

// 3. optional
void SuperVoxel::perturbSeedsToLocalMinGradient() {
    vector<Vector3i> neighbors; {                   // six direct neighbors
        neighbors.push_back(Vector3i(-1, 0, 0));    // left
        neighbors.push_back(Vector3i( 1, 0, 0));    // right
        neighbors.push_back(Vector3i( 0, 1, 0));    // top
        neighbors.push_back(Vector3i( 0,-1, 0));    // bottom
        neighbors.push_back(Vector3i( 0, 0,-1));    // front
        neighbors.push_back(Vector3i( 0, 0, 1));    // back
    }

    for (size_t n = 0; n < centroids_.size(); n++) {
        Vector3i originalCentroid = centroids_[n];
        int index = GetVoxelIndex(originalCentroid);
        int indexNew = index;
        for (size_t i = 0; i < neighbors.size(); i++) {
            Vector3i tempCentroid = originalCentroid + neighbors[i];
            if (tempCentroid.x >= 0 && tempCentroid.x < dim_.x &&
                tempCentroid.y >= 0 && tempCentroid.y < dim_.y &&
                tempCentroid.z >= 0 && tempCentroid.z < dim_.z) {
                int indexTemp = GetVoxelIndex(tempCentroid);
                if (pGradients_[indexTemp] < pGradients_[indexNew]) {
                    indexNew = indexTemp;   // index of least gradient after iteration.
                }
            }
        }
        if (index != indexNew) {  // shifted to neighbor
            centroids_[n] = GetVoxelPosition(n);
            centroidsValues_[n] = centroidsValues_[indexNew];
        }
    }
}

// 4. iteratively do super pixel clustering
void SuperVoxel::clustering(int segLength, float compactness) {
    const int kNumSeeds = centroids_.size();
    const int kLocalWindowsSize = segLength * 2;

    // A set of variables containing the segmentation result information of each iteration.
    std::vector<int> clusterSize(kNumSeeds, 0);                 // number of voxels dispatched to each cluster(seed)
    std::vector<float> sumValues(kNumSeeds, 0.0f);              // for calculating average value
    std::vector<float> minDistances(kNumElements_, DBL_MAX);    // minDist(voxel, nearest cluster center)
    std::vector<Vector3i> sumCentroids(kNumSeeds, Vector3i());  // for calculating cluster center

    float weight = static_cast<float>(segLength) / compactness; // weight intensity : position !
    weight = 1.0f / (weight * weight);

    for (int iter = 0; iter < 10; iter++) {
        for (int n = 0; n < kNumSeeds; n++) {
            // windows range
            Vector3i min, max; {
                min.x = std::max(centroids_[n].x - kLocalWindowsSize, 0);
                min.y = std::max(centroids_[n].y - kLocalWindowsSize, 0);
                min.z = std::max(centroids_[n].z - kLocalWindowsSize, 0);
                max.x = std::min(centroids_[n].x + kLocalWindowsSize, dim_.x);
                max.y = std::min(centroids_[n].y + kLocalWindowsSize, dim_.y);
                max.z = std::min(centroids_[n].z + kLocalWindowsSize, dim_.z);
            }

            for (int z = min.z; z < max.z; z++) {
                for (int y = min.y; y < max.y; y++) {
                    for (int x = min.x; x < max.x; x++) {
                        Vector3i currentPos(x,y,z);
                        Vector3i centroidPos = centroids_[n];

                        int currentIndex = GetVoxelIndex(currentPos);

                        float currentValue = pData_[currentIndex];
                        float centroidValue = centroidsValues_[n];

                        float distValue = currentValue - centroidValue;
                        float distSpace = (currentPos - centroidPos).MagnituteSquared();
                        float distance = distValue + distSpace * weight;

                        if (distance < minDistances[currentIndex]) {
                            minDistances[currentIndex] = distance;
                            pClustersTmp_[currentIndex] = n;
                        }
                    }
                }
            }
        }

        // update cluster centers for next iteration
        sumValues.assign(kNumSeeds, 0.0f);
        sumCentroids.assign(kNumSeeds, Vector3i());
        clusterSize.assign(kNumSeeds, 0);

        // todo: try not to re-interate over the whole data
        for (int z = 0; z < dim_.z; z++) {
            for (int y = 0; y < dim_.y; y++) {
                for (int x = 0; x < dim_.x; x++) {
                    Vector3i pos(x,y,z);
                    int clusterIndex = pClustersTmp_[GetVoxelIndex(pos)];
                    sumValues[clusterIndex] += pData_[clusterIndex];
                    sumCentroids[clusterIndex] += pos;
                    clusterSize[clusterIndex]++;
                }
            }
        }

        for (int n = 0; n < kNumSeeds; n++) {
            int size = clusterSize[n];
            if (size <= 0) continue;

            centroidsValues_[n] = sumValues[n] / size;
            centroids_[n].x = std::min(util::round(static_cast<float>(sumCentroids[n].x) / size), dim_.x);
            centroids_[n].y = std::min(util::round(static_cast<float>(sumCentroids[n].y) / size), dim_.y);
            centroids_[n].z = std::min(util::round(static_cast<float>(sumCentroids[n].z) / size), dim_.z);
        }
    }
}

void SuperVoxel::SegmentByNumber(const int expectedClusterNum, const float compactness) {
    int clusterSize = static_cast<int>(sqrt(kNumElements_ / expectedClusterNum));
    if (clusterSize > 100) {
        cerr << "Warning - cluster size is too large." << endl; return;
    }
    SegmentBySize(clusterSize, compactness);
}

void SuperVoxel::SegmentBySize(const int expectedClusterSize, const float compactness) {
    // 1. RGB to LAB
    bgr2lab();

    // 2. Spread seeds
    detectGradients();
    getInitialCenters(expectedClusterSize);

    // 3. clustering
    clusteringIteration(expectedClusterSize, compactness);

    // 4. enforce connectivity
    enforceConnectivity(expectedClusterSize);

    // 5. generate segmentation results
    for (int i = 0; i < numCluster_; i++) {
        Cluster cls; {
            cls.center.x = 0;
            cls.center.y = 0;
            cls.num_pixel = 0;
        }
        clusters_.push_back(cls);
    }
    for (int y = 0; y < dim_.y; y++) {
        for (int x = 0; x < dim_.x; x++) {
            int pos = y * dim_.x + x;
            int index = pClusters_[pos];
            clusters_[index].center.x += x;
            clusters_[index].center.y += y;
            clusters_[index].num_pixel++;
        }
    }
    for (int i = 0; i < numCluster_; i++) {
        clusters_[i].center.x = util::round(static_cast<float>(clusters_[i].center.x) / clusters_[i].num_pixel);
        clusters_[i].center.y = util::round(static_cast<float>(clusters_[i].center.y) / clusters_[i].num_pixel);
        clusters_[i].center.x = std::min(clusters_[i].center.x, dim_.x - 1);
        clusters_[i].center.y = std::min(clusters_[i].center.y, dim_.y - 1);
    }

    // 6. cleaning
    pCLs.clear();
    pCAs.clear();
    pCBs.clear();
    pCXs.clear();
    pCYs.clear();

    delete [] pClustersTmp_;
    pClustersTmp_ = nullptr;
}

// ============================================================================
// Testing function.
// Draw the contours of segmented areas on image
// ============================================================================
void SuperVoxel::DrawContours(const CvScalar& drawing_color, const std::string& save_path) {
    const int dx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
    const int dy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};
    IplImage* contour = cvCreateImage(cvSize(dim_.x, dim_.y),
                                      pImage_->depth, pImage_->nChannels);
    cvCopy(pImage_, contour);
    int step = contour->widthStep;
    uchar* data = reinterpret_cast<uchar*>(contour->imageData);
    int const kTotalPixelNum = dim_.x * dim_.y;
    std::vector<bool> istaken(kTotalPixelNum, false);
    for (int row = 0; row < dim_.y; ++row) {
        for (int col = 0; col < dim_.x; ++col) {
            int diff = 0;
            int pos_a = row * dim_.x + col;
            for (int i = 0; i < 8; ++i) {
                int x = col + dx8[i];
                int y = row + dy8[i];
                if ((x >= 0 && x < dim_.x) && (y >= 0 && y < dim_.y)) {
                    int pos_b = y * dim_.x + x;
                    if ((false == istaken[pos_a]) &&
                        (pClusters_[pos_a] != pClusters_[pos_b])) {
                        ++diff;
                    }
                }
            }
            if (diff >= 2) {
                istaken[pos_a] = true;
                data[row * step + col * 3 + 0] = drawing_color.val[0];
                data[row * step + col * 3 + 1] = drawing_color.val[1];
                data[row * step + col * 3 + 2] = drawing_color.val[2];
            }
        }
    }
    cvSaveImage(save_path.c_str(), contour);
    cvReleaseImage(&contour);
}

void SuperVoxel::bgr2lab() {
    int step = pImage_->widthStep;
    uchar* data = reinterpret_cast<uchar*>(pImage_->imageData);
    double epsilon = 0.008856;
    double kappa   = 903.3;
    double Xr = 0.950456;
    double Yr = 1.0;
    double Zr = 1.088754;
    for (int y = 0; y < dim_.y; y++) {
        for (int x = 0; x < dim_.x; x++) {
            // Access pixel values.
            double B = static_cast<double>(data[y * step + x * 3 + 0]) / 255.0;
            double G = static_cast<double>(data[y * step + x * 3 + 1]) / 255.0;
            double R = static_cast<double>(data[y * step + x * 3 + 2]) / 255.0;

            // Step 1: RGB to XYZ conversion.
            double r = R <= 0.04045 ? R/12.92 : pow((R+0.055)/1.055, 2.4);
            double g = G <= 0.04045 ? G/12.92 : pow((G+0.055)/1.055, 2.4);
            double b = B <= 0.04045 ? B/12.92 : pow((B+0.055)/1.055, 2.4);

            double X = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
            double Y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
            double Z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;

            // Step 2: XYZ to LAB conversion.
            double xr = X/Xr;
            double yr = Y/Yr;
            double zr = Z/Zr;

            double fx = xr > epsilon ? pow(xr, 1.0/3.0) : (kappa*xr + 16.0) / 116.0;
            double fy = yr > epsilon ? pow(yr, 1.0/3.0) : (kappa*yr + 16.0) / 116.0;
            double fz = zr > epsilon ? pow(zr, 1.0/3.0) : (kappa*zr + 16.0) / 116.0;

            // Add converted color to 1-D vectors.
            int pos = y * dim_.x + x;
            pLs[pos] = 116.0 * fy - 16.0;
            pAs[pos] = 500.0 * (fx - fy);
            pBs[pos] = 200.0 * (fy - fz);
        }
    }
}

void SuperVoxel::detectGradients() {
    for (int y = 1; y < dim_.y - 1; y++) {
        for (int x = 1; x < dim_.x - 1; x++) {
            int i = GetVoxelIndex(Vector3i(x,y));
            double dx = (pLs[i-1]-pLs[i+1]) * (pLs[i-1]-pLs[i+1]) +
                        (pAs[i-1]-pAs[i+1]) * (pAs[i-1]-pAs[i+1]) +
                        (pBs[i-1]-pBs[i+1]) * (pBs[i-1]-pBs[i+1]);
            double dy = (pLs[i-dim_.x]-pLs[i+dim_.x]) * (pLs[i-dim_.x]-pLs[i+dim_.x]) +
                        (pAs[i-dim_.x]-pAs[i+dim_.x]) * (pAs[i-dim_.x]-pAs[i+dim_.x]) +
                        (pBs[i-dim_.x]-pBs[i+dim_.x]) * (pBs[i-dim_.x]-pBs[i+dim_.x]);
            pGradients_[i] = (dx + dy);
        }
    }
}

void SuperVoxel::getInitialCentroids(int expectedClusterSize) {
    // Step 1: calculate gradients for each voxel
    for (int z = 1; z < dim_.z - 1; z++) {
        for (int y = 1; y < dim_.y - 1; y++) {
            for (int x = 1; x < dim_.x - 1; x++) {
                int pos = GetVoxelIndex(Vector3i(x+1,y,z));
                int neg = GetVoxelIndex(Vector3i(x-1,y,z));
                float dx = (pData_[pos] - pData_[neg]) * (pData_[pos] - pData_[neg]);
                    pos = GetVoxelIndex(Vector3i(x,y+1,z));
                    neg = GetVoxelIndex(Vector3i(x,y-1,z));
                float dy = (pData_[pos] - pData_[neg]) * (pData_[pos] - pData_[neg]);
                    pos = GetVoxelIndex(Vector3i(x,y,z+1));
                    neg = GetVoxelIndex(Vector3i(x,y,z-1));
                float dz = (pData_[pos] - pData_[neg]) * (pData_[pos] - pData_[neg]);
                pGradients_[GetVoxelIndex(Vector3i(x,y,z))] = dx + dy + dz;
            }
        }
    }

    // Step 2: evenly dispatch the initial seeds (centroids).
    Vector3i numSeg = dim_ / expectedClusterSize;            // |--1--|--2--|--3--|-- numSeg = 3
    Vector3i remains = dim_ - numSeg * expectedClusterSize;  //                    -- remains = 2
    float roundFactor = static_cast<float>(expectedClusterSize) / 2.0f;   // like floor(value + 0.5);

    for (int z = 0; z < numSeg.z; z++) {
        float zErr = static_cast<float>(z * remains.z) / numSeg.z;      // avg remain per segment
        for (int y = 0; y < numSeg.y; y++) {
            float yErr = static_cast<float>(y * remains.y) / numSeg.y;
            for (int x = 0; x < numSeg.x; x++) {
                float xErr = static_cast<float>(x * remains.x) / numSeg.x;

                Vector3i segIndex; {
                    segIndex.x = std::min(util::round(x * expectedClusterSize + roundFactor + xErr), dim_.x - 1);
                    segIndex.y = std::min(util::round(y * expectedClusterSize + roundFactor + yErr), dim_.y - 1);
                    segIndex.z = std::min(util::round(z * expectedClusterSize + roundFactor + zErr), dim_.z - 1);
                }
                int index = GetVoxelIndex(Vector3i(x,y,z));

                centroids_.push_back(segIndex);
                centroidsValues_.push_back(pData_[index]);
            }
        }
    }

    // Step 3: Find local lowest gradient positions within 6 neighbors and perturb seeds.
    vector<Vector3i> neighbors; {                   // six direct neighbors
        neighbors.push_back(Vector3i(-1, 0, 0));    // left
        neighbors.push_back(Vector3i( 1, 0, 0));    // right
        neighbors.push_back(Vector3i( 0, 1, 0));    // top
        neighbors.push_back(Vector3i( 0,-1, 0));    // bottom
        neighbors.push_back(Vector3i( 0, 0,-1));    // front
        neighbors.push_back(Vector3i( 0, 0, 1));    // back
    }

    for (unsigned int n = 0; n < centroids_.size(); n++) {
        Vector3i originalCentroid = centroids_[n];
        int index = GetVoxelIndex(originalCentroid);
        int indexNew = index;
        for (unsigned int i = 0; i < neighbors.size(); i++) {
            Vector3i tempCentroid = originalCentroid + neighbors[i];
            if (tempCentroid.x >= 0 && tempCentroid.x < dim_.x &&
                tempCentroid.y >= 0 && tempCentroid.y < dim_.y &&
                tempCentroid.z >= 0 && tempCentroid.z < dim_.z) {
                int indexTemp = GetVoxelIndex(tempCentroid);
                if (pGradients_[indexTemp] < pGradients_[indexNew]) {
                    indexNew = indexTemp;   // index of least gradient after iteration.
                }
            }
        }
        if (index != indexNew) {  // shifted to neighbor
            centroids_[n] = GetVoxelPosition(n);
            centroidsValues_[n] = centroidsValues_[indexNew];
        }
    }

}

// ============================================================================
// Get the initial centers(seeds) based on given expected super pixel size.
// ============================================================================
void SuperVoxel::getInitialCenters(int expectedClusterSize) {
    // Step 1: evenly dispatch the initial seeds(centers).
    Vector3i strips = dim_ / expectedClusterSize;               // |--1--|--2--|--3--|--    strips = 3
    Vector3i deviat = dim_ - strips * expectedClusterSize;      // --                       deviat = 2
    Vector3f offset = Vector3f(expectedClusterSize/2.0, expectedClusterSize/2.0, expectedClusterSize/2.0);
    for (int y = 0; y < strips.y; y++) {
        float y_err = static_cast<float>(y) * deviat.y / strips.y;
        for (int x = 0; x < strips.x; x++) {
            float x_err = static_cast<float>(x) * deviat.x / strips.x;  // index * 2 / 5
            int x_pos = std::min(util::round(x * expectedClusterSize + offset.x + x_err), dim_.x - 1);
            int y_pos = std::min(util::round(y * expectedClusterSize + offset.y + y_err), dim_.y - 1);
            pCXs.push_back(x_pos);
            pCYs.push_back(y_pos);
            int position = y_pos * dim_.x + x_pos;
            pCLs.push_back(pLs[position]);
            pCAs.push_back(pAs[position]);
            pCBs.push_back(pBs[position]);
        }
    }

    // Step 2: Find local lowest gradient positions in 3x3 area and perturb seeds.
    const int kDx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
    const int kDy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};
    const int kSeedsNum = pCLs.size();
    for (int n = 0; n < kSeedsNum; n++) {
        int original_x = pCXs[n];
        int original_y = pCYs[n];
        int original_pos = original_y * dim_.x + original_x;
        int new_pos = original_pos;
        for (int i = 0; i < 8; ++i) {
            int temp_x = original_x + kDx8[i];
            int temp_y = original_y + kDy8[i];
            if (temp_x >= 0 && temp_x < dim_.x && temp_y >= 0 && temp_y < dim_.y) {
                int temp_pos = temp_y * dim_.x + temp_x;
                if (pGradients_[temp_pos] < pGradients_[new_pos]) {
                    new_pos = temp_pos;
                }
            }
        }
        if (original_pos != new_pos) {
            pCXs[n] = new_pos % dim_.x;
            pCYs[n] = new_pos / dim_.x;
            pCLs[n] = pLs[new_pos];
            pCAs[n] = pAs[new_pos];
            pCBs[n] = pBs[new_pos];
        }
    }
}

// ============================================================================
// Iteratively do super pixel clustering.
// Need post-processing to enforce connectivity.
// ============================================================================
void SuperVoxel::clusteringIteration(int expectedClusterSize, float compactness) {
    const int kSeedsNum = pCLs.size();
    const int kWindowOffset = expectedClusterSize * 2;

    // A set of variables containing the segmentation result information of each iteration.
    // The number of pixels dispatched to each center(seed).
    std::vector<int> cluster_size(kSeedsNum, 0);

    // To calculate the average value of color, we need to store the sum of pixel colors.
    std::vector<float> sum_l(kSeedsNum, 0.0);
    std::vector<float> sum_a(kSeedsNum, 0.0);
    std::vector<float> sum_b(kSeedsNum, 0.0);

    // To calculate the geometric center of each cluster, we need to store the sum of x/y offsets.
    std::vector<int> sum_x(kSeedsNum, 0);
    std::vector<int> sum_y(kSeedsNum, 0);

    // Store the distance from each pixel to its nearest clustering center.
    std::vector<float> min_distances(kNumElements_, DBL_MAX);

    // The weighting variable between color hint and space(position) hint.
    float weight = static_cast<float>(expectedClusterSize) / compactness;
//    float invert_weight = 1.0 / (weight * weight);

    // According to the paper ,we need to set windows centered at the clustering centers,
    // and to look up all the pixels in the wondow for clustering.
    // Following variables define the window size and position.
    for (int iter = 0; iter < 10; ++iter) {
        // According to the paper,the convergence error drops sharply in a
        // few iterations. They propose to run 10 iterations for experiemnts.
        for (int n = 0; n < kSeedsNum; ++n) {
            // Do clustering for each of the clusters (seeds).
            int x_start = std::max(0, pCXs[n] - kWindowOffset);
            int y_start = std::max(0, pCYs[n] - kWindowOffset);
            int x_end = std::min(dim_.x, pCXs[n] + kWindowOffset);
            int y_end = std::min(dim_.y, pCYs[n] + kWindowOffset);

            for (int y = y_start; y < y_end; y++) {
                for (int x = x_start; x < x_end; x++) {
                    int pos = y * dim_.x + x;
                    Vector3f currentColor = Vector3f(pLs[pos], pAs[pos], pBs[pos]);
                    Vector3f centerColor  = Vector3f(pCLs[n], pCAs[n], pCBs[n]);
                    Vector3f currentPos = Vector3f(x, y, 0);
                    Vector3f centerPos  = Vector3f(pCXs[n], pCYs[n], 0.0);

                    float distanceColor = (currentColor - centerColor).MagnituteSquared();
                    float distanceSpace = (currentPos - centerPos).MagnituteSquared();
                    float distance = distanceColor + distanceSpace / (weight * weight);

                    if (distance < min_distances[pos]) {
                        min_distances[pos] = distance;
                        pClustersTmp_[pos] = n;
                    }
                }
            }
        }

        // After assigning pixels, recalculate the cluster centers for next iter.
        sum_l.assign(kSeedsNum, 0.0);
        sum_a.assign(kSeedsNum, 0.0);
        sum_b.assign(kSeedsNum, 0.0);
        sum_x.assign(kSeedsNum, 0);
        sum_y.assign(kSeedsNum, 0);
        cluster_size.assign(kSeedsNum, 0);
        for (int y = 0; y < dim_.y; y++) {
            for (int x = 0; x < dim_.x; x++) {
                int pos = y * dim_.x + x;
                sum_l[pClustersTmp_[pos]] += pLs[pos];
                sum_a[pClustersTmp_[pos]] += pAs[pos];
                sum_b[pClustersTmp_[pos]] += pBs[pos];
                sum_y[pClustersTmp_[pos]] += y;
                sum_x[pClustersTmp_[pos]] += x;
                cluster_size[pClustersTmp_[pos]] += 1;
            }
        }
        for (int k = 0; k < kSeedsNum; k++) {
            if (cluster_size[k] <= 0) cluster_size[k] = 1;
            pCLs[k] = sum_l[k] / cluster_size[k];
            pCAs[k] = sum_a[k] / cluster_size[k];
            pCBs[k] = sum_b[k] / cluster_size[k];
            pCXs[k] = std::min(dim_.x - 1, cvRound(sum_x[k] / cluster_size[k]));
            pCYs[k] = std::min(dim_.y - 1, cvRound(sum_y[k] / cluster_size[k]));
        }
    }
}

// ============================================================================
// Post-processing. Enforce connectivity.
// After clustering iterations, a few stray labels may remian.
// That is, a few pixels in the vicinity of a large segment having the same
// label but not connected to it. We enforce connectivity finally by relabeling
// disjoint segments with the labels of the largest neighboring cluster.
// ============================================================================
void SuperVoxel::enforceConnectivity(int expectedClusterSize) {
    const int kDx4[4] = {-1, 0, 1, 0};
    const int kDy4[4] = {0, -1, 0, 1};
    const int kAverageSize = expectedClusterSize * expectedClusterSize;
    int i = 0;
    int clusterIndex = 0;
    int adjacent_index = 0;
    int* x_pos = new int[kNumElements_];
    int* y_pos = new int[kNumElements_];

    for (int y = 0; y < dim_.y; y++) {
        for (int x = 0; x < dim_.x; x++) {
            // We initialize all the elements in segmentation_map as -1.
            // Then by Traversing all the pixels, we assign them with segment
            // indexes. Since segmentation_map_ only contains
            // the final segmentation result, if it is less than 0, we
            // need to process the current pixel and get its segmentation result.
            if (pClusters_[i] < 0) {
                pClusters_[i] = clusterIndex;
                // Step 1:
                // Quickly find an adjacent label for use later if needed.
                for (int n = 0; n < 4; n++) {
                    int x = x + kDx4[n];
                    int y = y + kDy4[n];
                    if ((x >= 0 && x < dim_.x) && (y >= 0 && y < dim_.y)) {
                        int pos = y * dim_.x + x;
                        if (pClusters_[pos] >= 0) {
                            adjacent_index = pClusters_[pos];
                        }
                    }
                }
                // Step 2: traverse from the current pixel and find all the
                // connected components. Store their x and y positions in
                // "x_pos" and "y_pos". "*count" is the number of pixels in
                // current segment.
                x_pos[0] = x;
                y_pos[0] = y;
                // Store number of pixels in current segment.
                int num_of_pixels = 1;
                int* count;
                count = &num_of_pixels;
                findNext(x, y, clusterIndex, x_pos, y_pos, count);
                // Step 3: check if current segment is too small.
                // The limit is defined as half of the expected super pixel size.
                // If the current segment is too small, replace it with adjacent
                // pixel's segment index.
                if (num_of_pixels <= (kAverageSize >> 2)) {
                    for (int c = 0; c < num_of_pixels; ++c) {
                        int ind = y_pos[c] * dim_.x + x_pos[c];
                        // Replace the segmentation label with adjacent pixel's label.
                        pClusters_[ind] = adjacent_index;
                    }
                    clusterIndex--;
                }
                clusterIndex++;
            }
            i++;
        }
    }
    numCluster_ = clusterIndex;
    if (x_pos) {
        delete [] x_pos;
        x_pos = NULL;
    }
    if (y_pos) {
        delete [] y_pos;
        y_pos = NULL;
    }
}

// ============================================================================
// Find next connected components(pixel) which belongs to the same cluster.
// This is called recursively to get the size of connected area cluster.
// ============================================================================
void SuperVoxel::findNext(int x, int y, int clusterIndex, int* x_pos, int* y_pos, int* count) {
    const int kDx4[4] = {-1, 0, 1, 0};
    const int kDy4[4] = {0, -1, 0, 1};
    int old_index = pClustersTmp_[y * dim_.x + x];
    for (int i = 0; i < 4; ++i) {
        int col_new = x + kDx4[i];
        int row_new = y + kDy4[i];
        // Find a connected pixel belong to the same segment in pClustersTmp_.
        if ((row_new < dim_.y && row_new >= 0) && (col_new < dim_.x && col_new >= 0)) {
            int new_pos = row_new * dim_.x + col_new;
            if (pClusters_[new_pos] < 0 &&
                pClustersTmp_[new_pos] == old_index) {
                x_pos[*count] = col_new;
                y_pos[*count] = row_new;
                *count = *count + 1;
                pClusters_[new_pos] = clusterIndex;
                findNext(col_new, row_new, clusterIndex, x_pos, y_pos, count);
            }
        }
    }
}
