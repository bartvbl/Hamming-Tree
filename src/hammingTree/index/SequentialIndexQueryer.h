#pragma once

#include <hammingTree/index/types/Index.h>
#include <hammingTree/index/types/IndexEntry.h>
#include "IndexQueryer.h"

namespace SpinImage {
    namespace index {
        std::vector<QueryResult> sequentialQuery(std::experimental::filesystem::path dumpDirectory, const ShapeDescriptor::QUICCIDescriptor &queryImage, unsigned int resultCount, unsigned int fileStartIndex, unsigned int fileEndIndex, unsigned int num_threads = 0, debug::QueryRunInfo* runInfo = nullptr);
    }
}