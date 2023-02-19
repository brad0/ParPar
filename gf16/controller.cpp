#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <cassert>
#include <algorithm>


PAR2Proc::PAR2Proc() : endSignalled(false) {
	gfmat_init();
}


bool PAR2Proc::init(size_t sliceSize, const std::vector<struct PAR2ProcBackendAlloc>& _backends, const PAR2ProcCompleteCb& _progressCb) {
	progressCb = _progressCb;
	finishCb = nullptr;
	hasAdded = false;
	
	currentSliceSize = sliceSize;
	
	// TODO: better distribution
	backends.resize(_backends.size());
	for(unsigned i=0; i<_backends.size(); i++) {
		size_t size = _backends[i].size;
		auto& backend = backends[i];
		backend.currentSliceSize = size;
		backend.currentOffset = _backends[i].offset;
		backend.be = _backends[i].be;
		backend.be->setSliceSize(size);
		
		backend.be->setProgressCb([this](int numInputs, int firstInput) {
			this->onBackendProcess(numInputs, firstInput);
		});
	}
	return checkBackendAllocation();
}

bool PAR2Proc::checkBackendAllocation() {
	// check ranges of backends (could maybe make this more optimal with a heap, but I expect few devices, so good enough for now)
	// determine if we're covering the full slice, and whether there are overlaps (overlap = dynamic scheduling)
	size_t start = backends[0].currentOffset, end = backends[0].currentOffset+backends[0].currentSliceSize;
	bool hasOverlap = false;
	std::vector<bool> beChecked(backends.size());
	int beUnchecked = backends.size()-1;
	beChecked[0] = true;
	while(beUnchecked) {
		bool beFound = false;
		for(unsigned i=1; i<backends.size(); i++) {
			if(beChecked[i]) continue;
			const auto& backend = backends[i];
			size_t currentEnd = backend.currentOffset + backend.currentSliceSize;
			if(backend.currentOffset <= start && currentEnd >= start) {
				if(currentEnd > start) hasOverlap = true;
				start = backend.currentOffset;
				if(currentEnd > end) end = currentEnd;
			}
			else if(currentEnd >= end && backend.currentOffset <= end) {
				if(backend.currentOffset < end) hasOverlap = true;
				end = currentEnd;
				if(backend.currentOffset < start) start = backend.currentOffset; // this shouldn't be possible I think
			}
			else if(backend.currentOffset > start && currentEnd < end) {
				hasOverlap = true;
			}
			else continue;
			
			// found a connecting backend
			beChecked[i] = true;
			beUnchecked--;
			beFound = true;
			
			// ensure alignment to 16-bit words
			if(backend.currentOffset & 1) return false;
			if((backend.currentSliceSize & 1) && backend.currentOffset+backend.currentSliceSize != currentSliceSize) return false;
		}
		if(!beFound) return false;
	}
	if(hasOverlap) return false; // TODO: eventually support overlapping
	return (start == 0 && end == currentSliceSize); // fail if backends don't cover the entire slice
}

// this just reduces the size without resizing backends; TODO: this should be removed
bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	if(newSliceSize > currentSliceSize) return false;
	currentSliceSize = newSliceSize;
	
	bool success = true;
	size_t pos = 0;
	for(auto& backend : backends) {
		backend.currentSliceSize = std::min(currentSliceSize-pos, backend.currentSliceSize);
		backend.currentOffset = pos;
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		pos += backend.currentSliceSize;
	}
	return success;
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize, const std::vector<std::pair<size_t, size_t>>& sizeAlloc) {
	if(backends.size() != sizeAlloc.size()) return false;
	currentSliceSize = newSliceSize;
	
	bool success = true;
	const auto* alloc = sizeAlloc.data();
	for(auto& backend : backends) {
		backend.currentSliceSize = alloc->second;
		backend.currentOffset = alloc->first;
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		alloc++;
	}
	return checkBackendAllocation();
}

bool PAR2Proc::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	// TODO: may eventually consider splitting by recovery, but for now, just pass through
	// - though we may still need a way to allocate different recovery to different backends (don't want to split slices to finely)
	bool success = true;
	for(auto& backend : backends)
		success = success && backend.be->setRecoverySlices(numSlices, exponents);
	return success;
}

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) {
	assert(!endSignalled);
	
	auto cbRef = addCbRefs.find(inputNum);
	if(cbRef != addCbRefs.end()) {
		cbRef->second.cb = cb;
	} else {
		cbRef = addCbRefs.emplace(std::make_pair(inputNum, PAR2ProcAddCbRef{
			(int)backends.size(), cb,
			[this, inputNum]() {
				auto& ref = addCbRefs[inputNum];
				if(--ref.backendsActive == 0) {
					auto cb = ref.cb;
					addCbRefs.erase(inputNum);
					if(cb) cb();
				}
			}
		})).first;
		for(auto& backend : backends) {
			size_t amount = std::min(size-backend.currentOffset, backend.currentSliceSize);
			if(backend.currentOffset >= size || amount == 0)
				cbRef->second.backendsActive--;
		}
	}
	
	// if the last add was unsuccessful, we assume that failed add is now being resent
	// TODO: consider some better system - e.g. it may be worthwhile allowing accepting backends to continue to get new buffers? or perhaps use this as an opportunity to size up the size?
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		size_t amount = std::min(size-backend.currentOffset, backend.currentSliceSize);
		if(amount == 0) continue;
		if(backend.added.find(inputNum) == backend.added.end()) {
			bool addSuccessful = backend.be->addInput(static_cast<const char*>(buffer) + backend.currentOffset, amount, inputNum, flush, cbRef->second.backendCb) != PROC_ADD_FULL;
			success = success && addSuccessful;
			if(addSuccessful) backend.added.insert(inputNum);
		}
	}
	if(success) {
		hasAdded = true;
		for(auto& backend : backends)
			backend.added.erase(inputNum);
	}
	return success;
}

bool PAR2Proc::dummyInput(size_t size, uint16_t inputNum, bool flush) {
	assert(!endSignalled);
	
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size || backend.currentSliceSize == 0) continue;
		if(backend.added.find(inputNum) == backend.added.end()) {
			bool addSuccessful = backend.be->dummyInput(inputNum, flush) != PROC_ADD_FULL;
			success = success && addSuccessful;
			if(addSuccessful) backend.added.insert(inputNum);
		}
	}
	if(success) {
		hasAdded = true;
		for(auto& backend : backends)
			backend.added.erase(inputNum);
	}
	return success;
}

bool PAR2Proc::fillInput(const void* buffer, size_t size) {
	assert(!endSignalled);
	bool finished = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size || backend.currentSliceSize == 0) continue;
		if(backend.added.find(-1) == backend.added.end()) {
			bool fillSuccessful = backend.be->fillInput(static_cast<const char*>(buffer) + backend.currentOffset);
			finished = finished && fillSuccessful;
			if(fillSuccessful) backend.added.insert(-1);
		}
	}
	return finished;
}



void PAR2Proc::flush() {
	for(auto& backend : backends)
		if(backend.currentSliceSize > 0)
			backend.be->flush();
}

void PAR2Proc::endInput(const PAR2ProcPlainCb& _finishCb) {
	assert(!endSignalled);
	flush();
	finishCb = _finishCb;
	bool allIsEmpty = true;
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		backend.be->endInput();
		allIsEmpty = allIsEmpty && backend.be->isEmpty();
	}
	endSignalled = true;
	if(allIsEmpty)
		processing_finished();
}

void PAR2Proc::getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const {
	if(!hasAdded) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		cb(true);
		return;
	}
	
	auto* cbRef = new int(backends.size());
	for(const auto& backend : backends) {
		if(backend.currentSliceSize == 0)
			(*cbRef)--;
	}
	auto* allValid = new bool(true);
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		// TODO: for overlapping regions, need to do a xor-merge pass
		backend.be->getOutput(index, static_cast<char*>(output) + backend.currentOffset, [cbRef, allValid, cb](bool valid) {
			*allValid = *allValid && valid;
			if(--(*cbRef) == 0) {
				delete cbRef;
				cb(*allValid);
				delete allValid;
			}
		});
	}
}

void PAR2Proc::onBackendProcess(int numInputs, int firstInput) {
	// since we need to invoke the callback for each backend which completes (for adds to continue), this means this isn't exactly 'progress' any more
	// TODO: consider renaming
	if(progressCb) progressCb(numInputs, firstInput);
	
	if(endSignalled) {
		bool allIsEmpty = true;
		for(auto& backend : backends)
			if(!backend.be->isEmpty()) {
				allIsEmpty = false;
				break;
			}
		if(allIsEmpty)
			processing_finished();
	}
}

void PAR2Proc::processing_finished() {
	endSignalled = false;
	
	for(auto& backend : backends)
		if(backend.currentSliceSize > 0)
			backend.be->processing_finished();
	
	if(finishCb) finishCb();
	finishCb = nullptr;
}

void PAR2Proc::deinit(PAR2ProcPlainCb cb) {
	auto* cnt = new int(backends.size());
	for(auto& backend : backends)
		backend.be->deinit([cnt, cb]() {
			if(--(*cnt) == 0) {
				delete cnt;
				cb();
			}
		});
}