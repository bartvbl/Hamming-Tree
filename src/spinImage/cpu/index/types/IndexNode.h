#pragma once

const bool INDEX_LINK_INDEX_NODE = false;
const bool INDEX_LINK_BUCKET_NODE = true;

#include <vector>
#include <spinImage/cpu/types/BoolVector.h>
#include "Index.h"

struct IndexNode {
    const IndexNodeID id;

    // The number of unsigned integers per image can vary depending on index level
    // However, this is the only way we can store all image types in a single type
    // which simplifies the remaining implementation a lot
    std::vector<unsigned int> images;

    std::vector<IndexNodeID> links;
    // 1 bit per image/link. 0 = index node, 1 = bucket node
    BoolVector linkTypes;

    IndexNode(IndexNodeID id) : id(id) {}
};