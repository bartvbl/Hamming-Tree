#pragma once
#include <stddef.h>

// Due to parsing order of header files, these must be at the top, before the remaining includes
// They represent a tradeoff between the number of files/images the database is able to represent,
// relative to the amount of data it costs to store them on disk and in memory
typedef unsigned int IndexFileID;
typedef size_t IndexNodeID;
typedef unsigned int IndexImageID;

const unsigned int NODES_PER_BLOCK = 4097;
const unsigned int NODE_SPLIT_THRESHOLD = 256;
