#pragma once

#include <shapeDescriptor/common/types/methods/QUICCIDescriptor.h>
#include "IndexEntry.h"

struct NodeBlockEntry {
    IndexEntry indexEntry;
    ShapeDescriptor::QUICCIDescriptor image;

    NodeBlockEntry(const IndexEntry entry, ShapeDescriptor::QUICCIDescriptor image) : indexEntry(entry), image(image) {}
    NodeBlockEntry() {}
};