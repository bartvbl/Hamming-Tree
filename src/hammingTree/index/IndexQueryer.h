#pragma once

#include <hammingTree/index/types/Index.h>
#include <hammingTree/index/types/IndexEntry.h>
#include <shapeDescriptor/common/types/methods/QUICCIDescriptor.h>

namespace SpinImage {
    namespace index {
        struct QueryResult {
            IndexEntry entry;
            float score = 0;
            ShapeDescriptor::QUICCIDescriptor image;

            bool operator<(const QueryResult &rhs) const {
                if(score != rhs.score) {
                    return score < rhs.score;
                }

                return entry < rhs.entry;
            }
        };

        namespace debug {
            struct QueryRunInfo {
                double totalQueryTime = -1;
                unsigned int threadCount = 0;
                std::array<double, spinImageWidthPixels * spinImageWidthPixels> distanceTimes;

                QueryRunInfo() {
                    std::fill(distanceTimes.begin(), distanceTimes.end(), -1);
                }
            };
        }

        std::vector<QueryResult> query(Index &index, const ShapeDescriptor::QUICCIDescriptor &queryImage,
                unsigned int resultCountLimit, debug::QueryRunInfo* runInfo = nullptr);
    }
}



