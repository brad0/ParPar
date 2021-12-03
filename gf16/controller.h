#include "../src/stdint.h"
#include <vector>
#include <cstring>
#include <atomic>
#include <functional>
#include <uv.h>
#include "threadqueue.h"

#include "gf16mul.h"


// callback types
typedef std::function<void(const void*, unsigned)> PAR2ProcPrepareCb;
typedef std::function<void(const void*, unsigned, bool)> PAR2ProcOutputCb;
typedef std::function<void(unsigned, uint16_t)> PAR2ProcCompleteCb;
typedef std::function<void()> PAR2ProcFinishedCb;


class PAR2ProcStaging {
public:
	void* src;
	std::vector<uint16_t> inputNums;
	std::vector<uint16_t> procCoeffs;
	std::atomic<int> procRefs;
	bool isActive;
	
	PAR2ProcStaging() : src(nullptr), isActive(false) {}
	~PAR2ProcStaging();
};

class PAR2Proc {
private:
	uv_loop_t* loop; // is NULL when closed
	
	size_t sliceSize; // actual whole slice size
	size_t alignedSliceSize; // allocated memory for slice (>=sliceSize)
	size_t currentSliceSize; // current slice chunk size (<=sliceSize)
	size_t alignedCurrentSliceSize; // memory used for current slice chunk (<=alignedSliceSize)
	std::vector<uint16_t> outputExp; // recovery exponents
	
	int numThreads;
	std::vector<MessageThread> thWorkers; // main processing worker threads
	std::vector<void*> gfScratch; // scratch memory for each thread
	int nextThread;
	
	Galois16Mul* gf;
	size_t chunkLen; // loop tiling size
	unsigned numChunks;
	unsigned alignment;
	unsigned stride;
	unsigned inputGrouping;
	void freeGf();
	
	// staging area from which processing is performed
	std::vector<PAR2ProcStaging> staging;
	bool reallocMemInput();
	unsigned currentInputBuf, currentInputPos;
	void* memProcessing; // TODO: break this into chunks, to avoid massive single allocation
	bool processingAdd;
	int stagingActiveCount;
	
	MessageThread prepareThread;
	
	PAR2ProcCompleteCb progressCb;
	bool endSignalled;
	void processing_finished();
	PAR2ProcFinishedCb finishCb;
	
	void do_computation(int inBuf, int numInputs);
	
	// disable copy constructor
	PAR2Proc(const PAR2Proc&);
	PAR2Proc& operator=(const PAR2Proc&);
	
public:
	ThreadMessageQueue<void*> _preparedChunks;
	uv_async_t _preparedSignal;
	ThreadMessageQueue<void*> _processedChunks;
	uv_async_t _doneSignal;
	
	void _after_computation();
	void _after_prepare_chunk();
	
	explicit PAR2Proc(size_t _sliceSize, uv_loop_t* _loop, int stagingAreas=2);
	explicit inline PAR2Proc(size_t _sliceSize, int stagingAreas=2) : PAR2Proc(_sliceSize, uv_default_loop(), stagingAreas) {}
	void deinit(PAR2ProcFinishedCb cb);
	void deinit();
	~PAR2Proc();
	
	bool init(const PAR2ProcCompleteCb& _progressCb = nullptr, Galois16Methods method = GF16_AUTO, unsigned inputGrouping = 0, size_t chunkLen = 0);
	bool setCurrentSliceSize(size_t newSliceSize);
	inline size_t getCurrentSliceSize() const {
		return currentSliceSize;
	}
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL);
	inline bool setRecoverySlices(const std::vector<uint16_t>& exponents) {
		return setRecoverySlices(exponents.size(), exponents.data());
	}
	inline int getNumRecoverySlices() const {
		return outputExp.size();
	}
	void freeProcessingMem();
	
	void setNumThreads(int threads);
	inline int getNumThreads() const {
		return numThreads;
	}
	inline const void* getMethodName() const {
		return gf->info().name;
	}
	
	bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb);
	void flush();
	void endInput(const PAR2ProcFinishedCb& _finishCb);
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const;
	inline void discardOutput() {
		processingAdd = false;
	}
	
	static inline Galois16Methods default_method() {
		return Galois16Mul::default_method();
	}
	static inline Galois16MethodInfo info(Galois16Methods method) {
		return Galois16Mul::info(method);
	}
};