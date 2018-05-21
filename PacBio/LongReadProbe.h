
#ifndef LONGREADPROBE_H
#define LONGREADPROBE_H

#include "SeedFeature.h"

struct ProbeParameters
{
	ProbeParameters(void) = default;
	~ProbeParameters(void) = default;
	
	ProbeParameters(
			BWTIndexSet _indices,
			std::string _directory,
			int _startKmerLen,
			int _PBcoverage,
			int _mode,
			std::array<int, 3> _offset,
			std::set<int> _pool,
			bool _DebugSeed,
			bool _Manual)
	:	indices(_indices),
		directory(_directory),
		startKmerLen(_startKmerLen),
		PBcoverage(_PBcoverage),
		mode(_mode),
		offset(_offset),
		pool(_pool),
		DebugSeed(_DebugSeed),
		Manual(_Manual){ }
		
	BWTIndexSet indices;
	std::string directory;
	
	int startKmerLen;
	int scanKmerLen = 19;
	int kmerLenUpBound = 50;
	
	int PBcoverage;
	int mode;
	int radius = 100;
	float hhRatio = 0.6;
	
	std::array<int, 3> offset;
	std::set<int> pool;
	
	bool DebugSeed;
	bool Manual;
};

namespace LongReadProbe
{
	extern ProbeParameters m_params;
	extern thread_local std::string readid;
	
	void searchSeedsWithHybridKmers(const std::string& readSeq, SeedFeature::SeedVector& seedVec);
	void getSeqAttribute(const std::string& seq, int* const type);
	SeedFeature::SeedVector removeHitchhikingSeeds(SeedFeature::SeedVector initSeedVec, int const *type);
};
#endif