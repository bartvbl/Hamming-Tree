#include <arrrgh.hpp>
#include <shapeDescriptor/cpu/index/types/Index.h>
#include <shapeDescriptor/cpu/index/IndexIO.h>
#include <shapeDescriptor/cpu/index/IndexQueryer.h>
#include <lodepng.h>
#include <shapeDescriptor/cpu/types/QUICCIImages.h>
#include <shapeDescriptor/utilities/dumpers/descriptors.h>
#include <shapeDescriptor/cpu/index/types/BitCountMipmapStack.h>

int main(int argc, const char** argv) {
    arrrgh::parser parser("queryindex", "Query an existing index of QUICCI images.");
    const auto& indexDirectory = parser.add<std::string>(
            "index-directory", "The location of the directory containing the existing index.", '\0', arrrgh::Required, "");
    const auto& queryImage = parser.add<std::string>(
            "query-image-file", "The location of a PNG file representing the image that should be queried.", '\0', arrrgh::Optional, "");
    const auto& showHelp = parser.add<bool>(
            "help", "Show this help message.", 'h', arrrgh::Optional, false);

    try
    {
        parser.parse(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        parser.show_usage(std::cerr);
        exit(1);
    }

    // Show help if desired
    if(showHelp.value())
    {
        return 0;
    }

    std::cout << "Reading query image.." << std::endl;
    std::experimental::filesystem::path imageFilePath = queryImage.value();
    if(!std::experimental::filesystem::exists(imageFilePath)) {
        std::cerr << "Query image file " << std::experimental::filesystem::absolute(imageFilePath) << " was not found." << std::endl;
        return 0;
    }

    std::vector<unsigned char> imageData;
    unsigned int width;
    unsigned int height;
    lodepng::decode(imageData, width, height, imageFilePath.string(), LCT_RGBA);
    std::cout << "Image is " << width << "x" << height << std::endl;

    assert(width == spinImageWidthPixels && height == spinImageWidthPixels);

    QuiccImage queryQUIICIMage;

    for(unsigned int row = 0; row < spinImageWidthPixels; row++) {
        unsigned int chunk = 0;
        for(unsigned int col = 0; col < spinImageWidthPixels; col++) {
            const unsigned int colourChannelsPerPixel = 4;
            unsigned int bit = imageData.at(colourChannelsPerPixel * ((spinImageWidthPixels - 1 - row) * spinImageWidthPixels + col)) != 0 ? 1 : 0;
            chunk = chunk | (bit << (31U - col%32));
            if(col % 32 == 31) {
                queryQUIICIMage[row * (spinImageWidthPixels / 32) + (col / 32)] = chunk;
                chunk = 0;
            }
        }
    }
    BitCountMipmapStack(queryQUIICIMage).print();

    std::cout << "Reading index metadata.." << std::endl;
    Index index = SpinImage::index::io::readIndex(indexDirectory.value());

    const unsigned int resultCount = 25;

    std::cout << "Querying index.." << std::endl;
    std::vector<SpinImage::index::QueryResult> searchResults = SpinImage::index::query(index, queryQUIICIMage, resultCount);

    std::cout << "Dumping results.." << std::endl;
    ShapeDescriptor::cpu::array<ShapeDescriptor::QUICCIDescriptor> imageBuffer;
    imageBuffer.content = new ShapeDescriptor::QUICCIDescriptor[std::max<unsigned int>(searchResults.size(), 1)];
    imageBuffer.length = std::max<unsigned int>(searchResults.size(), 1);

    ShapeDescriptor::QUICCIDescriptor blankImage;
    std::fill(blankImage.contents, blankImage.contents + 128, 0);
    std::fill(imageBuffer.content, imageBuffer.content + std::max<unsigned int>(searchResults.size(), 1), blankImage);

    for(int searchResult = 0; searchResult < searchResults.size(); searchResult++) {
        // Not the greatest way of handling this, but allows the libraries to be 'glued' together without too much pain
        imageBuffer.content[searchResult] = *reinterpret_cast<ShapeDescriptor::QUICCIDescriptor*>(&searchResults.at(searchResult).image);
    }

    ShapeDescriptor::dump::descriptors(imageBuffer, "searchResults.png", 50);
}