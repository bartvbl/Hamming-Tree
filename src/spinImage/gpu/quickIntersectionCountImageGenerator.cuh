#pragma once

#include <spinImage/common/types/array.h>
#include <spinImage/libraryBuildSettings.h>
#include <spinImage/gpu/types/QUICCImages.h>
#include <spinImage/gpu/types/Mesh.h>
#include <spinImage/gpu/types/DeviceOrientedPoint.h>
#include <spinImage/gpu/types/methods/QUICCIDescriptor.h>

namespace SpinImage {
    namespace debug {
        struct QUICCIExecutionTimes {
            double generationTimeSeconds;
            double meshScaleTimeSeconds;
            double redistributionTimeSeconds;
            double totalExecutionTimeSeconds;
        };
    }

    namespace gpu {
        SpinImage::array<SpinImage::gpu::QUICCIDescriptor> generateQUICCImages(
                Mesh device_mesh,
                array<DeviceOrientedPoint> device_descriptorOrigins,
                float supportRadius,
                SpinImage::debug::QUICCIExecutionTimes* executionTimes = nullptr);
    }
}