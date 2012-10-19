#include "BlockController.h"

BlockController::BlockController() {
    blockSize.x = blockSize.y = blockSize.z = 0;
    for (int surface = 0; surface < 6; surface++) {
        adjacentBlocks[surface] = SURFACE_NULL;
    }
}

BlockController::~BlockController() {
    localGraph.clear();
    pDataManager->~DataManager();
    pFeatureTracker->~FeatureTracker();
}

void BlockController::InitParameters(Vector3i partition, Vector3i blockCoord, DataSet ds) {
    initAdjacentBlocks(partition, blockCoord);

    pDataManager = new DataManager();
    pDataManager->PreloadDataSequence(partition, blockCoord, ds, t);
    pDataManager->CreateNewMaskVolume();
    pDataManager->InitTFSettings(ds.tf);

    blockSize = pDataManager->GetVolumeDimension();
    pFeatureTracker = new FeatureTracker(blockSize);
    pFeatureTracker->SetTFResolution(pDataManager->GetTFResolution());
    pFeatureTracker->SetTFOpacityMap(pDataManager->GetTFOpacityMap());
    pFeatureTracker->SetVolumeDataPointer(pDataManager->GetDataPointer(t));
}

void BlockController::TrackForward(Vector3i partition, Vector3i blockCoord, DataSet ds) {
    pDataManager->PreloadDataSequence(partition, blockCoord, ds, t);
    pFeatureTracker->TrackFeature(pDataManager->GetDataPointer(t),
                                  LOW_THRESHOLD, HIGH_THRESHOLD,
                                  TRACKING_FORWARD, TRACKING_MODE_DIRECT);
    ExtractAllFeatures();
}

void BlockController::ExtractAllFeatures() {
    int tfRes = pFeatureTracker->GetTFResolution();
    for (int z = 0; z < blockSize.z; z++) {
        for (int y = 0; y < blockSize.y; y++) {
            for (int x = 0; x < blockSize.x; x++) {
                int index = z * blockSize.y * blockSize.x + y * blockSize.x + x;
                if (pFeatureTracker->GetMaskVolumePointer()[index] != 0) {
                    continue;
                }
                float *pVolume = pDataManager->GetDataPointer(t);
                int tfIndex = (int)(pVolume[index] * (float)(tfRes-1));
                float opacity = pFeatureTracker->GetTFOpacityMap()[tfIndex];
                if (opacity >= LOW_THRESHOLD && opacity <= HIGH_THRESHOLD) {
                    pFeatureTracker->FindNewFeature(DataPoint(x,y,z), LOW_THRESHOLD,
                                                    HIGH_THRESHOLD);
                }
            }
        }
    }
    pFeatureTracker->SaveExtractedFeatures(t);
}

void BlockController::initAdjacentBlocks(Vector3i partition, Vector3i blockCoord) {
    int px = partition.x,   py = partition.y,   pz = partition.z;
    int x = blockCoord.x,   y = blockCoord.y,   z = blockCoord.z;

    adjacentBlocks[SURFACE_LEFT]   = x-1 >= 0  ? px*py*z + px*y + x - 1 : -1;
    adjacentBlocks[SURFACE_RIGHT]  = x+1 <  px ? px*py*z + px*y + x + 1 : -1;
    adjacentBlocks[SURFACE_BOTTOM] = y-1 >= 0  ? px*py*z + px*(y-1) + x : -1;
    adjacentBlocks[SURFACE_TOP]    = y+1 <  py ? px*py*z + px*(y+1) + x : -1;
    adjacentBlocks[SURFACE_FRONT]  = z-1 >= 0  ? px*py*(z-1) + px*y + x : -1;
    adjacentBlocks[SURFACE_BACK]   = z+1 <  pz ? px*py*(z+1) + px*y + x : -1;
}

vector<int> BlockController::GetAdjacentBlocks() {
    vector<int> indices;
    for (uint i = 0; i < adjacentBlocks.size(); i++) {
        if (adjacentBlocks[i] != -1) {
            indices.push_back(adjacentBlocks[i]);
        }
    }
    return indices;
}

void BlockController::UpdateLocalGraph(int blockID, Vector3i blockCoord) {
    localGraph.clear();

    vector<Feature> *pCurrentFeatures;
    pCurrentFeatures = pFeatureTracker->GetFeatureVectorPointer(t);
    if (pCurrentFeatures->size() == 0) {
        return;
    }

    for (uint i = 0; i < pCurrentFeatures->size(); i++) {
        Feature feature = pCurrentFeatures->at(i);
        vector<int> touchedSurfaces = feature.TouchedSurfaces;

        for (uint j = 0; j < touchedSurfaces.size(); j++) {
            int surface = touchedSurfaces[j];
            int adjacentBlock = adjacentBlocks[surface];
            if (adjacentBlock == -1) {
                continue;
            }

            Vector3i centroid = feature.BoundaryCentroid[surface];

            Edge edge;
            edge.id         = feature.ID;
            edge.start      = blockID;
            edge.end        = adjacentBlock;
            edge.centroid   = centroid + blockSize * blockCoord;

            localGraph.push_back(edge);
        }
    }
}
