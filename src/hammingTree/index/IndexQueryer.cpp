#include <queue>
#include "IndexQueryer.h"
#include "NodeBlockCache.h"
#include <hammingTree/index/types/BitCountMipmapStack.h>
#include <algorithm>
#include <fstream>

struct UnvisitedNode {
    UnvisitedNode(IndexPath indexPath, std::string unvisitedNodeID, unsigned int minDistance, unsigned int nodeLevel)
    : path(indexPath), nodeID(unvisitedNodeID), minDistanceScore(minDistance), level(nodeLevel) {}

    IndexPath path;
    std::string nodeID;
    unsigned int minDistanceScore;
    unsigned int level;

    // We want the open node priority queue to sort items by lowest score
    // Since the priority queue by default optimises for finding the highest sorted element,
    // we need to invert the sort order.
    bool operator< (const UnvisitedNode &right) const {
        if(minDistanceScore != right.minDistanceScore) {
            return minDistanceScore > right.minDistanceScore;
        }
        return level < right.level;
    }
};

struct SearchResultEntry {
    SearchResultEntry(IndexEntry entry, const ShapeDescriptor::QUICCIDescriptor &imageEntry, std::string debug_nodeID, unsigned int minDistance)
        : reference(entry), image(imageEntry), debug_indexPath(debug_nodeID), distanceScore(minDistance) {}

    IndexEntry reference;
    ShapeDescriptor::QUICCIDescriptor image;
    std::string debug_indexPath;
    unsigned int distanceScore;

    bool operator< (const SearchResultEntry &right) const {
        return distanceScore < right.distanceScore;
    }
};

std::stringstream IDBuilder;
unsigned int debug_visitedNodeCount = 0;

std::string appendPath(const std::string &parentNodeID, unsigned long childIndex) {
    std::string byteString = parentNodeID;
    const std::string characterMap = "0123456789abcdef";
    byteString += characterMap.at((childIndex >> 12U) & 0x0FU);
    byteString += characterMap.at((childIndex >> 8U) & 0x0FU);
    byteString += characterMap.at((childIndex >> 4U) & 0x0FU);
    byteString += characterMap.at((childIndex & 0x0FU));
    byteString += "/";
    return byteString;

}

unsigned int computeHammingDistance(const ShapeDescriptor::QUICCIDescriptor &needle, const ShapeDescriptor::QUICCIDescriptor &haystack) {

    // Wherever pixels don't match, we apply a penalty for each of them
    unsigned int score = 0;
    for(int i = 0; i < ShapeDescriptor::QUICCIDescriptorLength; i++) {
        score += std::bitset<32>(needle.contents[i] ^ haystack.contents[i]).count();
    }

    return score;
}

const unsigned int computeMinDistanceThreshold(std::vector<SearchResultEntry> &currentSearchResults) {
    return currentSearchResults.empty() ?
               std::numeric_limits<unsigned int>::max()
               : currentSearchResults.at(currentSearchResults.size() - 1).distanceScore;
}

void visitNode(
        const NodeBlock* block,
        IndexPath path,
        const std::string &nodeID,
        const unsigned int level,
        std::priority_queue<UnvisitedNode> &closedNodeQueue,
        std::vector<SearchResultEntry> &currentSearchResults,
        const BitCountMipmapStack &queryImageMipmapStack,
        const ShapeDescriptor::QUICCIDescriptor &queryImage) {
    // Divide child nodes over both queues
    const unsigned int childLevel = level + 1;
    // If we have not yet acquired any search results, disable the threshold
    const unsigned int searchResultScoreThreshold =
            computeMinDistanceThreshold(currentSearchResults);


    //std::cout << "\rVisiting node " << debug_visitedNodeCount << " -> " << currentSearchResults.size() << " search results, " << closedNodeQueue.size() << " queued nodes, " << searchResultScoreThreshold  << " vs " << closedNodeQueue.top().minDistanceScore << " - " << nodeID << std::flush;
    for(int child = 0; child < NODES_PER_BLOCK; child++) {
        if(block->childNodeIsLeafNode[child]) {
            //std::cout << "Child " << child << " is leaf node!" << std::endl;
            // If child is a leaf node, insert its images into the search result list
            for(const NodeBlockEntry& entry : block->leafNodeContents.at(child)) {
                unsigned int distanceScore = computeHammingDistance(queryImage, entry.image);

                // Only consider the image if it is potentially better than what's there already
                if(distanceScore <= searchResultScoreThreshold) {
                    currentSearchResults.emplace_back(entry.indexEntry, entry.image, nodeID, distanceScore);
                }
            }
        } else {
            // If the child is an intermediate node, enqueue it in the closed node list
            IndexPath childPath = path.append(child);
            unsigned int minDistanceScore = childPath.computeMinDistanceTo(queryImageMipmapStack);

            if(minDistanceScore <= searchResultScoreThreshold) {
                //unsigned int hammingDistance = computeHammingDistance(queryImageMipmapStack, childPath, level);
                //std::cout << "Enqueued " << appendPath(nodeID, child) << " -> " << minDistanceScore << std::endl;
                //std::cout << "Enqueued " << appendPath(nodeID, child) << " -> " << minDistanceScore << std::endl;
                closedNodeQueue.emplace(
                    childPath,
                    appendPath(nodeID, child),
                    minDistanceScore,
                    childLevel);
            }
        }
    }
}

std::vector<SpinImage::index::QueryResult> SpinImage::index::query(Index &index, const ShapeDescriptor::QUICCIDescriptor &queryImage, unsigned int resultCountLimit, debug::QueryRunInfo* runInfo) {
    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

    BitCountMipmapStack queryImageBitCountMipmapStack(queryImage);

    NodeBlockCache cache(100000, 2500000, index.indexDirectory, true);

    std::priority_queue<UnvisitedNode> closedNodeQueue;
    std::vector<SearchResultEntry> currentSearchResults;

    currentSearchResults.reserve(30000 + resultCountLimit + NODES_PER_BLOCK * NODE_SPLIT_THRESHOLD);

    // Root node path is not referenced, so can be left uninitialised
    IndexPath rootNodePath;
    closedNodeQueue.emplace(rootNodePath, "", 0, 0);
    debug_visitedNodeCount = 0;

    std::array<double, spinImageWidthPixels * spinImageWidthPixels> executionTimesSeconds;
    std::fill(executionTimesSeconds.begin(), executionTimesSeconds.end(), -1);

    std::chrono::steady_clock::time_point queryStartTime = std::chrono::steady_clock::now();

    std::cout << "Query in progress.." << std::endl;

    // Iteratively add additional nodes until there's no chance any additional node can improve the best distance score
    while(  !closedNodeQueue.empty() &&
            computeMinDistanceThreshold(currentSearchResults) >= closedNodeQueue.top().minDistanceScore) {
        UnvisitedNode nextBestUnvisitedNode = closedNodeQueue.top();
        closedNodeQueue.pop();
        const NodeBlock* block = cache.getNodeBlockByID(nextBestUnvisitedNode.nodeID);
        visitNode(block, nextBestUnvisitedNode.path, nextBestUnvisitedNode.nodeID, nextBestUnvisitedNode.level,
                closedNodeQueue, currentSearchResults, queryImageBitCountMipmapStack, queryImage);
        debug_visitedNodeCount++;

        // Re-sort search results
        std::sort(currentSearchResults.begin(), currentSearchResults.end());

        // Chop off irrelevant search results
        if(currentSearchResults.size() > resultCountLimit) {
            currentSearchResults.erase(currentSearchResults.begin() + resultCountLimit, currentSearchResults.end());
        }

        // Time measurement

        std::chrono::steady_clock::time_point queryEndTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(queryEndTime - queryStartTime);
        double timeUntilNow = double(duration.count()) / 1000000000.0;
        executionTimesSeconds.at(closedNodeQueue.top().minDistanceScore) = timeUntilNow;

        /*std::cout << "Search results: ";
        for(int i = 0; i < currentSearchResults.size(); i++) {
            std::cout << currentSearchResults.at(i).distanceScore << ", ";
        }
        std::cout << std::endl;
        std::cout << "Closed nodes: ";
        for(int i = 0; i < debug_closedNodeQueue.size(); i++) {
            std::cout << debug_closedNodeQueue.at(i).minDistanceScore << "|" << debug_closedNodeQueue.at(i).nodeID << ", ";
        }
        std::cout << std::endl;*/
    }

    std::cout << "Query finished, " << computeMinDistanceThreshold(currentSearchResults) << " vs " << closedNodeQueue.top().minDistanceScore << std::endl;

    std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << std::endl << "Query complete. " << std::endl;
    std::cout << "Total execution time: " << float(duration.count()) / 1000.0f << " seconds" << std::endl;

    std::vector<SpinImage::index::QueryResult> queryResults;
    queryResults.reserve(currentSearchResults.size());

    /*for(int i = 0; i < currentSearchResults.size(); i++) {
        queryResults.push_back({currentSearchResults.at(i).reference, (float)currentSearchResults.at(i).distanceScore, currentSearchResults.at(i).image});
        std::cout << "Result " << i << ": "
               "file " << currentSearchResults.at(i).reference.fileIndex <<
               ", image " << currentSearchResults.at(i).reference.imageIndex <<
               ", score " << currentSearchResults.at(i).distanceScore <<
               ", path " << currentSearchResults.at(i).debug_indexPath << std::endl;
    }*/

    if(runInfo != nullptr) {
        runInfo->totalQueryTime = double(duration.count()) / 1000.0;
        runInfo->threadCount = 1;
        runInfo->distanceTimes = executionTimesSeconds;
    }

    return queryResults;
}
