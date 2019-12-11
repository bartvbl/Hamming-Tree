#include <ZipLib/ZipFile.h>
#include <iomanip>
#include <fstream>
#include <cassert>
#include "IndexIO.h"

std::string formatFileIndex(IndexNodeID nodeID) {
    std::stringstream ss;
    ss << std::setw(10) << std::setfill('0') << nodeID;
    return ss.str();
}

IndexNode *index::io::readIndexNode(const std::experimental::filesystem::path& indexRootDirectory, IndexNodeID nodeID) {
    IndexNode* indexNode = new IndexNode(nodeID);

    std::experimental::filesystem::path indexFilePath = indexRootDirectory / "nodes" / (formatFileIndex(nodeID) + ".idx");

    ZipArchive::Ptr archive = ZipFile::Open(indexFilePath.string());

    ZipArchiveEntry::Ptr entry = archive->GetEntry("index_node.dat");
    std::istream* decompressStream = entry->GetDecompressionStream();

    // One extra 0 because of 0-termination
    std::array<char, 5> headerTitle = {0, 0, 0, 0, 0};
    IndexNodeID headerNodeID;
    unsigned long headerLinkArrayLength;
    unsigned long headerImageArrayLength;

    decompressStream->read(headerTitle.data(), 4);
    decompressStream->read((char*) &headerNodeID, sizeof(IndexNodeID));
    decompressStream->read((char*) &headerLinkArrayLength, sizeof(unsigned long));
    decompressStream->read((char*) &headerImageArrayLength, sizeof(unsigned long));

    assert(std::string(headerTitle.data()) == "INDX");

    indexNode->images.resize(headerImageArrayLength);
    indexNode->links.resize(headerLinkArrayLength);
    indexNode->linkTypes.resize(headerLinkArrayLength);

    decompressStream->read((char*) indexNode->links.data(), indexNode->links.size() * sizeof(IndexNodeID));
    decompressStream->read((char*) indexNode->linkTypes.data(), indexNode->linkTypes.sizeInBytes());
    decompressStream->read((char*) indexNode->images.data(), indexNode->images.size() * sizeof(unsigned int));

    return indexNode;
}

void index::io::writeIndexNode(const std::experimental::filesystem::path& indexRootDirectory, IndexNode *node) {
    std::basic_stringstream<char> outStream;

    unsigned long linkSize = node->links.size();
    unsigned long imageSize = node->images.size();

    outStream << "INDX";
    outStream.write((char*) &node->id, sizeof(IndexNodeID));
    outStream.write((char*) &linkSize, sizeof(unsigned long));
    outStream.write((char*) &imageSize, sizeof(unsigned long));
    outStream.write((char*) node->links.data(), linkSize * sizeof(IndexNodeID));
    outStream.write((char*) node->linkTypes.data(), node->linkTypes.sizeInBytes());
    outStream.write((char*) node->images.data(), imageSize * sizeof(unsigned int));

    std::experimental::filesystem::path indexDirectory = indexRootDirectory / "nodes";
    std::experimental::filesystem::create_directories(indexDirectory);

    std::experimental::filesystem::path indexFile = indexDirectory / (formatFileIndex(node->id) + ".idx");

    const std::string filename = "index_node.dat";
    auto archive = ZipFile::Open(indexFile.string());
    if(archive->GetEntry(filename) != nullptr) {
        archive->RemoveEntry(filename);
    }
    auto entry = archive->CreateEntry(filename);
    entry->UseDataDescriptor(); // read stream only once
    entry->SetCompressionStream(outStream);
    ZipFile::SaveAndClose(archive, indexFile.string());
}

BucketNode *index::io::readBucketNode(const std::experimental::filesystem::path& indexRootDirectory, IndexNodeID nodeID) {
    BucketNode* bucketNode = new BucketNode(nodeID);

    std::experimental::filesystem::path indexFilePath = indexRootDirectory / "buckets" / (formatFileIndex(nodeID) + ".bkt");

    ZipArchive::Ptr archive = ZipFile::Open(indexFilePath.string());

    ZipArchiveEntry::Ptr entry = archive->GetEntry("bucket_node.dat");
    std::istream* decompressStream = entry->GetDecompressionStream();

    std::array<char, 5> headerTitle = {0, 0, 0, 0, 0};
    IndexNodeID headerNodeID;
    unsigned long headerIndexEntryCount;

    decompressStream->read(headerTitle.data(), 4);
    decompressStream->read((char*) &headerNodeID, sizeof(IndexNodeID));
    decompressStream->read((char*) &headerIndexEntryCount, sizeof(unsigned long));

    assert(std::string(headerTitle.data()) == "BCKT");

    bucketNode->images.resize(headerIndexEntryCount);

    decompressStream->read((char*) bucketNode->images.data(), bucketNode->images.size() * sizeof(IndexEntry));

    return bucketNode;
}

void index::io::writeBucketNode(const std::experimental::filesystem::path& indexRootDirectory, BucketNode *node) {
    std::basic_stringstream<char> outStream;

    unsigned long imageSize = node->images.size();

    outStream << "BCKT";
    outStream.write((char*) &node->id, sizeof(IndexNodeID));
    outStream.write((char*) &imageSize, sizeof(unsigned long));
    outStream.write((char*) node->images.data(), imageSize * sizeof(IndexEntry));

    std::experimental::filesystem::path bucketDirectory = indexRootDirectory / "buckets";
    std::experimental::filesystem::create_directories(bucketDirectory);

    std::experimental::filesystem::path bucketFile = bucketDirectory / (formatFileIndex(node->id) + ".bkt");

    const std::string filename = "bucket_node.dat";
    auto archive = ZipFile::Open(bucketFile.string());
    if(archive->GetEntry(filename) != nullptr) {
        archive->RemoveEntry(filename);
    }
    auto entry = archive->CreateEntry(filename);
    entry->UseDataDescriptor(); // read stream only once
    entry->SetCompressionStream(outStream);
    ZipFile::SaveAndClose(archive, bucketFile.string());
}





Index index::io::loadIndex(std::experimental::filesystem::path rootFile) {
    return Index(std::experimental::filesystem::path(), nullptr, nullptr, 0, 0);
}

void index::io::writeIndex(Index index, std::experimental::filesystem::path outDirectory) {

}