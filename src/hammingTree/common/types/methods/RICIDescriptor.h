#pragma once

#include <shapeDescriptor/treeBuildSettings.h>

namespace ShapeDescriptor {
    struct RICIDescriptor {
        radialIntersectionCountImagePixelType contents[spinImageWidthPixels * spinImageWidthPixels];
    };
}

