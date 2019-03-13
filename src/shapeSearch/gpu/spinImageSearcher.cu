#include <shapeSearch/gpu/types/DeviceMesh.h>
#include <cuda_runtime.h>
#include <shapeSearch/libraryBuildSettings.h>
#include <curand_mtgp32_kernel.h>
#include <tgmath.h>
#include <assert.h>
#include <iostream>
#include <climits>
#include <cfloat>
#include <chrono>
#include "nvidia/helper_cuda.h"
#include "spinImageSearcher.cuh"

const unsigned int warpCount = 2;


__inline__ __device__ float warpAllReduceSum(float val) {
	for (int mask = warpSize/2; mask > 0; mask /= 2)
		val += __shfl_xor_sync(0xFFFFFFFF, val, mask);
	return val;
}

template<typename pixelType>
__device__ float computeImagePairCorrelation(pixelType* descriptors,
											 pixelType* otherDescriptors,
                                             size_t spinImageIndex,
                                             size_t otherImageIndex,
                                             float averageX, float averageY) {
	float threadSquaredSumX = 0;
	float threadSquaredSumY = 0;
	float threadMultiplicativeSum = 0;

	for (int y = 0; y < spinImageWidthPixels; y++)
	{
		const int warpSize = 32;
	    for (int x = threadIdx.x % 32; x < spinImageWidthPixels; x += warpSize)
		{
            const size_t spinImageElementCount = spinImageWidthPixels * spinImageWidthPixels;

			pixelType pixelValueX = descriptors[spinImageIndex * spinImageElementCount + (y * spinImageWidthPixels + x)];
			pixelType pixelValueY = otherDescriptors[otherImageIndex * spinImageElementCount + (y * spinImageWidthPixels + x)];

			float deltaX = float(pixelValueX) - averageX;
			float deltaY = float(pixelValueY) - averageY;

			threadSquaredSumX += deltaX * deltaX;
			threadSquaredSumY += deltaY * deltaY;
			threadMultiplicativeSum += deltaX * deltaY;
		}
	}

	float squaredSumX = sqrt(warpAllReduceSum(threadSquaredSumX));
    float squaredSumY = sqrt(warpAllReduceSum(threadSquaredSumY));
    float multiplicativeSum = warpAllReduceSum(threadMultiplicativeSum);

    float correlation = -1;

    // Avoid zero divisions
    if(squaredSumX != 0 && squaredSumY != 0)
    {
        correlation = multiplicativeSum / (squaredSumX * squaredSumY);
    }

    return correlation;
}

template<typename pixelType>
__device__ float computeImageAverage(pixelType* descriptors, size_t spinImageIndex)
{
	const unsigned int spinImageElementCount = spinImageWidthPixels * spinImageWidthPixels;

	float threadPartialSum = 0;

	for (int y = 0; y < spinImageWidthPixels; y++)
	{
		for (int x = threadIdx.x; x < spinImageWidthPixels; x += blockDim.x)
		{
			float pixelValue = float(descriptors[spinImageIndex * spinImageElementCount + (y * spinImageWidthPixels + x)]);
			threadPartialSum += pixelValue;
		}
	}

	return warpAllReduceSum(threadPartialSum) / float(spinImageElementCount);
}

template<typename pixelType>
__global__ void calculateImageAverages(pixelType* images, float* averages) {
	// This kernel assumes one warp per image
	assert(blockDim.x == 32);

	size_t imageIndex = blockIdx.x;

	float average = computeImageAverage<pixelType>(images, imageIndex);

	if(threadIdx.x == 0) {
		averages[imageIndex] = average;
	}
}

template<typename pixelType>
__global__ void generateSearchResults(pixelType* needleDescriptors,
									  size_t needleImageCount,
									  pixelType* haystackDescriptors,
									  size_t haystackImageCount,
									  ImageSearchResults* searchResults,
									  float* needleImageAverages,
									  float* haystackImageAverages) {

	size_t needleImageIndex = warpCount * blockIdx.x + (threadIdx.x / 32);

	if(needleImageIndex >= needleImageCount) {
	    return;
	}

	// Pearson correlation, which is used as distance measure, means closer to 1 is better
	// We thus initialise the score to the absolute minimum, so that any score is higher.
	static_assert(SEARCH_RESULT_COUNT == 128, "Array initialisation needs to change if search result count is changed");
	size_t threadSearchResultImageIndexes[SEARCH_RESULT_COUNT / 32] = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
	float threadSearchResultScores[SEARCH_RESULT_COUNT / 32] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX}; // FLT_MIN represents smallest POSITIVE float

    const int blockCount = (SEARCH_RESULT_COUNT / 32);

	float needleImageAverage = needleImageAverages[needleImageIndex];

	for(size_t haystackImageIndex = 0; haystackImageIndex < haystackImageCount; haystackImageIndex++) {
		float haystackImageAverage = haystackImageAverages[haystackImageIndex];

		float correlation = computeImagePairCorrelation(needleDescriptors,
														haystackDescriptors,
														needleImageIndex,
														haystackImageIndex,
														needleImageAverage,
														haystackImageAverage);



		// Since most images will not make it into the top ranking, we do a quick check to avoid a search
		// This saves a few instructions.
		if(correlation > __shfl_sync(0xFFFFFFFF, threadSearchResultScores[(SEARCH_RESULT_COUNT / 32) - 1], 31)) {
		    unsigned int foundIndex = 0;
            for(int block = 0; block < blockCount; block++) {
                bool threadExceeds = threadSearchResultScores[block] < correlation;
                unsigned int bitString = __ballot_sync(0xFFFFFFFF, threadExceeds);
                unsigned int firstSet = __ffs(bitString) - 1;
                if(firstSet < 32) {
                    foundIndex = (block * 32) + (firstSet);
                    break;
                }
            }

            int startBlock = foundIndex / 32;
            const int endBlock = blockCount - 1;
			const int laneID = threadIdx.x % 32;

            // We first shift all values to the right for "full" 32-value blocks
            // Afterwards, we do one final iteration to shift only the values that are
            // block will never be 0, which ensures the loop body does not go out of range
            for(int block = endBlock; block > startBlock; block--) {
				int sourceThread = laneID - 1;
				int sourceBlock = block;
				int destinationBlock = block;

				if(laneID == 0) {
					sourceThread = 31;
				} else if(laneID == 31){
                    sourceBlock = block - 1;
				}

				threadSearchResultScores[destinationBlock] = __shfl_sync(0xFFFFFFFF, threadSearchResultScores[sourceBlock], sourceThread);
				threadSearchResultImageIndexes[destinationBlock] = __shfl_sync(0xFFFFFFFF, threadSearchResultImageIndexes[sourceBlock], sourceThread);
            }

            // This shifts over values in the block where we're inserting the new value.
            // As such it requires some more fine-grained control.
			if(laneID >= foundIndex % 32) {
				int targetThread = laneID - 1;

				threadSearchResultScores[startBlock] = __shfl_sync(0xFFFFFFFF, threadSearchResultScores[startBlock], targetThread);
				threadSearchResultImageIndexes[startBlock] = __shfl_sync(0xFFFFFFFF, threadSearchResultImageIndexes[startBlock], targetThread);

				if(laneID == foundIndex % 32) {
					threadSearchResultScores[startBlock] = correlation;
					threadSearchResultImageIndexes[startBlock] = haystackImageIndex;
				}
			}

		}
	}


    const unsigned int laneID = threadIdx.x % 32;
	// Storing search results
	for(int block = 0; block < blockCount; block++) {
        searchResults[needleImageIndex].resultIndices[block * 32 + laneID] = threadSearchResultImageIndexes[block];
        searchResults[needleImageIndex].resultScores[block * 32 + laneID] = threadSearchResultScores[block];
    }

}

template<typename pixelType>
array<ImageSearchResults> doFindDescriptorsInHaystack(
                                 array<pixelType> device_needleDescriptors,
                                 size_t needleImageCount,
                                 array<pixelType> device_haystackDescriptors,
                                 size_t haystackImageCount)
{
    // Step 1: Compute image averages, since they're constant and are needed for each comparison

	float* device_needleImageAverages;
	float* device_haystackImageAverages;
	checkCudaErrors(cudaMalloc(&device_needleImageAverages, needleImageCount * sizeof(float)));
	checkCudaErrors(cudaMalloc(&device_haystackImageAverages, haystackImageCount * sizeof(float)));

	std::cout << "\t\tComputing image averages.." << std::endl;
	calculateImageAverages<pixelType><<<needleImageCount, 32>>>(device_needleDescriptors.content, device_needleImageAverages);
	calculateImageAverages<pixelType><<<haystackImageCount, 32>>>(device_haystackDescriptors.content, device_haystackImageAverages);
	checkCudaErrors(cudaDeviceSynchronize());

	// Step 2: Perform search

	size_t searchResultBufferSize = needleImageCount * sizeof(ImageSearchResults);
	ImageSearchResults* device_searchResults;
	checkCudaErrors(cudaMalloc(&device_searchResults, searchResultBufferSize));

	std::cout << "\t\tPerforming search.." << std::endl;
    auto start = std::chrono::steady_clock::now();

    generateSearchResults<<<(needleImageCount / warpCount) + 1, 32 * warpCount>>>(
                                                    device_needleDescriptors.content,
													needleImageCount,
													device_haystackDescriptors.content,
													haystackImageCount,
													device_searchResults,
													device_needleImageAverages,
													device_haystackImageAverages);
    checkCudaErrors(cudaDeviceSynchronize());

    std::chrono::milliseconds duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << "\t\t\tExecution time: " << duration.count() << std::endl;

    // Step 3: Copying results to CPU

	array<ImageSearchResults> searchResults;
	searchResults.content = new ImageSearchResults[needleImageCount];
	searchResults.length = needleImageCount;

	checkCudaErrors(cudaMemcpy(searchResults.content, device_searchResults, searchResultBufferSize, cudaMemcpyDeviceToHost));

	// Cleanup

	cudaFree(device_needleImageAverages);
	cudaFree(device_haystackImageAverages);
	cudaFree(device_searchResults);

	return searchResults;
}

array<ImageSearchResults> findDescriptorsInHaystack(
		array<classicSpinImagePixelType > device_needleDescriptors,
		size_t needleImageCount,
		array<classicSpinImagePixelType > device_haystackDescriptors,
		size_t haystackImageCount) {
	return doFindDescriptorsInHaystack<classicSpinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}

array<ImageSearchResults> findDescriptorsInHaystack(
		array<newSpinImagePixelType> device_needleDescriptors,
		size_t needleImageCount,
		array<newSpinImagePixelType > device_haystackDescriptors,
		size_t haystackImageCount) {
	return doFindDescriptorsInHaystack<newSpinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}