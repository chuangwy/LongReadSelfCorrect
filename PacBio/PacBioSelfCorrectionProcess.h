//----------------------------------------------
// Copyright 2015 National Chung Cheng University
// Written by Yao-Ting Huang
// Released under the GPL
//-----------------------------------------------
//
// PacBioSelfCorrectionProcess - Self-correction using FM-index walk for PacBio reads
//

#ifndef PACBIOSELFCORRECTIONPROCESS_H
#define PACBIOSELFCORRECTIONPROCESS_H

#include <set>
#include <cstdio>
#include "Util.h"
#include "SequenceWorkItem.h"
#include "BWTIndexSet.h"
#include "SampledSuffixArray.h"
#include "BWTAlgorithms.h"
#include "SeedFeature.h"
#include "LongReadCorrectByOverlap.h"

// Parameter object for the error corrector
struct PacBioSelfCorrectionParameters
{
	BWTIndexSet indices;
	std::string directory;
	
	// PACBIO
	int PBcoverage;
    double ErrorRate;

	int startKmerLen;
	
	// tree search parameters
//	int minOverlap;
//	int maxOverlap;
	int nextTarget;
	int maxLeaves;
    int idmerLen;
	int minKmerLen;
	
	std::set<int> pool;
    
	bool Split;
    bool DebugExtend;
    bool DebugSeed;
	bool OnlySeed;
	bool NoDp;
	
	FMextendParameters FM_params;

};




struct PacBioSelfCorrectionResult
{
	PacBioSelfCorrectionResult()
	:	merge(false),
		totalReadsLen(0),
		correctedLen(0),
		totalSeedNum(0),
		totalWalkNum(0),
		highErrorNum(0),
		exceedDepthNum(0),
		exceedLeaveNum(0),
		FMNum(0),
		DPNum(0),
		seedDis(0),
		Timer_Seed(0),
		Timer_FM(0),
		Timer_DP(0){ }

	std::string readid;
	bool merge;
	
	// PacBio reads correction by Ya, v20151001.
	std::vector<DNAString> correctedStrs;
	int64_t totalReadsLen;
	int64_t correctedLen;
	int64_t totalSeedNum;
	int64_t totalWalkNum;
	int64_t highErrorNum;
	int64_t exceedDepthNum;
	int64_t exceedLeaveNum;
	int64_t FMNum;
    int64_t DPNum;
	int64_t seedDis;
    double Timer_Seed;
    double Timer_FM;
    double Timer_DP;
};

//
class PacBioSelfCorrectionProcess
{
	public:
	
		PacBioSelfCorrectionProcess(const PacBioSelfCorrectionParameters& params):m_params(params){ }
		~PacBioSelfCorrectionProcess(){ }
		PacBioSelfCorrectionResult process(const SequenceWorkItem& workItem);

	private:
		const PacBioSelfCorrectionParameters m_params;
	
		//correct sequence
		void initCorrect(std::string& readSeq, const SeedFeature::SeedVector& seedVec, SeedFeature::SeedVector& pieceVec, PacBioSelfCorrectionResult& result);
		int correctByFMExtension(const SeedFeature& source, const SeedFeature& target, const std::string& in, std::string& out, PacBioSelfCorrectionResult& result, debugExtInfo& debug);
		bool correctByMSAlignment(const SeedFeature& source, const SeedFeature& target, const std::string& in, std::string& out, PacBioSelfCorrectionResult& result);
};

//postprocess
class PacBioSelfCorrectionPostProcess
{
	public:
		PacBioSelfCorrectionPostProcess(const PacBioSelfCorrectionParameters& params);
		~PacBioSelfCorrectionPostProcess();
		void process(const SequenceWorkItem& item, const PacBioSelfCorrectionResult& result);
	
	private:
		PacBioSelfCorrectionParameters m_params;
		
		std::ostream* m_pCorrectWriter;
		std::ostream* m_pDiscardWriter;
	
		int64_t m_totalReadsLen;
		int64_t m_correctedLen;
		int64_t m_totalSeedNum;
		int64_t m_totalWalkNum;
		int64_t m_highErrorNum;
		int64_t m_exceedDepthNum;
		int64_t m_exceedLeaveNum;
		int64_t m_FMNum;
		int64_t m_DPNum;
		int64_t m_OutcastNum;
		int64_t m_seedDis;
		double m_Timer_Seed;
		double m_Timer_FM;
	    double m_Timer_DP;
		
		FILE* m_pStatusWriter;
		int m_status[3];
		
		void summarize(FILE* out, const int* status, std::string subject);
};


#endif
