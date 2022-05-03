
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <node_object_wrap.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include "../gf16/controller.h"
#include "../gf16/controller_cpu.h"
#include "../gf16/threadqueue.h"
#include "../hasher/hasher.h"


using namespace v8;

/*******************************************/

#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x
#define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
#define HANDLE_SCOPE HandleScope scope(isolate)
#define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HANDLE_SCOPE

# if NODE_VERSION_AT_LEAST(8, 0, 0)
#  define NEW_STRING(s) String::NewFromOneByte(isolate, (const uint8_t*)(s), NewStringType::kNormal).ToLocalChecked()
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)(e), NewStringType::kNormal).ToLocalChecked())); return; }
#  define ARG_TO_NUM(t, a) (a).As<t>()->Value()
#  define ARG_TO_OBJ(a) (a).As<Object>()
# else
#  define NEW_STRING(s) String::NewFromUtf8(isolate, s)
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, e))); return; }
#  define ARG_TO_NUM(t, a) (a)->To##t()->Value()
#  define ARG_TO_OBJ(a) (a)->ToObject()
# endif

#define RETURN_VAL(v) args.GetReturnValue().Set(v)
#define RETURN_UNDEF return;
#define RETURN_BUFFER RETURN_VAL
#define ISOLATE isolate,
#define NEW_OBJ(o) o::New(isolate)
#define BUFFER_TYPE Local<Object>
#define PERSIST_VALUE(p, v) p.Reset(isolate, v)
#define PERSIST_CLEAR(p) p.Reset()

#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define HANDLE_SCOPE HandleScope scope
#define FUNC_START HANDLE_SCOPE
#define NEW_STRING String::New
#define ARG_TO_NUM(t, a) (a)->To##t()->Value()
#define ARG_TO_OBJ(a) (a)->ToObject()
#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_BUFFER(v) RETURN_VAL(v->handle_)
#define RETURN_UNDEF RETURN_VAL( Undefined() );
#define ISOLATE
#define NEW_OBJ(o) o::New()
#define BUFFER_TYPE node::Buffer*
#define PERSIST_VALUE(p, v) p = Persistent<Value>::New(v)
#define PERSIST_CLEAR(p) p.Dispose()

#endif

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__).ToLocalChecked()
#else
#define BUFFER_NEW(...) node::Buffer::New(ISOLATE __VA_ARGS__)
#endif

#if NODE_VERSION_AT_LEAST(12, 0, 0)
# define SET_OBJ(obj, key, val) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), val).Check()
# define GET_ARR(obj, idx) (obj)->Get(isolate->GetCurrentContext(), idx).ToLocalChecked()
# define SET_OBJ_FUNC(obj, key, f) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), f->GetFunction(isolate->GetCurrentContext()).ToLocalChecked()).Check()
#else
# define SET_OBJ(obj, key, val) (obj)->Set(NEW_STRING(key), val)
# define GET_ARR(obj, idx) (obj)->Get(idx)
# define SET_OBJ_FUNC(obj, key, f) (obj)->Set(NEW_STRING(key), f->GetFunction())
#endif



#if NODE_VERSION_AT_LEAST(0, 11, 0)
// copied from node::GetCurrentEventLoop [https://github.com/nodejs/node/pull/17109]
static inline uv_loop_t* getCurrentLoop(Isolate* isolate, int) {
	/* -- don't have access to node::Environment :()
	Local<Context> context = isolate->GetCurrentContext();
	if(context.IsEmpty())
		return uv_default_loop();
	return node::Environment::GetCurrent(context)->event_loop();
	*/
	
# if NODE_VERSION_AT_LEAST(9, 3, 0) || (NODE_MAJOR_VERSION == 8 && NODE_MINOR_VERSION >= 10) || (NODE_MAJOR_VERSION == 6 && NODE_MINOR_VERSION >= 14)
	return node::GetCurrentEventLoop(isolate);
# endif
	return uv_default_loop();
}
#else
static inline uv_loop_t getCurrentLoop(int) {
	return uv_default_loop();
}
#endif


struct CallbackWrapper {
	CallbackWrapper() : hasCallback(false) {}
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate;
	// use BaseObject / AsyncWrap instead? meh
	explicit CallbackWrapper(Isolate* _isolate, const Local<Value>& callback) : isolate(_isolate) {
		attachCallback(_isolate, callback);
	}
#else
	CallbackWrapper(const Local<Value>& callback) {
		attachCallback(callback);
	}
#endif
	~CallbackWrapper() {
		PERSIST_CLEAR(value);
		detachCallback();
	};
	Persistent<Object> obj_;
	// persist copy of buffer for the duration of the job
	Persistent<Value> value;
	bool hasCallback;
	
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	void attachCallback(Isolate* _isolate, const Local<Value>& callback) {
		isolate = _isolate;
		Local<Object> obj = NEW_OBJ(Object);
		SET_OBJ(obj, "ondone", callback);
		obj_.Reset(ISOLATE obj);
		//if (env->in_domain())
		//	obj_->Set(env->domain_string(), env->domain_array()->Get(0));
		hasCallback = true;
	}
#else
	void attachCallback(const Local<Value>& callback) {
		obj_ = Persistent<Object>::New(NEW_OBJ(Object));
		obj_->Set(NEW_STRING("ondone"), callback);
		//SetActiveDomain(obj_); // never set in node_zlib.cc - perhaps domains aren't that important?
		hasCallback = true;
	}
#endif
	void detachCallback() {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		obj_.Reset();
#else
		//if (obj_.IsEmpty()) return;
		obj_.Dispose();
		obj_.Clear(); // TODO: why this line?
#endif
		hasCallback = false;
	}
	
	void attachValue(const Local<Value>& val) {
		PERSIST_VALUE(value, val);
	}
	void call(int argc, Local<Value>* argv) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		Local<Object> obj = Local<Object>::New(isolate, obj_);
# if NODE_VERSION_AT_LEAST(10, 0, 0)
		node::async_context ac;
		memset(&ac, 0, sizeof(ac));
		node::MakeCallback(isolate, obj, "ondone", argc, argv, ac);
# else
		node::MakeCallback(isolate, obj, "ondone", argc, argv);
# endif
#else
		node::MakeCallback(obj_, "ondone", argc, argv);
#endif
	}
	
	inline void call() {
		HANDLE_SCOPE;
		call(0, nullptr);
	}
	inline void call(const HandleScope&) {
		call(0, nullptr);
	}
	inline void call(std::initializer_list<Local<Value>> args) {
		std::vector<Local<Value>> argList(args);
		call(argList.size(), argList.data());
	}
};


class GfProc : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1); // necessary for node::Object::Wrap
		
		NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
		NODE_SET_PROTOTYPE_METHOD(t, "freeMem", FreeMem);
		NODE_SET_PROTOTYPE_METHOD(t, "setRecoverySlices", SetRecoverySlices);
		NODE_SET_PROTOTYPE_METHOD(t, "setCurrentSliceSize", SetCurrentSliceSize);
		NODE_SET_PROTOTYPE_METHOD(t, "setNumThreads", SetNumThreads);
		NODE_SET_PROTOTYPE_METHOD(t, "setProgressCb", SetProgressCb);
		NODE_SET_PROTOTYPE_METHOD(t, "info", GetInfo);
		NODE_SET_PROTOTYPE_METHOD(t, "add", AddSlice);
		NODE_SET_PROTOTYPE_METHOD(t, "end", EndInput);
		NODE_SET_PROTOTYPE_METHOD(t, "get", GetOutputSlice);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 1)
			RETURN_ERROR("Slice size required");
		
		size_t sliceSize = (size_t)ARG_TO_NUM(Integer, args[0]);
		if(sliceSize < 2 || sliceSize & 1)
			RETURN_ERROR("Slice size is invalid");
		
		// accept method if specified
		Galois16Methods method = args.Length() >= 2 && !args[1]->IsUndefined() && !args[1]->IsNull() ? (Galois16Methods)ARG_TO_NUM(Int32, args[1]) : GF16_AUTO;
		unsigned inputGrouping = args.Length() >= 3 && !args[2]->IsUndefined() && !args[2]->IsNull() ? ARG_TO_NUM(Uint32, args[2]) : 0;
		int stagingAreas = args.Length() >= 4 && !args[3]->IsUndefined() && !args[3]->IsNull() ? ARG_TO_NUM(Int32, args[3]) : 2;
		size_t chunkLen = args.Length() >= 5 && !args[4]->IsUndefined() && !args[4]->IsNull() ? ARG_TO_NUM(Uint32, args[4]) : 0;
		
		if(inputGrouping > 32768)
			RETURN_ERROR("Input grouping is invalid");
		if(stagingAreas < 1 || stagingAreas > 32768)
			RETURN_ERROR("Staging area count is invalid");
		
		if(inputGrouping * stagingAreas > 65536)
			RETURN_ERROR("Staging area too large");
		
		GfProc *self = new GfProc(sliceSize, stagingAreas, getCurrentLoop(ISOLATE 0));
		if(!self->init(method, inputGrouping, chunkLen)) {
			delete self;
			RETURN_ERROR("Failed to allocate memory");
		}
		self->Wrap(args.This());
	}
	
private:
	bool isRunning;
	bool isClosed;
	bool pendingDiscardOutput;
	bool hasOutput;
	CallbackWrapper progressCb;
	PAR2Proc par2;
	PAR2ProcCPU par2cpu;
	
	// disable copy constructor
	GfProc(const GfProc&);
	GfProc& operator=(const GfProc&);
	
protected:
	FUNC(Close) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot close whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() >= 1 && !args[0]->IsUndefined() && !args[1]->IsNull()) {
			if(!args[0]->IsFunction())
				RETURN_ERROR("First argument must be a callback");
			
			CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[0]));
			self->par2.deinit([cb]() {
				cb->call();
				delete cb;
			});
		} else {
			self->par2.deinit();
		}
		self->isClosed = true;
	}
	
	FUNC(FreeMem) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot free memory whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		self->par2.freeProcessingMem();
		self->hasOutput = false;
	}
	
	FUNC(SetCurrentSliceSize) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1)
			RETURN_ERROR("Argument required");
		
		size_t sliceSize = (size_t)ARG_TO_NUM(Integer, args[0]);
		if(sliceSize < 2 || sliceSize & 1)
			RETURN_ERROR("Slice size is invalid");
		
		self->hasOutput = false;
		if(!self->par2.setCurrentSliceSize(sliceSize))
			RETURN_ERROR("Failed to allocate memory");
	}
	FUNC(SetRecoverySlices) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1 || !args[0]->IsArray())
			RETURN_ERROR("List of recovery indicies required");
		
		auto argOutputs = Local<Array>::Cast(args[0]);
		int numOutputs = (int)argOutputs->Length();
		if(numOutputs > 65534)
			RETURN_ERROR("Too many recovery indicies specified");
		if(numOutputs < 1)
			RETURN_ERROR("At least one recovery index must be supplied");
		
		std::vector<uint16_t> outputs(numOutputs);
		
		for(int i=0; i<numOutputs; i++) {
			Local<Value> output = GET_ARR(argOutputs, i);
			outputs[i] = ARG_TO_NUM(Uint32, output);
			if(outputs[i] > 65534)
				RETURN_ERROR("Invalid recovery index supplied");
		}
		
		self->hasOutput = false; // probably can be retained, but we'll pretend not for consistency's sake
		if(!self->par2.setRecoverySlices(outputs))
			RETURN_ERROR("Failed to allocate memory");
	}
	
	FUNC(SetNumThreads) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot change params whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1)
			RETURN_ERROR("Integer required");
		self->par2cpu.setNumThreads(ARG_TO_NUM(Int32, args[0]));
		
		RETURN_VAL(Integer::New(ISOLATE self->par2cpu.getNumThreads()));
	}
	
	FUNC(SetProgressCb) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() >= 1) {
			if(!args[0]->IsFunction())
				RETURN_ERROR("Callback required");
			self->progressCb.attachCallback(ISOLATE args[0]);
		} else {
			self->progressCb.detachCallback();
		}
	}
	
	FUNC(GetInfo) {
		// num threads, method name
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		Local<Object> ret = NEW_OBJ(Object);
		SET_OBJ(ret, "threads", Integer::New(ISOLATE self->par2cpu.getNumThreads()));
		SET_OBJ(ret, "method_desc", NEW_STRING(self->par2cpu.getMethodName()));
		SET_OBJ(ret, "chunk_size", Integer::New(ISOLATE self->par2cpu.getChunkLen()));
		SET_OBJ(ret, "staging_count", Integer::New(ISOLATE self->par2cpu.getStagingAreas()));
		SET_OBJ(ret, "staging_size", Integer::New(ISOLATE self->par2cpu.getInputBatchSize()));
		SET_OBJ(ret, "alignment", Integer::New(ISOLATE self->par2cpu.getAlignment()));
		SET_OBJ(ret, "stride", Integer::New(ISOLATE self->par2cpu.getStride()));
		
		RETURN_VAL(ret);
	}
	
	FUNC(AddSlice) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->par2.getNumRecoverySlices())
			RETURN_ERROR("setRecoverySlices not yet called");
		
		if(args.Length() < 3)
			RETURN_ERROR("Requires 3 arguments");
		
		if(!node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Input buffer required");
		if(!args[2]->IsFunction())
			RETURN_ERROR("Callback required");
		
		int idx = ARG_TO_NUM(Int32, args[0]);
		if(idx < 0 || idx > 32767)
			RETURN_ERROR("Input index not valid");
		
		if(node::Buffer::Length(args[1]) > self->par2.getCurrentSliceSize())
			RETURN_ERROR("Input buffer too large");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[2]));
		cb->attachValue(args[1]);
		
		self->isRunning = true;
		self->hasOutput = false;
		if(self->pendingDiscardOutput) {
			self->pendingDiscardOutput = false;
			self->par2.discardOutput();
		}
		
		bool added = self->par2.addInput(
			node::Buffer::Data(args[1]), node::Buffer::Length(args[1]),
			idx, false, [ISOLATE cb, idx]() {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, idx), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(idx), buffer });
#endif
				delete cb;
			}
		);
		
		if(!added) {
			delete cb;
		}
		RETURN_VAL(Boolean::New(ISOLATE added));
	}
	
	FUNC(EndInput) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		// NOTE: it's possible to end without adding anything, so don't require !self->isRunning
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		
		if(args.Length() < 1 || !args[0]->IsFunction())
			RETURN_ERROR("Callback required");
		if(!self->par2.getNumRecoverySlices())
			RETURN_ERROR("setRecoverySlices not yet called");
		
		if(self->pendingDiscardOutput)
			self->par2.discardOutput();
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[0]));
		self->par2.endInput([cb, self]() {
			self->isRunning = false;
			self->hasOutput = true;
			self->pendingDiscardOutput = true;
			cb->call();
			delete cb;
		});
	}
	
	FUNC(GetOutputSlice) {
		FUNC_START;
		GfProc* self = node::ObjectWrap::Unwrap<GfProc>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot get output whilst running");
		if(self->isClosed)
			RETURN_ERROR("Already closed");
		if(!self->hasOutput)
			RETURN_ERROR("No finalized output to retrieve");
		
		if(args.Length() < 3)
			RETURN_ERROR("Requires 3 arguments");
		
		if(!node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Output buffer required");
		if(!args[2]->IsFunction())
			RETURN_ERROR("Callback required");
		
		if(node::Buffer::Length(args[1]) < self->par2.getCurrentSliceSize())
			RETURN_ERROR("Output buffer too small");
		int idx = ARG_TO_NUM(Int32, args[0]);
		if(idx < 0 || idx >= self->par2.getNumRecoverySlices())
			RETURN_ERROR("Recovery index is not valid");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[2]));
		cb->attachValue(args[1]);
		
		self->par2.getOutput(
			idx,
			node::Buffer::Data(args[1]),
			[ISOLATE cb, idx](bool cksumValid) {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, idx), Boolean::New(cb->isolate, cksumValid), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(idx), Boolean::New(cksumValid), buffer });
#endif
				delete cb;
			}
		);
	}
	
	explicit GfProc(size_t sliceSize, int stagingAreas, uv_loop_t* loop)
	: ObjectWrap(), isRunning(false), isClosed(false), pendingDiscardOutput(true), hasOutput(false), par2cpu(loop, stagingAreas) {
		par2.init(sliceSize, {&par2cpu}, [&](unsigned numInputs, uint16_t firstInput) {
			if(progressCb.hasCallback) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				HandleScope scope(progressCb.isolate);
				progressCb.call({ Integer::New(progressCb.isolate, numInputs), Integer::New(progressCb.isolate, firstInput) });
#else
				HandleScope scope;
				progressCb.call({ Integer::New(numInputs), Integer::New(firstInput) });
#endif
			}
		});
	}
	
	bool init(Galois16Methods method, unsigned inputGrouping, size_t chunkLen) {
		return par2cpu.init(method, inputGrouping, chunkLen);
	}
	
	~GfProc() {
		par2.deinit();
	}
};

FUNC(GfInfo) {
	FUNC_START;
	
	// get method
	Galois16Methods method = args.Length() >= 1 && !args[0]->IsUndefined() && !args[0]->IsNull() ? (Galois16Methods)ARG_TO_NUM(Int32, args[0]) : GF16_AUTO;
	
	if(method == GF16_AUTO) {
		// TODO: accept hints
		method = PAR2ProcCPU::default_method();
	}
	
	auto info = PAR2ProcCPU::info(method);
	Local<Object> ret = NEW_OBJ(Object);
	SET_OBJ(ret, "id", Integer::New(ISOLATE info.id));
	SET_OBJ(ret, "name", NEW_STRING(info.name));
	SET_OBJ(ret, "alignment", Integer::New(ISOLATE info.alignment));
	SET_OBJ(ret, "stride", Integer::New(ISOLATE info.stride));
	SET_OBJ(ret, "target_chunk", Integer::New(ISOLATE info.idealChunkSize));
	SET_OBJ(ret, "target_grouping", Integer::New(ISOLATE info.idealInputMultiple));
	
	RETURN_VAL(ret);
}


class HasherInput;
static std::vector<MessageThread*> HasherInputThreadPool;
struct input_blockHash {
	uint64_t size;
	uint64_t pos;
	int count;
	char* ptr;
};
struct input_work_data {
	IHasherInput* hasher;
	const void* buffer;
	size_t len;
	struct input_blockHash* bh;
	CallbackWrapper* cb;
	HasherInput* self;
};
class HasherInput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "end", End);
		NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 2 || !node::Buffer::HasInstance(args[1]))
			RETURN_ERROR("Requires a size and buffer");

		// grab slice size + buffer to write hashes into
		double sliceSize = 0; // double ensures enough range even if int is 32-bit
#if NODE_VERSION_AT_LEAST(8, 0, 0)
		sliceSize = args[0].As<Number>()->Value();
#else
		sliceSize = args[0]->NumberValue();
#endif
		
		HasherInput *self = new HasherInput(getCurrentLoop(ISOLATE 0));
		
		self->bh.size = (uint64_t)sliceSize;
		self->bh.pos = 0;
		self->bh.count = node::Buffer::Length(args[1]) / 20;
		self->bh.ptr = node::Buffer::Data(args[1]);
		PERSIST_VALUE(self->ifscData, args[1]);
		
		self->Wrap(args.This());
	}
	
private:
	IHasherInput* hasher;
	uv_loop_t* loop;
	int queueCount;
	
	std::unique_ptr<MessageThread> thread;
	uv_async_t threadSignal;
	ThreadMessageQueue<struct input_work_data*> hashesDone;
	
	struct input_blockHash bh;
	Persistent<Value> ifscData;
	
	// disable copy constructor
	HasherInput(const HasherInput&);
	HasherInput& operator=(const HasherInput&);
	
protected:
	FUNC(Reset) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->queueCount)
			RETURN_ERROR("Cannot reset whilst running");
		
		self->hasher->reset();
	}
	
	static void thread_func(void* req) {
		struct input_work_data* data = static_cast<struct input_work_data*>(req);
		
		
		char* src_ = (char*)data->buffer;
		size_t len = data->len;
		// feed initial part
		uint64_t blockLeft = data->bh->size - data->bh->pos;
		while(len >= blockLeft) {
			data->hasher->update(src_, blockLeft);
			src_ += blockLeft;
			len -= blockLeft;
			blockLeft = data->bh->size;
			data->bh->pos = 0;
			
			if(data->bh->count) {
				data->hasher->getBlock(data->bh->ptr, 0);
				data->bh->ptr += 20;
				data->bh->count--;
			} // else there's an overflow
		}
		if(len) data->hasher->update(src_, len);
		data->bh->pos += len;
		
		
		// signal main thread that hashing has completed
		data->self->hashesDone.push(data);
		uv_async_send(&(data->self->threadSignal));
	}
	void after_process() {
		struct input_work_data* data;
		while(hashesDone.trypop(&data)) {
			static_cast<HasherInput*>(data->self)->queueCount--;
			data->cb->call();
			delete data->cb;
			delete data;
		}
	}
	
	inline void thread_send(struct input_work_data* data) {
		if(thread == nullptr) {
			if(HasherInputThreadPool.empty())
				thread.reset(new MessageThread(thread_func));
			else {
				thread.reset(HasherInputThreadPool.back());
				HasherInputThreadPool.pop_back();
			}
		}
		thread->send(data);
	}
	
	FUNC(Update) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		// TODO: consider queueing mechanism; for now, require JS to do the queueing
		if(!self->hasher)
			RETURN_ERROR("Process already ended");
		
		if(args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !args[1]->IsFunction())
			RETURN_ERROR("Requires a buffer and callback");
		
		CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[1]));
		cb->attachValue(args[0]);
		
		self->queueCount++;
		
		struct input_work_data* data = new struct input_work_data;
		data->cb = cb;
		data->hasher = self->hasher;
		data->buffer = node::Buffer::Data(args[0]);
		data->len = node::Buffer::Length(args[0]);
		data->self = self;
		data->bh = &self->bh;
		self->thread_send(data);
	}
	
	void deinit() {
		if(!hasher) return;
		hasher->destroy();
		if(thread != nullptr)
			HasherInputThreadPool.push_back(thread.release());
		uv_close(reinterpret_cast<uv_handle_t*>(&threadSignal), nullptr);
		hasher = nullptr;
		
		PERSIST_CLEAR(ifscData);
	}
	
	FUNC(End) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->queueCount)
			RETURN_ERROR("Process currently active");
		if(!self->hasher)
			RETURN_ERROR("Process already ended");
		
		if(args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("Requires a buffer");
		if(node::Buffer::Length(args[0]) < 16)
			RETURN_ERROR("Buffer must be at least 16 bytes long");
		
		// finish block hashes
		if(self->bh.count)
			// TODO: as zero padding can be slow, consider way of doing it in separate thread to not lock this one
			self->hasher->getBlock(self->bh.ptr, self->bh.size - self->bh.pos);
		
		char* result = (char*)node::Buffer::Data(args[0]);
		self->hasher->end(result);
		
		// clean up everything
		self->deinit();
	}
	
	explicit HasherInput(uv_loop_t* _loop) : ObjectWrap(), loop(_loop), queueCount(0), thread(nullptr) {
		hasher = HasherInput_Create();
		uv_async_init(loop, &threadSignal, [](uv_async_t *handle) {
			static_cast<HasherInput*>(handle->data)->after_process();
		});
		threadSignal.data = static_cast<void*>(this);
	}
	
	~HasherInput() {
		// TODO: if active, cancel thread?
		deinit();
	}
};

FUNC(HasherInputClear) {
	FUNC_START;
	for(auto thread : HasherInputThreadPool)
		delete thread;
	HasherInputThreadPool.clear();
}

class HasherOutput;
struct output_work_data {
	MD5Multi* hasher;
	const void* const* buffer;
	size_t len;
	CallbackWrapper* cb;
	HasherOutput* self;
};
class HasherOutput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "get", Get);
		NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		if(args.Length() < 1)
			RETURN_ERROR("Number of regions required");
		unsigned regions = ARG_TO_NUM(Int32, args[0]);
		if(regions < 1 || regions > 65534)
			RETURN_ERROR("Invalid number of regions specified");
		
		HasherOutput *self = new HasherOutput(regions, getCurrentLoop(ISOLATE 0));
		self->Wrap(args.This());
	}
	
private:
	MD5Multi hasher;
	uv_loop_t* loop;
	int numRegions;
	bool isRunning;
	std::vector<const void*> buffers;
	
	// disable copy constructor
	HasherOutput(const HasherOutput&);
	HasherOutput& operator=(const HasherOutput&);
	
protected:
	FUNC(Reset) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot reset whilst running");
		
		self->hasher.reset();
	}
	
	static void do_update(uv_work_t *req) {
		struct output_work_data* data = static_cast<struct output_work_data*>(req->data);
		data->hasher->update(data->buffer, data->len);
	}
	static void after_update(uv_work_t *req, int status) {
		assert(status == 0);
		
		struct output_work_data* data = static_cast<struct output_work_data*>(req->data);
		data->self->isRunning = false;
		data->cb->call();
		delete data->cb;
		delete data;
		delete req;
	}
	
	FUNC(Update) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process already active");
		
		if(args.Length() < 1 || !args[0]->IsArray())
			RETURN_ERROR("Requires an array of buffers");
		
		
		// check array of buffers
		int numBufs = Local<Array>::Cast(args[0])->Length();
		if(numBufs != self->numRegions)
			RETURN_ERROR("Invalid number of array items given");
		
		Local<Object> oBufs = ARG_TO_OBJ(args[0]);
		size_t bufLen = 0;
		for(int i = 0; i < numBufs; i++) {
			Local<Value> buffer = GET_ARR(oBufs, i);
			if (!node::Buffer::HasInstance(buffer))
				RETURN_ERROR("All inputs must be Buffers");
			self->buffers[i] = static_cast<const void*>(node::Buffer::Data(buffer));
			
			size_t currentLen = node::Buffer::Length(buffer);
			if(i) {
				if (currentLen != bufLen)
					RETURN_ERROR("All inputs' length must be equal");
			} else {
				bufLen = currentLen;
			}
		}
		
		if(args.Length() > 1) {
			if(!args[1]->IsFunction())
				RETURN_ERROR("Second argument must be a callback");
			
			CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[1]));
			cb->attachValue(args[0]);
			
			self->isRunning = true;
			
			uv_work_t* req = new uv_work_t;
			struct output_work_data* data = new struct output_work_data;
			data->cb = cb;
			data->hasher = &(self->hasher);
			data->buffer = self->buffers.data();
			data->len = bufLen;
			data->self = self;
			req->data = data;
			uv_queue_work(self->loop, req, do_update, after_update);
		} else {
			self->hasher.update(self->buffers.data(), bufLen);
		}
	}
	
	FUNC(Get) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		if(args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("Requires a buffer");
		
		if(node::Buffer::Length(args[0]) < (unsigned)self->numRegions*16)
			RETURN_ERROR("Buffer must be large enough to hold all hashes");
		
		char* result = (char*)node::Buffer::Data(args[0]);
		self->hasher.end();
		self->hasher.get(result);
	}
	
	explicit HasherOutput(unsigned regions, uv_loop_t* _loop) : ObjectWrap(), hasher(regions), loop(_loop), numRegions(regions), isRunning(false), buffers(regions) {
		// TODO: consider multi-threaded hashing
	}
	
	~HasherOutput() {
		// TODO: if isRunning, cancel
	}
};

FUNC(SetHasherInput) {
	FUNC_START;
	
	if(args.Length() < 1)
		RETURN_ERROR("Method required");

	HasherInputMethods method = (HasherInputMethods)ARG_TO_NUM(Int32, args[0]);
	RETURN_VAL(Boolean::New(ISOLATE set_hasherInput(method)));
}
FUNC(SetHasherOutput) {
	FUNC_START;
	
	if(args.Length() < 1)
		RETURN_ERROR("Method required");

	MD5MultiLevels level = (MD5MultiLevels)ARG_TO_NUM(Int32, args[0]);
	set_hasherOutputLevel(level);
}



void parpar_gf_init(
#if NODE_VERSION_AT_LEAST(4, 0, 0)
 Local<Object> target,
 Local<Value> module,
 void* priv
#else
 Handle<Object> target
#endif
) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
	Isolate* isolate = target->GetIsolate();
#endif
	HANDLE_SCOPE;
	Local<FunctionTemplate> t = FunctionTemplate::New(ISOLATE GfProc::New);
	GfProc::AttachMethods(t);
	SET_OBJ_FUNC(target, "GfProc", t);
	
	NODE_SET_METHOD(target, "gf_info", GfInfo);
	
	t = FunctionTemplate::New(ISOLATE HasherInput::New);
	HasherInput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherInput", t);
	
	NODE_SET_METHOD(target, "hasher_clear", HasherInputClear);
	
	t = FunctionTemplate::New(ISOLATE HasherOutput::New);
	HasherOutput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherOutput", t);
	
	NODE_SET_METHOD(target, "set_HasherInput", SetHasherInput);
	NODE_SET_METHOD(target, "set_HasherOutput", SetHasherOutput);
	
	setup_hasher();
}

NODE_MODULE(parpar_gf, parpar_gf_init);
