#include "SeedFeature.h"
#include "BWTAlgorithms.h"
#include "Util.h"

std::map<std::string, SeedFeature::SeedVector>& SeedFeature::Log()
{
	static std::map<std::string, SeedVector> log;
	return log;
}

std::ostream& operator<<(std::ostream& out, const SeedFeature::SeedVector& vec)
{
	for(const auto& iter : vec)
		out
		<< iter.seedStr << '\t'
		<< iter.maxFixedMerFreq << '\t' 
		<< iter.seedStartPos << '\t'
		<< (iter.isRepeat ? "Yes" : "No") << '\n';
	return out;
}

SeedFeature::SeedFeature(
		std::string str,
		int startPos,
		int frequency,
		bool repeat,
		int kmerSize,
		int PBcoverage)
:	seedStr(str),
	seedLen(seedStr.length()),
	seedStartPos(startPos),
	seedEndPos(startPos + seedLen - 1),
	maxFixedMerFreq(frequency),
	isRepeat(repeat),
	isHitchhiked(false),
	startBestKmerSize(kmerSize),
	endBestKmerSize(kmerSize),
	sizeUpperBound(seedLen),
	sizeLowerBound(kmerSize),
	freqUpperBound(PBcoverage >> 1),
	freqLowerBound(PBcoverage >> 2){ }

void SeedFeature::estimateBestKmerSize(const BWTIndexSet& indices)
{
	modifyKmerSize(indices, true);
	modifyKmerSize(indices, false);
}
//pole(true/false) ? start : end
//bit(1/-1) > 0 ? increase : decrease
void SeedFeature::modifyKmerSize(const BWTIndexSet& indices, bool pole)
{
	int& kmerSize = pole ? startBestKmerSize : endBestKmerSize;
	int& kmerFreq = pole ? startKmerFreq : endKmerFreq;
	const BWT* const pSelBWT = pole ? indices.pRBWT : indices.pBWT;
	std::string seed = pole ?  reverse(seedStr) : seedStr;
	kmerFreq = BWTAlgorithms::countSequenceOccurrences(seed.substr(seedLen - kmerSize), pSelBWT);
	int bit;
	if(kmerFreq > freqUpperBound)
		bit = 1;
	else if (kmerFreq < freqLowerBound)
		bit = -1;
	else
		return;
	const int freqBound     = bit > 0 ? freqUpperBound : freqLowerBound;
	const int corsFreqBound = bit > 0 ? freqLowerBound : freqUpperBound;
	const int sizeBound = bit > 0 ? sizeUpperBound : sizeLowerBound;
	
	while((bit^kmerFreq) > (bit^freqBound) && (bit^kmerSize) < (bit^sizeBound))
	{
		kmerSize += bit;
		kmerFreq = BWTAlgorithms::countSequenceOccurrences(seed.substr(seedLen - kmerSize), pSelBWT);
	}
	if((bit^kmerFreq) < (bit^corsFreqBound))
	{
		kmerSize -= bit;
		kmerFreq = BWTAlgorithms::countSequenceOccurrences(seed.substr(seedLen - kmerSize), pSelBWT);
	}
}

//Legacy part
/***********/
SeedFeature::SeedFeature(
		size_t startPos,
		std::string str,
		bool repeat,
		size_t staticKmerSize,
		size_t repeatCutoff,
		size_t maxFixedMerFreq)
:	seedStr(str),
	seedStartPos(startPos),
	maxFixedMerFreq(maxFixedMerFreq),
	isRepeat(repeat),
	isHitchhiked(false),
	minKmerSize(staticKmerSize),
	freqUpperBound(repeatCutoff),
	freqLowerBound(repeatCutoff>>1)
{
	seedLen = seedStr.length();
	seedEndPos = seedStartPos + seedLen -1;
	startBestKmerSize = endBestKmerSize = staticKmerSize;
}
/***********/

