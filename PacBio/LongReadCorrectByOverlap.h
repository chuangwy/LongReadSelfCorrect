//----------------------------------------------
// Copyright 2016 National Chung Cheng University
// Written by Yao-Ting Huang & Ping-Yeh Chen
// Released under the GPL
//-----------------------------------------------
//
// Re-written from Jared Simpson's StringThreaderNode and StringThreader class
// The search tree represents a traversal through implicit FM-index graph
//
#ifndef OverlapTree_H
#define OverlapTree_H

#include <list>
#include "BWT.h"
#include "BWTAlgorithms.h"
#include "SAINode.h"
#include "IntervalTree.h"
#include "HashtableSearch.h"


struct FMWalkResult2
{
	std::string mergedSeq;
	int alnScore;
	double kmerFreq;
};

struct FMextendParameters
{
	public:
		FMextendParameters(BWTIndexSet indices, int idmerLength, int maxLeaves,int minKmerLength,size_t PBcoverage, double ErrorRate):
			indices(indices),
			idmerLength(idmerLength),
			maxLeaves(maxLeaves),
			minKmerLength(minKmerLength),
			PBcoverage(PBcoverage),
			ErrorRate(ErrorRate){};

		FMextendParameters(){};
		BWTIndexSet indices;
		int idmerLength;
		int maxLeaves;
		int minKmerLength;
		size_t PBcoverage;
		double ErrorRate;

};

struct debugExtInfo
{
	public:
		debugExtInfo  (
						bool isDebug = false,
						std::ostream* debug_file = NULL,
						std::string readID = "",
						int  caseNum = 0,
						int  sourceStart = 0,
						int  sourceEnd   = 0,
						int  targetStart = 0,
						int  targetEnd   = 0,
						bool isPosStrand = true
					):
						isDebug(isDebug),
						debug_file(debug_file),
						readID(readID),
						caseNum(caseNum),
						sourceStart(sourceStart),
						sourceEnd(sourceEnd),
						targetStart(targetStart),
						targetEnd(targetEnd),
						isPosStrand(isPosStrand){};

		void reverseStrand()
			{
				std::swap(sourceStart,targetStart);
				std::swap(sourceEnd  ,targetEnd  );
				isPosStrand = !isPosStrand;
			}
		void sourceReduceSize(size_t startLoc)
			{
				if (isPosStrand)
				{
					sourceStart = sourceStart + startLoc;
				}
				else
				{
					sourceEnd   = sourceEnd   - startLoc;
				}
			}

		const bool isDebug;
		std::ostream* debug_file;
		std::string   readID;
		size_t  caseNum;
		size_t  sourceStart;
		size_t  sourceEnd;
		size_t  targetStart;
		size_t  targetEnd;
		bool isPosStrand;
};

struct FMidx
{
	public:
		FMidx  (
					const std::string& s,
					const BWTInterval& fwdInterval,
					const BWTInterval& rvcInterval
				):
					SearchLetters(s),
					fwdInterval(fwdInterval),
					rvcInterval(rvcInterval),
					kmerFrequency(fwdInterval.size() + rvcInterval.size())
				{};
		FMidx   (
					const char c,
					const BWTInterval& fwdInterval,
					const BWTInterval& rvcInterval
				):
					SearchLetters(std::string(1,c)), 
					fwdInterval(fwdInterval), 
					rvcInterval(rvcInterval),
					kmerFrequency(fwdInterval.size() + rvcInterval.size())
				{};
		void setInterval(const BWTInterval& fwdInterval,const BWTInterval& rvcInterval)
			{
				this -> fwdInterval   = fwdInterval;
				this -> rvcInterval   = rvcInterval;
				this -> kmerFrequency = fwdInterval.size() + rvcInterval.size();
			}
		BWTInterval getFwdInterval()
			{
				return fwdInterval;
			}
		BWTInterval getRvcInterval()
			{
				return rvcInterval;
			}
		int getKmerFrequency()
			{
				return kmerFrequency;
			}

		const std::string SearchLetters;

	private:
		BWTInterval fwdInterval;
		BWTInterval rvcInterval;
		int kmerFrequency;

};
typedef std::vector<FMidx> extArray;

struct leafInfo
{
	public:
		leafInfo(SAIOverlapNode3* leafNode, const size_t lastLeafNum):leafNodePtr(leafNode), lastLeafID(lastLeafNum)
			{
				const std::string leafLabel = leafNode -> getFullString();
				tailLetterCount   = 0;

				for(auto reverseIdx = leafLabel.crbegin(); reverseIdx != leafLabel.crend(); ++reverseIdx)
				{
					std::string suffixLetter(1,(*reverseIdx));
					if (reverseIdx == leafLabel.crbegin())
						tailLetter = suffixLetter;
					if (tailLetter == suffixLetter)
						tailLetterCount++;
					else
						break;
				}

				kmerFrequency =   (leafNode -> fwdInterval).size() 
								+ (leafNode -> rvcInterval).size();
			}
		leafInfo(SAIOverlapNode3* currNode, const leafInfo& leaf, FMidx& extension, const size_t currLeavesNum)
			{
				const std::string& extLabel = extension.SearchLetters;

				// Set kmerFrequency
					kmerFrequency = extension.getKmerFrequency();

				// Set currNode
					// Copy the intervals
						currNode->fwdInterval = extension.getFwdInterval();
						currNode->rvcInterval = extension.getRvcInterval();
						currNode->addKmerCount( kmerFrequency );
					// currOverlapLen/queryOverlapLen always increase wrt each extension
					// in order to know the approximate real-time matched length for terminal/containment processing
						currNode -> currOverlapLen++;
						currNode -> queryOverlapLen++;
					leafNodePtr = currNode;

				// Set lastLeafID
					lastLeafID = currLeavesNum;

				// Set tailLetter and its counter
					if (leaf.tailLetter == extLabel)
					{
						tailLetter      = leaf.tailLetter;
						tailLetterCount = leaf.tailLetterCount + 1;
					}
					else
					{
						tailLetter = extLabel;
						tailLetterCount = 1;
					}
			}

		SAIOverlapNode3* leafNodePtr;
		size_t lastLeafID;
		int kmerFrequency;

		std::string tailLetter;
		size_t tailLetterCount;

};
typedef std::list<leafInfo> leafList;

class LongReadSelfCorrectByOverlap
{
	public:
		LongReadSelfCorrectByOverlap();
		LongReadSelfCorrectByOverlap(
										const std::string& sourceSeed,
										const std::string& strBetweenSrcTarget,
										const std::string& targetSeed,
										int m_disBetweenSrcTarget,
										size_t initkmersize,
										size_t maxOverlap,
										const FMextendParameters params,
										size_t m_min_SA_threshold = 3,
										const debugExtInfo debug = debugExtInfo(),
										double errorRate = 0.25,
										size_t repeatFreq = 256,
										size_t localSimilarlykmerSize = 100
									);

        ~LongReadSelfCorrectByOverlap();

		// extend all leaves one base pair during overlap computation
			int extendOverlap(FMWalkResult2& FMWResult);

		// return emptiness of leaves
			inline bool isEmpty(){return m_leaves.empty();};

		// return size of leaves
			inline size_t size(){return m_leaves.size();};

		// return size of seed
			inline size_t getSeedSize(){return m_seedSize;};

		// return size of seed
			inline size_t getCurrentLength(){return m_currentLength;};

		size_t minTotalcount = 10000000;
		size_t totalcount = 0;

		size_t SelectFreqsOfrange(const size_t LowerBound, const size_t UpperBound, leafList& newLeaves);

		std::pair<size_t,size_t> alnscore;
    private:

		//
		// Functions
		//
			void initialRootNode(const std::string& beginningkmer);
			void buildOverlapbyFMindex(IntervalTree<size_t>& fwdIntervalTree,IntervalTree<size_t>& rvcIntervalTree,const int& overlapSize);

			void extendLeaves(leafList& newLeaves);
			void attempToExtend(leafList& newLeaves,bool isSuccessToReduce);
			void updateLeaves(leafList& newLeaves,extArray& extensions,leafInfo& leaf,size_t currLeavesNum);

			void refineSAInterval(leafList& leaves, const size_t newKmerSize);

			int findTheBestPath(const SAIntervalNodeResultVector& results, FMWalkResult2& FMWResult);

			extArray getFMIndexExtensions(const leafInfo& currLeaf,const bool printDebugInfo);

			// prone the leaves without seeds in proximity
				bool PrunedBySeedSupport(leafList& newLeaves);
			//Check if need reduce kmer size
				bool isInsufficientFreqs(leafList& newLeaves);

			// Check if the leaves reach $
				bool isTerminated(SAIntervalNodeResultVector& results);

			bool isOverlapAcceptable(SAIOverlapNode3* currNode);
			bool isSupportedByNewSeed(SAIOverlapNode3* currNode, size_t smallSeedIdx, size_t largeSeedIdx);
			bool ismatchedbykmer(BWTInterval currFwdInterval,BWTInterval currRvcInterval);
			double computeErrorRate(SAIOverlapNode3* currNode);

		//
		// Data
		//
			const std::string m_sourceSeed;
			const std::string m_strBetweenSrcTarget;
			const std::string m_targetSeed;
			const int m_disBetweenSrcTarget;
			const size_t m_initkmersize;
			const size_t m_minOverlap;
			const size_t m_maxOverlap;
			const BWT* m_pBWT;
			const BWT* m_pRBWT;
			const size_t m_PBcoverage;
			size_t m_min_SA_threshold;
			double m_errorRate;
			const size_t m_maxLeaves;
			const size_t m_seedSize;
			size_t m_repeatFreq;
			size_t m_localSimilarlykmerSize;
			const double m_PacBioErrorRate;

		// debug tools
			debugExtInfo m_Debug;
			int m_step_number;

		size_t m_maxIndelSize;
		double* freqsOfKmerSize;

		// Optional parameters
			size_t m_maxfreqs;

		std::string m_query;
		size_t m_maxLength;
		size_t m_minLength;
		std::vector<BWTInterval> m_fwdTerminatedInterval;   //in rBWT
		std::vector<BWTInterval> m_rvcTerminatedInterval;   //in BWT

		leafList m_leaves;
		SAIOverlapNode3* m_pRootNode;
		SONode3PtrList m_RootNodes;

		size_t m_currentLength;
		size_t m_currentKmerSize;

		IntervalTree<size_t> m_fwdIntervalTree;
		IntervalTree<size_t> m_rvcIntervalTree;

		IntervalTree<size_t> m_fwdIntervalTree2;
		IntervalTree<size_t> m_rvcIntervalTree2;

		size_t RemainedMaxLength;

};

#endif