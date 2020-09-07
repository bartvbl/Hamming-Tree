#pragma once

#include <string>
#include <experimental/filesystem>
#include <shapeDescriptor/common/types/methods/QUICCIDescriptor.h>
#include <shapeDescriptor/cpu/types/array.h>

namespace SpinImage {
    namespace read {
        ShapeDescriptor::cpu::array<ShapeDescriptor::QUICCIDescriptor> QUICCImagesFromDumpFile(const std::experimental::filesystem::path &dumpFileLocation);
    }
}

