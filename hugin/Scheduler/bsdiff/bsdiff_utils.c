/*
 * Copyright (C) 2018 Orange
 *
 * This software is distributed under the terms and conditions of the 'BSD-3-Clause-Clear'
 * license which can be found in the file 'LICENSE.txt' in this package distribution
 * or at 'https://spdx.org/licenses/BSD-3-Clause-Clear.html'.
 */

/**
 * @author Emile-Hugo Spir
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <err.h>

#define MIN(x, y) (((x)<(y)) ? (x) : (y))

static inline void swap(off_t * array, off_t index1, off_t  index2)
{
	off_t tmp = array[index1];
	array[index1] = array[index2];
	array[index2] = tmp;
}

static void split(off_t *index, off_t *value, off_t start, off_t matchLength, off_t offset)
{
	if (matchLength < 16)
	{
		//Bubble sort
		for (off_t k = start, lastStrike; k < start + matchLength; k += lastStrike)
		{
			lastStrike = 1;
			off_t x = value[index[k] + offset];

			for (off_t i = k + 1; i < start + matchLength; i++)
			{
				//Is minimum?
				if (value[index[i] + offset] < x)
				{
					x = value[index[i] + offset];
					lastStrike = 0;
				}

				//Found multiple hits with the same value, we move them just after the original minimum
				if (value[index[i] + offset] == x)
				{
					swap(index, k + lastStrike, i);
					lastStrike += 1;
				}
			}

			for (off_t i = 0; i < lastStrike; i++)
				value[index[k + i]] = k + lastStrike - 1;

			if (lastStrike == 1)
				index[k] = -1;
		}
	}
	else
	{
		off_t pivotValue = value[index[start + matchLength / 2] + offset];
		off_t nbBelowPivotValue = 0, nbBelowOrEqualToPivotValue = 0;

		//Count items below our insertion pivot
		for (off_t i = start; i < start + matchLength; i++)
		{
			if (value[index[i] + offset] < pivotValue)
				nbBelowPivotValue++;

			if (value[index[i] + offset] == pivotValue)
				nbBelowOrEqualToPivotValue++;
		}

		//Compute the number of value that will have to be moved before the final pivot position
		nbBelowPivotValue += start;
		nbBelowOrEqualToPivotValue += nbBelowPivotValue;

		off_t equalValueCount = 0, higherValueCount = 0;

		//Move items <= to the pivot value in the first part of the array
		//	Each half are still unsorted
		for (off_t i = start; i < nbBelowPivotValue;)
		{
			//Item below the pivot is less than pivot. Perfect
			if (value[index[i] + offset] < pivotValue)
			{
				i++;
			}
				//Item has the same value than pivot, we move it just after the pivot
			else if (value[index[i] + offset] == pivotValue)
			{
				swap(index, i, nbBelowPivotValue + equalValueCount);
				equalValueCount++;
			}
				//Item has a higher value than pivot, we move it after the pivot and the space allocated to it
			else
			{
				swap(index, i, nbBelowOrEqualToPivotValue + higherValueCount);
				higherValueCount++;
			}
		}

		//We found all values that belonged before the pivot but are still missing some that are equal to the pivot
		while (nbBelowPivotValue + equalValueCount < nbBelowOrEqualToPivotValue)
		{
			if (value[index[nbBelowPivotValue + equalValueCount] + offset] == pivotValue)
			{
				equalValueCount++;
			}
			else
			{
				swap(index, nbBelowPivotValue + equalValueCount, nbBelowOrEqualToPivotValue + higherValueCount);
				higherValueCount++;
			}
		}

		//If we had values < to pivot
		if (nbBelowPivotValue > start)
			split(index, value, start, nbBelowPivotValue - start, offset);

		//Mark all values equal to pivot
		for (off_t i = 0; i < nbBelowOrEqualToPivotValue - nbBelowPivotValue; i++)
			value[index[nbBelowPivotValue + i]] = nbBelowOrEqualToPivotValue - 1;

		//If only one occurence of the pivot, we update the index
		if (nbBelowPivotValue == nbBelowOrEqualToPivotValue - 1)
			index[nbBelowPivotValue] = -1;

		//Had values after the pivot
		if (start + matchLength > nbBelowOrEqualToPivotValue)
			split(index, value, nbBelowOrEqualToPivotValue, start + matchLength - nbBelowOrEqualToPivotValue, offset);
	}
}

void qsufsort(off_t *index, off_t *value, const uint8_t *old, off_t oldSize)
{
	off_t buckets[256];

	//Count the number of hit in each bucket, sum them upward (each bucket has the number of hit in bucket <= to it, then drop the last one)
	memset(buckets, 0, sizeof(buckets));

	for (off_t i = 0; i < oldSize; i++)
		buckets[old[i]] += 1;

	for (size_t i = 1; i < 256; i++)
		buckets[i] += buckets[i - 1];

	//We drop the last bucket (the sum of all values)
	for (size_t i = 255; i > 0; i--)
		buckets[i] = buckets[i - 1];

	buckets[0] = 0;

	//Write to index the sorted rank of each value in old
	// 	(buckets[old[i]] is the number of time the value old[i] as been met + the base offset of the value, i.e. the number of occurences of values < itself)
	for (off_t i = 0; i < oldSize; i++)
		index[++buckets[old[i]]] = i;

	//Write to `value` the number of bytes <= to old[i]
	for (off_t i = 0; i < oldSize; i++)
		value[i] = buckets[old[i]];
	value[oldSize] = 0;

	//Find all byte value for which only one match was found, and write that to index
	for (off_t i = 1; i < 256; i++)
	{
		if (buckets[i] == buckets[i - 1] + 1)
			index[buckets[i]] = -1;
	}

	//Encode strike length
	index[0] = -1;

	for (off_t h = 1; index[0] != -(oldSize + 1) && h < oldSize; h *= 2)
	{
		off_t matchLength = 0, i = 0;
		while (i < oldSize + 1)
		{
			//If the previous strike end up on another strike, extend the previous one
			if (index[i] < 0)
			{
				matchLength -= index[i];
				i -= index[i];
			}
			else
			{
				//Mark the real length of the strike
				if (matchLength)
					index[i - matchLength] = -matchLength;

				//???????
				matchLength = value[index[i]] + 1 - i;

				//Sort index so that value[index[i]] <= value[index[i + 1]] from i + h and for length matchLength
				if(i + matchLength + h < oldSize)
					split(index, value, i, matchLength, h);
				else if(matchLength > h)
					split(index, value, i, matchLength - h, h);

				i += matchLength;
				matchLength = 0;
			}
		}

		if (matchLength)
			index[i - matchLength] = -matchLength;
	}

	for (off_t i = 0; i < oldSize + 1; i++)
		index[value[i]] = i;
}

static size_t matchlen(const uint8_t *old, off_t oldSize, const uint8_t *newer, off_t newSize)
{
	size_t i;

	for (i = 0; i < (size_t) oldSize && i < (size_t) newSize; i++)
	{
		if (old[i] != newer[i])
			break;
	}

	return i;
}

size_t search(off_t *index, const uint8_t *old, size_t oldSize, const uint8_t *newer, size_t newSize, size_t start, size_t end, size_t *matchPos)
{
	if (end - start < 2)
	{
		const size_t x = matchlen(&old[index[start]], oldSize - index[start], newer, newSize);
		const size_t y = matchlen(&old[index[end]], oldSize - index[end], newer, newSize);

		if (x > y)
		{
			*matchPos = (size_t) index[start];
			return x;
		}
		else
		{
			*matchPos = (size_t) index[end];
			return y;
		}
	}

	const size_t x = start + (end - start) / 2;

	if (memcmp(&old[index[x]], newer, (size_t) MIN(oldSize - index[x], newSize)) < 0)
	{
		return search(index, old, oldSize, newer, newSize, x, end, matchPos);
	}
	else
	{
		return search(index, old, oldSize, newer, newSize, start, x, matchPos);
	}
}

void offtout(uint32_t x, uint8_t * buf)
{
	buf[0] = x			& 0xffu;
	buf[1] = (x >> 8) 	& 0xffu;
	buf[2] = (x >> 16)	& 0xffu;
	buf[3] = (x >> 24)	& 0xffu;
}

uint8_t * readFile(const char * file, size_t * fileSize)
{
	int fd = open(file, O_RDONLY, 0);
	if(fd < 0)
		return NULL;

	off_t length = lseek(fd, 0, SEEK_END);
	if(length == -1)
	{
		warn("Couldn't fseek to the end of file %s", file);
		close(fd);
		return NULL;
	}
	*fileSize = (size_t) length;

	/* Allocate oldSize+1 bytes instead of oldSize bytes to ensure that we never try to malloc(0) and get a nullptr pointer */
	void * output = malloc(*fileSize + 1);
	if(output == NULL)
	{
		warn("Couldn't allocate %li bytes for file %s", *fileSize, file);
		close(fd);
		return NULL;
	}

	if(lseek(fd, 0, SEEK_SET) != 0)
	{
		warn("Couldn't fseek to the beginning of file %s", file);
		close(fd);
		return NULL;
	}

	if(read(fd, output, *fileSize) != (ssize_t) *fileSize)
	{
		warn("Couldn't read the file %s", file);
		close(fd);
		return NULL;
	}

	if(close(fd) == -1)
	{
		warn("Couldn't close the file %s", file);
		return NULL;
	}

	return (uint8_t*) output;
}
