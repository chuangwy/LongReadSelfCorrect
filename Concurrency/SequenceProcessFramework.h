//-----------------------------------------------
// Copyright 2010 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// SequenceProcessFramework - Generic framework for performing
// some operations on input data produced by a generator,
// serially or in parallel.
//
#include "ThreadWorker.h"
#include "Timer.h"
#include "SequenceWorkItem.h"
#include "config.h"

#if HAVE_OPENMP
#include <omp.h>
#endif

#ifndef SEQUENCEPROCESSFRAMEWORK_H
#define SEQUENCEPROCESSFRAMEWORK_H

namespace SequenceProcessFramework
{

const size_t BUFFER_SIZE = 500;

// Generic function to process n work items from a file.
// With the default value of -1, n becomes the largest value representable for
// a size_t and all values will be read
template<class Input, class Output, class Generator, class Processor, class PostProcessor>
size_t processWorkSerial(Generator& generator, Processor* pProcessor, PostProcessor* pPostProcessor, size_t n = -1)
{
    Timer timer("SequenceProcess", true);
    Input workItem;

    // Generate work items using the generic generation class while the number
    // of sequences consumed from the SeqReader is less than n and there
    // are still sequences to consume from the reader
    while(generator.getNumConsumed() < n && generator.generate(workItem))
    {

		Output output = pProcessor->process(workItem);

        pPostProcessor->process(workItem, output);
        if(generator.getNumConsumed() % 50000 == 0)
            fprintf(stderr, "Processed %zu sequences (%lfs elapsed)\n", generator.getNumConsumed(), timer.getElapsedWallTime());
    }

    assert(n == (size_t)-1 || generator.getNumConsumed() == n);

    //
    double proc_time_secs = timer.getElapsedWallTime();
    fprintf(stderr, "Processed %zu sequences in %lfs (%lf sequences/s)\n",
            generator.getNumConsumed(), proc_time_secs, (double)generator.getNumConsumed() / proc_time_secs);

    return generator.getNumConsumed();
}


// Wrapper function for performing operations over every sequence read in readsFile
template<class Input, class Output, class Processor, class PostProcessor>
size_t processSequencesSerial(const std::string& readsFile, Processor* pProcessor, PostProcessor* pPostProcessor)
{
    SeqReader reader(readsFile);
    WorkItemGenerator<Input> generator(&reader);
    return processWorkSerial<Input,
                             Output,
                             WorkItemGenerator<Input>,
                             Processor,
                             PostProcessor>(generator, pProcessor, pPostProcessor);
}


// Design:
// This function is a generic function to read some INPUT from a
// generic generator object, then perform work on them.
// The actual processing is done by the Processor class
// that is passed in. The number of threads
// created is determined by the size of the vector of processors -
// one thread per processor.
//
// The function buffers batches of input data.
// Once the buffers are full, the reads are dispatched to the thread
// which run the actual processing independently. An optional post processor
// can be specified to process the results that the threads return. If the n
// parameter is used, at most n sequences will be read from the file.
//
// This version is based on pthreads.
template<class Input, class Output, class Generator, class Processor, class PostProcessor>
size_t processWorkParallelPthread(Generator& generator,
                                  std::vector<Processor*>& pProcessorVec,
                                  PostProcessor* pPostProcessor,
                                  size_t n = -1)
{
    Timer timer("SequenceProcess", true);

    // Helpful typedefs
    typedef ThreadWorker<Input, Output, Processor> Thread;
    typedef std::vector<Thread*> ThreadPtrVector;

    typedef std::vector<Input> InputVector;
    typedef std::vector<InputVector*> InputBufferVector;

    typedef std::vector<Output> OutputVector;
    typedef std::vector<OutputVector*> OutputBufferVector;
    typedef std::vector<sem_t*> SemaphorePtrVector;


    // Initialize threads, one thread per processor that was passed in
    int numThreads = pProcessorVec.size();

    ThreadPtrVector threadVec(numThreads);
    InputBufferVector inputBuffers(numThreads);
    OutputBufferVector outputBuffers(numThreads);
    SemaphorePtrVector semVec(numThreads);

    // Create the threads
    for(int i = 0; i < numThreads; ++i)
    {
        semVec[i] = new sem_t;
        int ret = sem_init( semVec[i], PTHREAD_PROCESS_PRIVATE, 0 );
        if(ret != 0)
        {
            std::cerr << "Semaphore initialization failed with error " << ret << "\n";
            std::cerr << "You are probably running on OSX which does not provide unnamed semaphores\n";
            exit(EXIT_FAILURE);
        }

        // Create and start the thread
        threadVec[i] = new Thread(semVec[i], pProcessorVec[i], BUFFER_SIZE);
        threadVec[i]->start();

        inputBuffers[i] = new InputVector;
        inputBuffers[i]->reserve(BUFFER_SIZE);

        outputBuffers[i] = new OutputVector;
        outputBuffers[i]->reserve(BUFFER_SIZE);
    }

    size_t numWorkItemsRead = 0;
    size_t numWorkItemsWrote = 0;
    bool done = false;
    int next_thread = 0;
    int num_buffers_full = 0;

    while(!done)
    {
        // Parse reads from the stream and add them into the incoming buffers averagely
        Input workItem;
        bool valid = generator.generate(workItem);
        if(valid)
        {
            
           inputBuffers[next_thread]->push_back(workItem);
           numWorkItemsRead += 1;
		
			next_thread = (next_thread+1)% numThreads; 	
			if(inputBuffers.back()->size() == BUFFER_SIZE)
				num_buffers_full = numThreads;
		
        }
        
		done = !valid || generator.getNumConsumed() == n;

        // Once all buffers are full or the input is finished, dispatch the reads to the threads
        // by swapping work buffers.
        if(num_buffers_full == numThreads || done)
        {
            int numLoops = 0;
            do
            {
                // Wait for all threads to be ready to receive
                for(int i = 0; i < numThreads; ++i)
                {
                    sem_wait(semVec[i]);
                    Thread* pThread = threadVec[i];
                    pThread->swapBuffers(*inputBuffers[i], *outputBuffers[i]);
                }
                num_buffers_full = 0;
                next_thread = 0;

                // Process the results and clear the buffers
                for(int i = 0; i < numThreads; ++i)
                {
                    assert(inputBuffers[i]->size() == outputBuffers[i]->size());
                    for(size_t j = 0; j < inputBuffers[i]->size(); ++j)
                    {
                        pPostProcessor->process((*inputBuffers[i])[j], (*outputBuffers[i])[j]);
                        ++numWorkItemsWrote;
                    }

                    inputBuffers[i]->clear();
                    outputBuffers[i]->clear();
                }

                double proc_time_secs = timer.getElapsedWallTime();
                if(generator.getNumConsumed() % (10 * BUFFER_SIZE * numThreads) == 0)
                    fprintf(stderr, "Processed %zu sequences in %lfs (%lf sequences/s)\n", generator.getNumConsumed(), proc_time_secs, (double)generator.getNumConsumed() / proc_time_secs);

                // This should never loop more than twice
                assert(numLoops < 2);
                ++numLoops;
            } while(done && numWorkItemsWrote < numWorkItemsRead);
        }
    }

    // Cleanup
    for(int i = 0; i < numThreads; ++i)
    {
        threadVec[i]->stop(); // Blocks until the thread joins
        delete threadVec[i];

        sem_destroy(semVec[i]);
        delete semVec[i];

        assert(inputBuffers[i]->empty());
        delete inputBuffers[i];

        assert(outputBuffers[i]->empty());
        delete outputBuffers[i];
    }
    assert(n == (size_t)-1 || generator.getNumConsumed() == n);
    assert(numWorkItemsRead == numWorkItemsWrote);

    double proc_time_secs = timer.getElapsedWallTime();
    fprintf(stderr, "Processed %zu sequences in %lfs (%lf sequences/s)\n",
            generator.getNumConsumed(), proc_time_secs, (double)generator.getNumConsumed() / proc_time_secs);
    return generator.getNumConsumed();
}

// Design:
// This function is a generic function to read some INPUT from a
// generic generator object, then perform work on them.
// The actual processing is done by the Processor class
// that is passed in. The number of threads
// created is determined by the size of the vector of processors -
// one thread per processor.
//
// The function buffers batches of input data.
// Once the buffers are full, the reads are dispatched to the thread
// which run the actual processing independently. An optional post processor
// can be specified to process the results that the threads return. If the n
// parameter is used, at most n sequences will be read from the file.
//
// This version is based on OpenMP.
template<class Input, class Output, class Generator, class Processor, class PostProcessor>
size_t processWorkParallelOpenMP(Generator& generator,
                                 std::vector<Processor*>& pProcessorVec,
                                 PostProcessor* pPostProcessor,
                                 size_t n = -1)
{
#if HAVE_OPENMP
    Timer timer("SequenceProcess", true);

    // Helpful typedefs
    typedef std::vector<Input> InputVector;
    typedef std::vector<Output> OutputVector;

    InputVector inputBuffer;
    OutputVector outputBuffer;

    size_t numWorkItemsRead = 0;
    size_t numWorkItemsWrote = 0;
    size_t numThreads = pProcessorVec.size();

    omp_set_num_threads(numThreads);

    bool done = false;
    while(!done)
    {
        // Parse reads from the stream and add them into the incoming buffers
        Input workItem;
        bool valid = generator.generate(workItem);
        if(valid)
        {
            inputBuffer.push_back(workItem);
            numWorkItemsRead += 1;
        }

        done = !valid || generator.getNumConsumed() == n;

        // Once all buffers are full or the input is finished, dispatch the work to the threads
        if(inputBuffer.size() == (numThreads * 64) || done)
        {
            outputBuffer.resize(inputBuffer.size());

            //
            #pragma omp parallel for schedule(dynamic, 8)
            for(int i = 0; i < (int)inputBuffer.size(); ++i)
            {
                // Dispatch the work to a processor and write the output to the output buffer
                size_t tid = omp_get_thread_num();
                outputBuffer[i] = pProcessorVec[tid]->process(inputBuffer[i]);
            }
			
			// Process the output with a single thread
            for(size_t i = 0; i < inputBuffer.size(); ++i)
            {
                pPostProcessor->process(inputBuffer[i], outputBuffer[i]);
                numWorkItemsWrote += 1;
            }
            inputBuffer.clear();
            outputBuffer.clear();

            double proc_time_secs = timer.getElapsedWallTime();
            if(generator.getNumConsumed() % (numThreads * 64) == 0)
                fprintf(stderr, "Processed %zu sequences in %lfs (%lf sequences/s)\n", generator.getNumConsumed(), proc_time_secs, (double)generator.getNumConsumed() / proc_time_secs);
        }
    }

    assert(n == (size_t)-1 || generator.getNumConsumed() == n);
    assert(numWorkItemsRead == numWorkItemsWrote);

    double proc_time_secs = timer.getElapsedWallTime();
    fprintf(stderr, "Processed %zu sequences in %lfs (%lf sequences/s)\n",
            generator.getNumConsumed(), proc_time_secs, (double)generator.getNumConsumed() / proc_time_secs);
    
	#pragma omp barrier
	return generator.getNumConsumed();

	#else // NO OPENMP
    (void)generator;
    (void)pProcessorVec;
    (void)pPostProcessor;
    (void)n;
    printf("Error: threading enabled but you did not compile with OpenMP\n");
    exit(EXIT_FAILURE);
#endif
}


// Wrapper function for operating over a file of sequences
template<class Input, class Output, class Processor, class PostProcessor>
size_t processSequencesParallel(const std::string& readsFile, std::vector<Processor*>& pProcessorVec, PostProcessor* pPostProcessor)
{
    SeqReader reader(readsFile);
	WorkItemGenerator<Input> generator(&reader);
	return processWorkParallelPthread<Input,
                                      Output,
                                      WorkItemGenerator<Input>,
                                      Processor,
                                      PostProcessor>(generator, pProcessorVec, pPostProcessor);
}

// Wrapper function for operating over a file of sequences
template<class Input, class Output, class Processor, class PostProcessor>
size_t processSequencesParallelOpenMP(const std::string& readsFile, std::vector<Processor*>& pProcessorVec, PostProcessor* pPostProcessor)
{
    SeqReader reader(readsFile);
	WorkItemGenerator<Input> generator(&reader);
	return processWorkParallelOpenMP<Input,
                                      Output,
                                      WorkItemGenerator<Input>,
                                      Processor,
                                      PostProcessor>(generator, pProcessorVec, pPostProcessor);
}

//Wrapper function to operate on single/multi threads.
//Processor & PostProcessor should only accept Parameter as single argument
//Noted by KuanWeiLee. 2018/4/30
template<class Input, class Output, class Processor, class PostProcessor, class Parameter>
void processSequences(int thread, const std::string& readsFile, const Parameter& params, bool pthread = true)
{
	assert(thread > 0);
	PostProcessor* pPostProcessor = new PostProcessor(params);
	if(thread == 1)
	{
		Processor* pProcessor = new Processor(params);
		processSequencesSerial<Input, Output, Processor, PostProcessor>(readsFile, pProcessor, pPostProcessor);
		delete pProcessor;
	}
	else
	{
		std::vector<Processor*> pProcessorVec;
		for(int i = 0; i < thread; i++)
			pProcessorVec.push_back(new Processor(params));
		if(pthread)
			processSequencesParallel<Input, Output, Processor, PostProcessor>(readsFile, pProcessorVec, pPostProcessor);
		else
			processSequencesParallelOpenMP<Input, Output, Processor, PostProcessor>(readsFile, pProcessorVec, pPostProcessor);
		for(auto& iter : pProcessorVec)
			delete iter;
	}
	delete pPostProcessor;
}

};

#endif
