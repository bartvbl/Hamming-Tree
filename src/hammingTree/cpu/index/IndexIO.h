#pragma once

#include <hammingTree/cpu/index/types/Index.h>
#include <string>
#include <hammingTree/cpu/index/types/NodeBlock.h>

namespace SpinImage {
    namespace index {
        namespace io {
            Index readIndex(std::experimental::filesystem::path indexDirectory);

            void writeIndex(const Index& index, std::experimental::filesystem::path indexDirectory);

            NodeBlock* readNodeBlock(const std::string &blockID, const std::experimental::filesystem::path &indexRootDirectory);

            void writeNodeBlock(const NodeBlock *block, const std::experimental::filesystem::path &indexRootDirectory);
        }
    }
}