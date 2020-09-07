#pragma once

#include <shapeDescriptor/cpu/types/Mesh.h>

namespace SpinImage {
    namespace utilities {
        ShapeDescriptor::cpu::Mesh fitMeshInsideSphereOfRadius(ShapeDescriptor::cpu::Mesh &input, float radius);
    }
}
