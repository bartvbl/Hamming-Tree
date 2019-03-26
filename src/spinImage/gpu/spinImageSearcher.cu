#include <spinImage/gpu/types/DeviceMesh.h>
#include <spinImage/libraryBuildSettings.h>
#include <cuda_runtime.h>
#include <curand_mtgp32_kernel.h>
#include <tgmath.h>
#include <assert.h>
#include <iostream>
#include <climits>
#include <cfloat>
#include <chrono>
#include "nvidia/helper_cuda.h"
#include "spinImageSearcher.cuh"

const unsigned int warpCount = 16;


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

    pixelType pixelValueX;
    pixelType pixelValueY;

	for (int y = 0; y < spinImageWidthPixels; y++)
	{
		const int warpSize = 32;
	    for (int x = threadIdx.x % 32; x < spinImageWidthPixels; x += warpSize)
		{
            const size_t spinImageElementCount = spinImageWidthPixels * spinImageWidthPixels;

			pixelValueX = descriptors[spinImageIndex * spinImageElementCount + (y * spinImageWidthPixels + x)];
			pixelValueY = otherDescriptors[otherImageIndex * spinImageElementCount + (y * spinImageWidthPixels + x)];

			float deltaX = float(pixelValueX) - averageX;
			float deltaY = float(pixelValueY) - averageY;

			threadSquaredSumX += deltaX * deltaX;
			threadSquaredSumY += deltaY * deltaY;
			threadMultiplicativeSum += deltaX * deltaY;
		}
	}

	float squaredSumX = float(sqrt(warpAllReduceSum(threadSquaredSumX)));
    float squaredSumY = float(sqrt(warpAllReduceSum(threadSquaredSumY)));
    float multiplicativeSum = warpAllReduceSum(threadMultiplicativeSum);

    float correlation = -1;

    if(squaredSumX != 0 || squaredSumY != 0)
    {
        // Avoiding zero divisions
        const float smallestNonZeroFactor = 0.0001;
        squaredSumX = max(squaredSumX, smallestNonZeroFactor);
        squaredSumY = max(squaredSumY, smallestNonZeroFactor);
		if(multiplicativeSum > 0) {
			multiplicativeSum = max(multiplicativeSum, smallestNonZeroFactor * smallestNonZeroFactor);
		} else {
			multiplicativeSum = min(multiplicativeSum, -(smallestNonZeroFactor * smallestNonZeroFactor));
		}

        correlation = multiplicativeSum / (squaredSumX * squaredSumY);
    } else if(squaredSumX == 0 && squaredSumY == 0 && pixelValueX == pixelValueY) {
        // If both sums are 0, both sequences must be constant
        // If any pair of pixels has the same value, by extension both images must be identical
        // Therefore, even though correlation is not defined at constant sequences,
        // the correlation value should be 1.
        correlation = 1;
    } else {
        // In case both images are constant, but have different values,
        // we define the correlation to be the fraction of their pixel values
        correlation = min(pixelValueX, pixelValueY) / max(pixelValueX, pixelValueY);
    }

    return correlation;
}


template<typename pixelType>
__global__ void calculateImageAverages(pixelType* images, float* averages) {
	size_t imageIndex = blockIdx.x;

	// Only support up to 32 warps
	__shared__ float warpSums[32];

	if(threadIdx.x < 32) {
	    warpSums[threadIdx.x] = 0;
	}

	__syncthreads();

    const unsigned int spinImageElementCount = spinImageWidthPixels * spinImageWidthPixels;

    float threadPartialSum = 0;

    for (int x = threadIdx.x; x < spinImageElementCount; x += blockDim.x)
    {
        float pixelValue = float(images[imageIndex * spinImageElementCount + x]);
        threadPartialSum += pixelValue;
    }

    float warpSum = warpAllReduceSum(threadPartialSum);

    if(threadIdx.x % 32 == 0) {
        warpSums[threadIdx.x / 32] = warpSum;
    }

    __syncthreads();

    if(threadIdx.x < 32) {
        float threadSum = warpSums[threadIdx.x];
        threadSum = warpAllReduceSum(threadSum);
        if(threadIdx.x == 0) {
            averages[imageIndex] = threadSum / float(spinImageElementCount);
        }
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

                if(laneID == 0) {
                    sourceThread = 31;
                }
                if(laneID == 31) {
                    sourceBlock = block - 1;
                }

				threadSearchResultScores[block] = __shfl_sync(0xFFFFFFFF, threadSearchResultScores[sourceBlock], sourceThread);
				threadSearchResultImageIndexes[block] = __shfl_sync(0xFFFFFFFF, threadSearchResultImageIndexes[sourceBlock], sourceThread);
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
    checkCudaErrors(cudaDeviceSynchronize());
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

array<ImageSearchResults> SpinImage::gpu::findDescriptorsInHaystack(
		array<spinImagePixelType> device_needleDescriptors,
		size_t needleImageCount,
		array<spinImagePixelType> device_haystackDescriptors,
		size_t haystackImageCount) {
	return doFindDescriptorsInHaystack<spinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}

array<ImageSearchResults> SpinImage::gpu::findDescriptorsInHaystack(
		array<quasiSpinImagePixelType> device_needleDescriptors,
		size_t needleImageCount,
		array<quasiSpinImagePixelType> device_haystackDescriptors,
		size_t haystackImageCount) {
	return doFindDescriptorsInHaystack<quasiSpinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}





















const int indexBasedWarpCount = 4;

template<typename pixelType>
__global__ void generateElementWiseSearchResults(
									  pixelType* needleDescriptors,
									  size_t needleImageCount,
									  pixelType* haystackDescriptors,
									  size_t haystackImageCount,
									  unsigned int* searchResults,
									  float* needleImageAverages,
									  float* haystackImageAverages) {

    size_t needleImageIndex = blockIdx.x;

	__shared__ pixelType referenceImage[spinImageWidthPixels * spinImageWidthPixels];
	for(unsigned int index = threadIdx.x; index < spinImageWidthPixels * spinImageWidthPixels; index += blockDim.x) {
		referenceImage[index] = needleDescriptors[spinImageWidthPixels * spinImageWidthPixels * needleImageIndex + index];
	}

	__syncthreads();

	float needleImageAverage = needleImageAverages[needleImageIndex];
	float correspondingImageAverage = haystackImageAverages[needleImageIndex];

	float referenceCorrelation = computeImagePairCorrelation(referenceImage,
															 haystackDescriptors,
															 0,
                                                             needleImageIndex,
															 needleImageAverage,
															 correspondingImageAverage);

	if(referenceCorrelation == 1) {
		if(threadIdx.x % 32 == 0) {
			searchResults[needleImageIndex] = 0;
		}
		return;
	}

	unsigned int searchResultRank = 0;

	for(size_t haystackImageIndex = 0; haystackImageIndex < haystackImageCount; haystackImageIndex++) {
		if(needleImageIndex == haystackImageIndex) {
			continue;
		}

		float haystackImageAverage = haystackImageAverages[haystackImageIndex];

		float correlation = computeImagePairCorrelation(referenceImage,
														haystackDescriptors,
														0,
														haystackImageIndex,
														needleImageAverage,
														haystackImageAverage);

		if(correlation > referenceCorrelation) {
			searchResultRank++;
		}
	}

	if(threadIdx.x % 32 == 0) {
		searchResults[needleImageIndex] = searchResultRank;
	}
}

template<typename pixelType>
array<unsigned int> doFindCorrespondingSearchResultIndices(
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
	checkCudaErrors(cudaDeviceSynchronize());
	checkCudaErrors(cudaGetLastError());
	calculateImageAverages<pixelType><<<haystackImageCount, 32>>>(device_haystackDescriptors.content, device_haystackImageAverages);
	checkCudaErrors(cudaDeviceSynchronize());
    checkCudaErrors(cudaGetLastError());

	// Step 2: Perform search

	size_t searchResultBufferSize = needleImageCount * sizeof(unsigned int);
	unsigned int* device_searchResults;
	checkCudaErrors(cudaMalloc(&device_searchResults, searchResultBufferSize));

	std::cout << "\t\tPerforming search.." << std::endl;
	auto start = std::chrono::steady_clock::now();

	generateElementWiseSearchResults<<<needleImageCount, 32 * indexBasedWarpCount>>>(
					device_needleDescriptors.content,
					needleImageCount,
					device_haystackDescriptors.content,
					haystackImageCount,
					device_searchResults,
					device_needleImageAverages,
					device_haystackImageAverages);
	checkCudaErrors(cudaDeviceSynchronize());
    checkCudaErrors(cudaGetLastError());

	std::chrono::milliseconds duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	std::cout << "\t\t\tExecution time: " << duration.count() << std::endl;


	// Step 3: Copying results to CPU

	array<unsigned int> resultIndices;
	resultIndices.content = new unsigned int[needleImageCount];
	resultIndices.length = needleImageCount;

	checkCudaErrors(cudaMemcpy(resultIndices.content, device_searchResults, searchResultBufferSize, cudaMemcpyDeviceToHost));

	// Cleanup

	cudaFree(device_needleImageAverages);
	cudaFree(device_haystackImageAverages);
	cudaFree(device_searchResults);

	return resultIndices;
}


array<unsigned int> SpinImage::gpu::computeSearchResultRanks(
		array<spinImagePixelType> device_needleDescriptors,
		size_t needleImageCount,
		array<spinImagePixelType> device_haystackDescriptors,
		size_t haystackImageCount) {
    return doFindCorrespondingSearchResultIndices<spinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}

array<unsigned int> SpinImage::gpu::computeSearchResultRanks(
		array<quasiSpinImagePixelType> device_needleDescriptors,
		size_t needleImageCount,
		array<quasiSpinImagePixelType> device_haystackDescriptors,
		size_t haystackImageCount) {
    return doFindCorrespondingSearchResultIndices<quasiSpinImagePixelType>(device_needleDescriptors, needleImageCount, device_haystackDescriptors, haystackImageCount);
}
