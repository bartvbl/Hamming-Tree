#pragma once

#include <string>
#include <experimental/filesystem>

namespace SpinImage {
    namespace read {
        ShapeDescriptor::cpu::array<ShapeDescriptor::QUICCIDescriptor> QUICCImagesFromDumpFile(const std::experimental::filesystem::path &dumpFileLocation);
    }
}

