#pragma once

#include "IndexEntry.h"
#include <shapeDescriptor/cpu/types/QuiccImage.h>

struct NodeBlockEntry {
    IndexEntry indexEntry;
    QuiccImage image;

    NodeBlockEntry(const IndexEntry entry, QuiccImage image) : indexEntry(entry), image(image) {}
    NodeBlockEntry() {}
};