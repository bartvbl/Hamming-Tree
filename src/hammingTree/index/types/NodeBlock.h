#pragma once

#include <array>
#include <hammingTree/types/BoolArray.h>
#include <hammingTree/index/types/IndexEntry.h>
#include <mutex>
#include "NodeBlockEntry.h"

struct NodeBlock {
    std::string identifier;
    BoolArray<NODES_PER_BLOCK> childNodeIsLeafNode = {true};
    std::array<std::vector<NodeBlockEntry>, NODES_PER_BLOCK> leafNodeContents;
    std::mutex blockLock;

    NodeBlock() {}
};