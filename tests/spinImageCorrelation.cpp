#include "spinImageCorrelation.h"
#include "utilities/spinImageGenerator.h"
#include <catch2/catch.hpp>
#include <spinImage/common/buildSettings/derivedBuildSettings.h>
#include <spinImage/cpu/spinImageSearcher.h>
#include <spinImage/common/types/array.h>
#include <spinImage/libraryBuildSettings.h>
#include <spinImage/utilities/CUDAContextCreator.h>
#include <cuda_runtime.h>
#include <nvidia/helper_cuda.h>
#include <spinImage/utilities/copy/hostDescriptorsToDevice.h>
#include <spinImage/gpu/spinImageSearcher.cuh>
#include <iostream>
#include <spinImage/utilities/dumpers/spinImageDumper.h>



TEST_CASE("Basic correlation computation (Spin Images)", "[correlation]") {

    SECTION("Equivalent images") {
        array<spinImagePixelType> constantImage =
                generateRepeatingTemplateSpinImage(0, 1, 0, 1, 0, 1, 0, 1);

        float correlation = SpinImage::cpu::computeSpinImagePairCorrelation(constantImage.content, constantImage.content, 0, 0);

        delete[] constantImage.content;
        REQUIRE(correlation == 1);
    }

    SECTION("Opposite images") {
        array<spinImagePixelType> positiveImage = generateEmptySpinImages(1);
        array<spinImagePixelType> negativeImage = generateEmptySpinImages(1);

        for (int i = 0; i < spinImageWidthPixels * spinImageWidthPixels; i++) {
            positiveImage.content[i] = float(i);
            negativeImage.content[i] = float(i) * -1;
        }

        float correlation = SpinImage::cpu::computeSpinImagePairCorrelation(positiveImage.content, negativeImage.content, 0, 0);
        delete[] positiveImage.content;
        delete[] negativeImage.content;
        REQUIRE(correlation == -1);
    }

    SECTION("Equivalent constant images") {
        array<spinImagePixelType> positiveImage = generateRepeatingTemplateSpinImage(
                5, 5, 5, 5, 5, 5, 5, 5);
        array<spinImagePixelType> negativeImage = generateRepeatingTemplateSpinImage(
                5, 5, 5, 5, 5, 5, 5, 5);

        float correlation = SpinImage::cpu::computeSpinImagePairCorrelation(positiveImage.content, negativeImage.content, 0, 0);
        delete[] positiveImage.content;
        delete[] negativeImage.content;
        REQUIRE(std::isnan(correlation));
    }

    SECTION("Different constant images") {
        array<spinImagePixelType> positiveImage = generateRepeatingTemplateSpinImage(
                2, 2, 2, 2, 2, 2, 2, 2);
        array<spinImagePixelType> negativeImage = generateRepeatingTemplateSpinImage(
                5, 5, 5, 5, 5, 5, 5, 5);

        float correlation = SpinImage::cpu::computeSpinImagePairCorrelation(positiveImage.content, negativeImage.content, 0, 0);

        float otherCorrelation = SpinImage::cpu::computeSpinImagePairCorrelation(negativeImage.content, positiveImage.content, 0, 0);

        delete[] positiveImage.content;
        delete[] negativeImage.content;
        REQUIRE(std::isnan(correlation));
        REQUIRE(std::isnan(otherCorrelation));
    }
}

const float correlationThreshold = 0.00001f;

TEST_CASE("Ranking of Spin Images on CPU", "[correlation]") {
    array<spinImagePixelType> imageSequence = generateKnownSpinImageSequence(imageCount, pixelsPerImage);
    array<quasiSpinImagePixelType> quasiImageSequence = generateKnownQuasiSpinImageSequence(imageCount, pixelsPerImage);

    SECTION("Ensuring equivalent images have a correlation of 1") {
        for (int i = 1; i < imageCount - 1; i++) {
            float pairCorrelation = SpinImage::cpu::computeSpinImagePairCorrelation(imageSequence.content,
                                                                                imageSequence.content, i, i);

            // We'll allow for some rounding errors here.
            REQUIRE(std::abs(pairCorrelation - 1.0f) < correlationThreshold);
        }
    }

    SECTION("Spin Image averages make sense") {
        float previousAverage = SpinImage::cpu::computeImageAverage(imageSequence.content, 0);
        REQUIRE(previousAverage == 1.0f / float(spinImageWidthPixels * spinImageWidthPixels));

        for(int image = 1; image < imageCount; image++) {
            float average = SpinImage::cpu::computeImageAverage(imageSequence.content, image);

            REQUIRE(previousAverage < average);

            previousAverage = average;
        }

        REQUIRE(previousAverage == 1.0f - (1.0f / float(spinImageWidthPixels * spinImageWidthPixels)));
    }

    SECTION("Ranking of known images on CPU") {
        std::vector<std::vector<SpinImageSearchResult>> resultsCPU = SpinImage::cpu::findSpinImagesInHaystack(
                imageSequence, imageCount, imageSequence, imageCount);

        // First and last image are excluded because they are completely constant.
        // For these the pearson correlation coefficient is not defined, and they don't
        // really occur in spin images anyway
        for (int i = 0; i < imageCount; i++) {
            // Allow for shared first places
            int resultIndex = 0;
            while (std::abs(resultsCPU.at(i).at(resultIndex).correlation - 1.0f) < correlationThreshold) {
                if (resultsCPU.at(i).at(resultIndex).imageIndex == i) {
                    break;
                }
                resultIndex++;
            }

            REQUIRE(std::abs(resultsCPU.at(i).at(resultIndex).correlation - 1.0f) < correlationThreshold);
            REQUIRE(resultsCPU.at(i).at(resultIndex).imageIndex == i);
        }



        SpinImage::utilities::createCUDAContext();

        // Compute the GPU equivalent
        array<spinImagePixelType> device_haystackImages = SpinImage::copy::hostDescriptorsToDevice(imageSequence, imageCount);

        array<SpinImageSearchResults> searchResults = SpinImage::gpu::findSpinImagesInHaystack(device_haystackImages, imageCount, device_haystackImages, imageCount);

        // The GPU version has a different floating point computation path relative to the CPU equivalent
        // This means the cumulative rounding can be noticeably different
        // That's why this test has a wider error margin than others.
        const float maxRoundingError = 0.001f;

        for(int image = 0; image < imageCount; image++) {
            for (int i = 0; i < SEARCH_RESULT_COUNT; i++) {
                std::cout << "Image " << image << ", result " << i << ": scores(" << searchResults.content[image].resultScores[i] << ", " << resultsCPU.at(image).at(i).correlation << ") & indices (" << searchResults.content[image].resultIndices[i] << ", " << resultsCPU.at(image).at(i).imageIndex << ")" << std::endl;

                REQUIRE(std::abs(searchResults.content[image].resultScores[i] - resultsCPU.at(image).at(i).correlation) < maxRoundingError);
            }
        }

        delete[] imageSequence.content;
        cudaFree(device_haystackImages.content);


    }
}

TEST_CASE("Ranking of Spin Images on the GPU") {

    SpinImage::utilities::createCUDAContext();

    array<spinImagePixelType> imageSequence = generateKnownSpinImageSequence(imageCount, pixelsPerImage);

    array<spinImagePixelType> device_haystackImages = SpinImage::copy::hostDescriptorsToDevice(imageSequence, imageCount);

    SECTION("Ranking by generating search results on GPU") {
        array<SpinImageSearchResults> searchResults = SpinImage::gpu::findSpinImagesInHaystack(device_haystackImages, imageCount, device_haystackImages, imageCount);

        SECTION("Equivalent images are the top search results") {
            for (int image = 0; image < imageCount; image++) {
                // Allow for shared first places
                int resultIndex = 0;
                while (std::abs(searchResults.content[image].resultScores[resultIndex] - 1.0f) < correlationThreshold) {
                    if (searchResults.content[image].resultIndices[resultIndex] == image) {
                        break;
                    }
                    resultIndex++;
                }

                REQUIRE(std::abs(searchResults.content[image].resultScores[resultIndex] - 1.0f) < correlationThreshold);
                REQUIRE(searchResults.content[image].resultIndices[resultIndex] == image);
            }
        }

        SECTION("Scores are properly sorted") {
            for(int image = 0; image < imageCount; image++) {
                for (int i = 0; i < SEARCH_RESULT_COUNT - 1; i++) {
                    float firstImageCurrentScore = searchResults.content[image].resultScores[i];
                    float firstImageNextScore = searchResults.content[image].resultScores[i + 1];

                    REQUIRE(firstImageCurrentScore >= firstImageNextScore);
                }
            }
        }
    }

    SECTION("Ranking by computing rank indices") {

        array<unsigned int> results = SpinImage::gpu::computeSpinImageSearchResultRanks(device_haystackImages, imageCount, device_haystackImages, imageCount);

        for(int i = 0; i < imageCount; i++) {
            REQUIRE(results.content[i] == 0);
        }

    }

    SECTION("Ranking by computing rank indices, reversed image sequence") {
        std::reverse(imageSequence.content, imageSequence.content + imageCount * pixelsPerImage);

        array<spinImagePixelType> device_haystackImages_reversed = SpinImage::copy::hostDescriptorsToDevice(imageSequence, imageCount);

        array<unsigned int> results = SpinImage::gpu::computeSpinImageSearchResultRanks(device_haystackImages_reversed, imageCount, device_haystackImages_reversed, imageCount);

        for(int i = 0; i < imageCount; i++) {
            REQUIRE(results.content[i] == 0);
        }

        cudaFree(device_haystackImages_reversed.content);
    }

    cudaFree(device_haystackImages.content);

}