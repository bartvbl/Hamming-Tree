#pragma once

#include <shapeDescriptor/cpu/types/QUICCIImages.h>
#include <string>
#include <experimental/filesystem>

namespace SpinImage {
    namespace read {
        SpinImage::cpu::QUICCIImages QUICCImagesFromDumpFile(const std::experimental::filesystem::path &dumpFileLocation);
    }
}

