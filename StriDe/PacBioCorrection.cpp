///-----------------------------------------------
// Copyright 2015 National Chung Cheng University
// Written by Yao-Ting Huang
// Released under the GPL
//-----------------------------------------------
//
// PacBioCorrectionProcess.cpp - Correction of PacBio reads using FM-index
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include "Util.h"
#include "PacBioCorrection.h"
#include "SuffixArray.h"
#include "BWT.h"
#include "SGACommon.h"
#include "OverlapCommon.h"
#include "Timer.h"
#include "BWTAlgorithms.h"
#include "ASQG.h"
#include "gzstream.h"
#include "SequenceProcessFramework.h"
#include "PacBioCorrectionProcess.h"
#include "CorrectionThresholds.h"
#include "BWTIntervalCache.h"

#define FORMULA( x,y,z ) ( (x) ? (0.05776992234f * y - 0.4583043394f * z + 10.19159685f) : (0.0710704607f * y - 0.5445663957f * z + 12.26253388f) )
//
// Getopt
//
#define SUBPROGRAM "PacBioCorrection"
static const char *CORRECT_VERSION_MESSAGE =
SUBPROGRAM " Version " PACKAGE_VERSION "\n"
"Written by Yao-Ting Huang & Ping-Yeh Chen.\n"
"\n"
"Copyright 2015 National Chung Cheng University\n";

static const char *CORRECT_USAGE_MESSAGE =
"Usage: " PACKAGE_NAME " " SUBPROGRAM " [OPTION] ... READSFILE\n"
"Correct PacBio reads via FM-index walk\n"
"\n"
"      --help                           Display this help and exit\n"
"      -v, --verbose                    Display verbose output\n"
"      -p, --prefix=PREFIX              Use PREFIX for the names of the index files (default: prefix of the input file)\n"
"      -o, --directory=PATH             Put results in the directory\n"
"      -t, --threads=NUM                Use NUM threads for the computation (default: 1)\n"
"      -a, --algorithm=STR              pacbioH: pacbio hybrid correction (using NGS reads to correct PB reads)\n"
"                                       pacbioS: pacbio self correction (using PB reads to correct PB reads)(default)\n"
"\nPacBio correction parameters:\n"
"      -k, --kmer-size=N                The length of the kmer to use. (default: 19 (PacBioS).)\n"
"      -s, --min-kmer-size=N            The minimum length of the kmer to use. (default: 13.)\n"
"      -x, --kmer-threshold=N           Attempt to correct kmers that are seen less than N times. (default: 3)\n"
"      -e, --error-rate=N               The error rate of PacBio reads.(default:0.15)\n"
"      -i, --idmer-length=N             The length of the kmer to identify similar reads.(default: 9)\n"
"      -L, --max-leaves=N               Number of maximum leaves in the search tree. (default: 32)\n"
"      -C, --PBcoverage=N               Coverage of PacBio reads(default: 90)\n"
"      --debugseed                      Output seeds file for each reads (default: false)\n"
"      --onlyseed                       Only search seeds file for each reads (default: false)\n"
"      --split                          Split the uncorrected reads (default: false)\n"

"\nReport bugs to " PACKAGE_BUGREPORT "\n\n";

static const char* PROGRAM_IDENT =
PACKAGE_NAME "::" SUBPROGRAM;

namespace opt
{
	static unsigned int verbose;
	static int numThreads = 1;
	static std::string prefix;
	static std::string readsFile;
	static std::string outFile;
	static std::string discardFile;
	static int sampleRate = BWT::DEFAULT_SAMPLE_RATE_SMALL;
	static int kmerLength = 19;
	static int kmerThreshold = 3;
	static int maxLeaves=32;
    static int idmerLength = 9;
    static double ErrorRate=0.15;	
	static int minKmerLength = 13;	
	static int numOfNextTarget = 1;
	static int collect = 5;
	
	static bool split = false;
	static bool isFirst = false;
	size_t maxSeedInterval = 500;
    static size_t PBcoverage = 90;// PB seed searh depth
    static bool DebugExtend = false;
    static bool DebugSeed = false;
	static bool OnlySeed = false;
	static PacBioCorrectionAlgorithm algorithm = PBC_SELF;
	static std::string directory;
}

static const char* shortopts = "p:t:o:a:k:x:L:s:d:c:C:v:e:i";

enum { OPT_HELP = 1, OPT_VERSION, OPT_DISCARD, OPT_SPLIT, OPT_FIRST,OPT_DEBUGEXTEND,OPT_DEBUGSEED,OPT_ONLYSEED };

static const struct option longopts[] = {
	{ "verbose",       no_argument,       NULL, 'v' },
	{ "threads",       required_argument, NULL, 't' },
	{ "directory",       required_argument, NULL, 'o' },
	{ "prefix",        required_argument, NULL, 'p' },
	{ "algorithm",     required_argument, NULL, 'a' },
	{ "kmer-size",     required_argument, NULL, 'k' },
	{ "kmer-threshold" ,required_argument, NULL, 'x' },
	{ "max-leaves",    required_argument, NULL, 'L' },
	{ "min-kmer-size"  ,required_argument, NULL, 's' },
    { "error-rate",    required_argument, NULL, 'e' },
    { "idmer-length",    required_argument, NULL, 'i' },
	{ "downward"       ,required_argument, NULL, 'd' },
	{ "collect"        ,required_argument, NULL, 'c' },
    { "PBcoverage",    required_argument, NULL, 'C' },
	{ "split",       	no_argument,       NULL, OPT_SPLIT },
	{ "first",       	no_argument,       NULL, OPT_FIRST },
    { "debugextend",       	no_argument,       NULL, OPT_DEBUGEXTEND },
    { "debugseed",       	no_argument,       NULL, OPT_DEBUGSEED },
	{ "onlyseed",       	no_argument,       NULL, OPT_ONLYSEED },
	{ "discard",       no_argument,       NULL, OPT_DISCARD },
	{ "help",          no_argument,       NULL, OPT_HELP },
	{ "version",       no_argument,       NULL, OPT_VERSION },
	{ NULL, 0, NULL, 0 }
};

//
// Main
//
int PacBioCorrectionMain(int argc, char** argv)
{
	parsePacBioCorrectionOptions(argc, argv);

	// Set the error correction parameters
	PacBioCorrectionParameters ecParams;
	BWT *pBWT, *pRBWT;
	SampledSuffixArray* pSSA;

	// Load indices
	#pragma omp parallel
	{
		#pragma omp single nowait
		{	//Initialization of large BWT takes some time, pass the disk to next job
			std::cout << std::endl << "Loading BWT: " << opt::prefix + BWT_EXT << "\n";
			pBWT = new BWT(opt::prefix + BWT_EXT, opt::sampleRate);
		}
		#pragma omp single nowait
		{
			std::cout << "Loading RBWT: " << opt::prefix + RBWT_EXT << "\n";
			pRBWT = new BWT(opt::prefix + RBWT_EXT, opt::sampleRate);
		}
		#pragma omp single nowait
		{
			std::cout << "Loading Sampled Suffix Array: " << opt::prefix + SAI_EXT << "\n";
			pSSA = new SampledSuffixArray(opt::prefix + SAI_EXT, SSA_FT_SAI);
		}
	}

	// Sample 100000 kmer counts into KmerDistribution from reverse BWT 
	// Don't sample from forward BWT as Illumina reads are bad at the 3' end
	// ecParams.kd = BWTAlgorithms::sampleKmerCounts(opt::kmerLength, 100000, pBWT);
	// ecParams.kd.computeKDAttributes();
	// ecParams.kd.print(100);
	// const size_t RepeatKmerFreq = ecParams.kd.getCutoffForProportion(0.95); 
	// std::cout << "Median kmer frequency: " <<ecParams.kd.getQuartile(2)() << "\t Std: " <<  ecParams.kd.getSdv() 
				// <<"\t 95% kmer frequency: " << ecParams.kd.getCutoffForProportion(0.95)
				// << "\t Repeat frequency cutoff: " << ecParams.kd.getRepeatKmerCutoff() << "\n";
	
	BWTIndexSet indexSet;
	indexSet.pBWT = pBWT;
	indexSet.pRBWT = pRBWT;
	indexSet.pSSA = pSSA;
	ecParams.indices = indexSet;

	
	// Open outfiles and start a timer
	std::ostream* pWriter = createWriter(opt::outFile);
	std::ostream* pDiscardWriter = createWriter(opt::discardFile);
	Timer* pTimer = new Timer(PROGRAM_IDENT);

	ecParams.algorithm = opt::algorithm;
	ecParams.kmerLength = opt::kmerLength;
	ecParams.maxLeaves = opt::maxLeaves;
	ecParams.minKmerLength = opt::minKmerLength;
    ecParams.idmerLength = opt::idmerLength;
    ecParams.ErrorRate = opt::ErrorRate;
	ecParams.FMWKmerThreshold = opt::kmerThreshold;
	ecParams.numOfNextTarget = opt::numOfNextTarget;
	ecParams.collectedSeeds = opt::collect;
    ecParams.PBcoverage = opt::PBcoverage;
	ecParams.isSplit = opt::split;
	ecParams.isFirst = opt::isFirst;
    ecParams.DebugExtend = opt::DebugExtend;
    ecParams.DebugSeed = opt::DebugSeed;
	ecParams.OnlySeed = opt::OnlySeed;
	ecParams.maxSeedInterval = opt::maxSeedInterval;
	ecParams.directory = opt::directory;
	
	if(ecParams.algorithm == PBC_SELF)
	{
		std::cout << std::endl << "Correcting PacBio reads for " << opt::readsFile << " using--" << std::endl
		<< "number of threads:\t" << opt::numThreads << std::endl
        << "PB reads coverage:\t" << ecParams.PBcoverage << std::endl
		<< "large kmer size:\t" << ecParams.kmerLength << std::endl 

		<< "small kmer size:\t" << ecParams.minKmerLength << std::endl
		<< "small kmer freq. cutoff:\t" << ecParams.FMWKmerThreshold << std::endl
		<< "max leaves:\t" << ecParams.maxLeaves  << std::endl
		<< "max depth:\t1.2~0.8* (length between two seeds +- 20)" << std::endl
		<< "num of next Targets:\t" << ecParams.numOfNextTarget << std::endl;
	}
	
	// Setup post-processor
	PacBioCorrectionPostProcess postProcessor(pWriter, pDiscardWriter, ecParams);

	if(opt::numThreads <= 1)
	{
		// Serial mode
		PacBioCorrectionProcess processor(ecParams);

		SequenceProcessFramework::processSequencesSerial<SequenceWorkItem,
		PacBioCorrectionResult,
		PacBioCorrectionProcess,
		PacBioCorrectionPostProcess>(opt::readsFile, &processor, &postProcessor);
	}
	else
	{
		// Parallel mode
		std::vector<PacBioCorrectionProcess*> pProcessorVector;
		for(int i = 0; i < opt::numThreads; ++i)
		{
			PacBioCorrectionProcess* pProcessor = new PacBioCorrectionProcess(ecParams);
			pProcessorVector.push_back(pProcessor);
		}

		SequenceProcessFramework::processSequencesParallel<SequenceWorkItem,
		PacBioCorrectionResult,
		PacBioCorrectionProcess,
		PacBioCorrectionPostProcess>(opt::readsFile, pProcessorVector, &postProcessor);

		// SequenceProcessFramework::processSequencesParallelOpenMP<SequenceWorkItem,
		// PacBioCorrectionResult,
		// PacBioCorrectionProcess,
		// PacBioCorrectionPostProcess>(opt::readsFile, pProcessorVector, &postProcessor);
		
		for(int i = 0; i < opt::numThreads; ++i)
		{
			delete pProcessorVector[i];
		}
	}

	delete pBWT;
	if(pRBWT != NULL)
	delete pRBWT;

	if(pSSA != NULL)
	delete pSSA;

	delete pTimer;

	delete pWriter;
	if(pDiscardWriter != NULL)
	delete pDiscardWriter;
	
	return 0;
}


//
// Handle command line arguments
//
void parsePacBioCorrectionOptions(int argc, char** argv)
{
	optind=1;	//reset getopt
	std::string algo_str;
	bool die = false;
	for (char c; (c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1;)
	{
		std::istringstream arg(optarg != NULL ? optarg : "");
		switch (c)
		{
		case 'p': arg >> opt::prefix; break;
		case 'o': arg >> opt::directory; break;
		case 't': arg >> opt::numThreads; break;
		case 'a': arg >> algo_str; break;
		case 'k': arg >> opt::kmerLength; break;
		case 'x': arg >> opt::kmerThreshold; break;
		case '?': die = true; break;
		case 'v': opt::verbose++; break;
		case 'L': arg >> opt::maxLeaves; break;
		case 's': arg >> opt::minKmerLength; break;
        case 'e': arg >> opt::ErrorRate; break;
        case 'i': arg >> opt::idmerLength; break;
		case 'd': arg >> opt::numOfNextTarget; break;
		case 'c': arg >> opt::collect; break;
        case 'C': arg >> opt::PBcoverage; break;
		case OPT_SPLIT: opt::split = true; break;
		case OPT_FIRST: opt::isFirst = true; break;
        case OPT_DEBUGEXTEND: opt::DebugExtend = true; break;
        case OPT_DEBUGSEED: opt::DebugSeed = true; break;
		case OPT_ONLYSEED: opt::OnlySeed = true; break;
		case OPT_HELP:
			std::cout << CORRECT_USAGE_MESSAGE;
			exit(EXIT_SUCCESS);
		case OPT_VERSION:
			std::cout << CORRECT_VERSION_MESSAGE;
			exit(EXIT_SUCCESS);
		}
	}

	if (argc - optind < 1)
	{
		std::cerr << SUBPROGRAM ": missing arguments\n";
		die = true;
	}
	else if (argc - optind > 1)
	{
		std::cerr << SUBPROGRAM ": too many arguments\n";
		die = true;
	}

	if(opt::numThreads <= 0)
	{
		std::cerr << SUBPROGRAM ": invalid number of threads: " << opt::numThreads << "\n";
		die = true;
	}


	if(opt::kmerLength <= 0)
	{
		std::cerr << SUBPROGRAM ": invalid kmer length: " << opt::kmerLength << ", must be greater than zero\n";
		die = true;
	}

	if(opt::kmerThreshold <= 0)
	{
		std::cerr << SUBPROGRAM ": invalid kmer threshold: " << opt::kmerThreshold << ", must be greater than zero\n";
		die = true;
	}

	// Determine the correction algorithm to use
	if(!algo_str.empty())
	{
		if(algo_str == "pacbioS")
		opt::algorithm = PBC_SELF;
		else
		{
			std::cerr << SUBPROGRAM << ": unrecognized -a,--algorithm parameter: " << algo_str << "\n";
			die = true;
		}
	}
	
	if(opt::prefix.empty())
	{
		std::cerr << SUBPROGRAM << ": no prefix: " << opt::prefix << "\n";
		die = true;
	}
	
	if(opt::directory.empty())
	{
		std::cerr << SUBPROGRAM << ": no directory: " << "\n";
		die = true;
	}
	else
	{		
		opt::directory = opt::directory + "/";
		std::string workingDir = opt::directory + (opt::DebugSeed ? "seed/stat/" : "");
		if( system(("mkdir -p " + workingDir).c_str()) != 0)
		{
			std::cerr << SUBPROGRAM << ": something wrong in directory: " << opt::directory << "\n";
			die = true;
		}
	}
	if(die)
	{
		std::cout << "\n" << CORRECT_USAGE_MESSAGE;
		exit(EXIT_FAILURE);
	}
	
	opt::readsFile = argv[optind++];
	std::string out_prefix = stripFilename(opt::readsFile);
	if(opt::algorithm == PBC_SELF)
	{
		opt::outFile = opt::directory + out_prefix + ".correct.fa";
		opt::discardFile = opt::directory + out_prefix + ".discard.fa";
	}
	else
		assert(false);

	// Parse the input filenames
	/*
	// Set the correction threshold
	if(opt::kmerThreshold <= 0)
	{
		std::cerr << "Invalid kmer support threshold: " << opt::kmerThreshold << "\n";
		exit(EXIT_FAILURE);
	}
	*/
	CorrectionThresholds::Instance().setBaseMinSupport(opt::kmerThreshold);
	
	std::string outfilename = opt::directory + "threshold-table";
	std::ofstream outfile(outfilename);
	for(int i=opt::kmerLength; i<=50; i++)
	{
		float kmerThresholdValueWithLowCoverage = FORMULA(true,opt::PBcoverage,i);
		float kmerThresholdValue = FORMULA(false,opt::PBcoverage,i);
		outfile << i << "\t";
		outfile << (kmerThresholdValue < 5 ? 5 : kmerThresholdValue) << "\t";
		outfile << (kmerThresholdValueWithLowCoverage < 5 ? 5 : kmerThresholdValueWithLowCoverage) << "\n";
	}
	outfile.close();
}
