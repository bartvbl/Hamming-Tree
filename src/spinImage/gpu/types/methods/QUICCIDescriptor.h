#pragma once

#include <array>
#include <spinImage/cpu/types/QuiccImage.h>

namespace SpinImage {
    namespace gpu {
        struct QUICCIDescriptor {
            std::array<unsigned int, (spinImageWidthPixels * spinImageWidthPixels) / (sizeof(unsigned int) * 8)> contents;
        };
    }
}

