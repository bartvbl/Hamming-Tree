#pragma once

#include "hostMesh.h"

HostMesh generateSphere();
HostMesh generateCylinder(float3_cpu orgin, float3_cpu direction, float radius, float height, unsigned int numSlices);