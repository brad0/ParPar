
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
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		value.Reset();
#else
		value.Dispose();
#endif
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
#if NODE_VERSION_AT_LEAST(0, 11, 0)
		value.Reset(isolate, val);
#else
		value = Persistent<Value>::New(val);
#endif
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
		unsigned targetInputGrouping = args.Length() >= 3 && !args[2]->IsUndefined() && !args[2]->IsNull() ? ARG_TO_NUM(Uint32, args[2]) : 0;
		
		if(targetInputGrouping > 32768)
			RETURN_ERROR("Input grouping is invalid");
		
		GfProc *self = new GfProc(sliceSize, method, targetInputGrouping, getCurrentLoop(ISOLATE 0));
		self->Wrap(args.This());
	}
	
private:
	bool isRunning;
	bool isClosed;
	bool pendingDiscardOutput;
	bool hasOutput;
	CallbackWrapper progressCb;
	PAR2Proc par2;
	
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
		
		self->par2.setCurrentSliceSize(sliceSize);
		self->hasOutput = false;
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
		
		self->par2.setRecoverySlices(outputs);
		self->hasOutput = false; // probably can be retained, but we'll pretend not for consistency's sake
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
		self->par2.setNumThreads(ARG_TO_NUM(Int32, args[0]));
		
		RETURN_VAL(Integer::New(ISOLATE self->par2.getNumThreads()));
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
		SET_OBJ(ret, "threads", Integer::New(ISOLATE self->par2.getNumThreads()));
		SET_OBJ(ret, "method_desc", NEW_STRING(self->par2.getMethodName()));
		// TODO: return stride, maybe info on input grouping + loop tiling size
		
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
			idx, false, [ISOLATE cb](const void*, unsigned inputIndex) {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, inputIndex), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(inputIndex), buffer });
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
			[ISOLATE cb](const void*, unsigned outputIndex, bool cksumValid) {
				HANDLE_SCOPE;
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				Local<Value> buffer = Local<Value>::New(cb->isolate, cb->value);
				cb->call({ Integer::New(cb->isolate, outputIndex), Boolean::New(cb->isolate, cksumValid), buffer });
#else
				Local<Value> buffer = Local<Value>::New(cb->value);
				cb->call({ Integer::New(outputIndex), Boolean::New(cksumValid), buffer });
#endif
				delete cb;
			}
		);
	}
	
	explicit GfProc(size_t sliceSize, Galois16Methods method, unsigned targetInputGrouping, uv_loop_t* loop)
	: ObjectWrap(), isRunning(false), isClosed(false), pendingDiscardOutput(true), hasOutput(false), par2(sliceSize, loop) {
		par2.init([&](unsigned numInputs, uint16_t firstInput) {
			if(progressCb.hasCallback) {
#if NODE_VERSION_AT_LEAST(0, 11, 0)
				HandleScope scope(progressCb.isolate);
				progressCb.call({ Integer::New(progressCb.isolate, numInputs), Integer::New(progressCb.isolate, firstInput) });
#else
				HandleScope scope;
				progressCb.call({ Integer::New(numInputs), Integer::New(firstInput) });
#endif
			}
		}, method, targetInputGrouping);
	}
	
	~GfProc() {
		par2.deinit();
	}
};


struct work_data {
	void* hasher;
	const void* buffer;
	size_t len;
	CallbackWrapper* cb;
	void* self;
};
class HasherInput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "getBlock", GetBlock);
		NODE_SET_PROTOTYPE_METHOD(t, "end", End);
		NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
	}
	
	FUNC(New) {
		FUNC_START;
		if(!args.IsConstructCall())
			RETURN_ERROR("Class must be constructed with 'new'");
		
		HasherInput *self = new HasherInput(getCurrentLoop(ISOLATE 0));
		self->Wrap(args.This());
	}
	
private:
	IHasherInput* hasher;
	uv_loop_t* loop;
	bool isRunning;
	
	// disable copy constructor
	HasherInput(const HasherInput&);
	HasherInput& operator=(const HasherInput&);
	
protected:
	FUNC(Reset) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Cannot reset whilst running");
		
		self->hasher->reset();
	}
	
	static void do_update(uv_work_t *req) {
		struct work_data* data = static_cast<struct work_data*>(req->data);
		static_cast<IHasherInput*>(data->hasher)->update(data->buffer, data->len);
	}
	static void after_update(uv_work_t *req, int status) {
		assert(status == 0);
		
		struct work_data* data = static_cast<struct work_data*>(req->data);
		static_cast<HasherInput*>(data->self)->isRunning = false;
		data->cb->call();
		delete data->cb;
		delete data;
		delete req;
	}
	
	FUNC(Update) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		// TODO: consider queueing mechanism; for now, require JS to do the queueing
		if(self->isRunning)
			RETURN_ERROR("Process already active");
		
		if(args.Length() < 1 || !node::Buffer::HasInstance(args[0]))
			RETURN_ERROR("Requires a buffer");
		
		if(args.Length() > 1) {
			if(!args[1]->IsFunction())
				RETURN_ERROR("Second argument must be a callback");
			
			CallbackWrapper* cb = new CallbackWrapper(ISOLATE Local<Function>::Cast(args[1]));
			cb->attachValue(args[0]);
			
			self->isRunning = true;
			
			uv_work_t* req = new uv_work_t;
			struct work_data* data = new struct work_data;
			data->cb = cb;
			data->hasher = self->hasher;
			data->buffer = node::Buffer::Data(args[0]);
			data->len = node::Buffer::Length(args[0]);
			data->self = self;
			req->data = data;
			uv_queue_work(self->loop, req, do_update, after_update);
		} else {
			self->hasher->update(node::Buffer::Data(args[0]), node::Buffer::Length(args[0]));
		}
	}
	
	FUNC(GetBlock) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		double zeroPad = 0; // double ensures enough range even if int is 32-bit
		if(args.Length() >= 1)
#if NODE_VERSION_AT_LEAST(8, 0, 0)
			zeroPad = args[0].As<Number>()->Value();
#else
			zeroPad = args[0]->NumberValue();
#endif
		
		// return MD5+CRC32 concatenated (in same format as written directly to the IFSC packet)
		// TODO: perhaps get the hasher to directly return data in that form
		BUFFER_TYPE hash = BUFFER_NEW(20); // TODO: perhaps consider accepting buffer to directly write into, since we should already have one, rather than always instantiate a new one
		char* result = (char*)node::Buffer::Data(hash);
		uint32_t crc;
		self->hasher->getBlock(result, &crc, (uint64_t)zeroPad);
		result[16] = crc & 0xff;
		result[17] = (crc >> 8) & 0xff;
		result[18] = (crc >> 16) & 0xff;
		result[19] = (crc >> 24) & 0xff;
		RETURN_BUFFER(hash);
	}
	
	FUNC(End) {
		FUNC_START;
		HasherInput* self = node::ObjectWrap::Unwrap<HasherInput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		BUFFER_TYPE md5 = BUFFER_NEW(16);
		char* result = (char*)node::Buffer::Data(md5);
		self->hasher->end(result);
		RETURN_BUFFER(md5);
	}
	
	explicit HasherInput(uv_loop_t* _loop) : ObjectWrap(), loop(_loop), isRunning(false) {
		hasher = HasherInput_Create();
	}
	
	~HasherInput() {
		// TODO: if active, cancel task?
		hasher->destroy();
	}
};

class HasherOutput : public node::ObjectWrap {
public:
	static inline void AttachMethods(Local<FunctionTemplate>& t) {
		t->InstanceTemplate()->SetInternalFieldCount(1);
		
		NODE_SET_PROTOTYPE_METHOD(t, "update", Update);
		NODE_SET_PROTOTYPE_METHOD(t, "get", GetHash);
		NODE_SET_PROTOTYPE_METHOD(t, "end", End);
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
		struct work_data* data = static_cast<struct work_data*>(req->data);
		static_cast<MD5Multi*>(data->hasher)->update(static_cast<const void* const*>(data->buffer), data->len);
	}
	static void after_update(uv_work_t *req, int status) {
		assert(status == 0);
		
		struct work_data* data = static_cast<struct work_data*>(req->data);
		static_cast<HasherOutput*>(data->self)->isRunning = false;
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
		size_t bufLen;
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
			struct work_data* data = new struct work_data;
			data->cb = cb;
			data->hasher = &(self->hasher);
			data->buffer = static_cast<const void*>(self->buffers.data());
			data->len = bufLen;
			data->self = self;
			req->data = data;
			uv_queue_work(self->loop, req, do_update, after_update);
		} else {
			self->hasher.update(self->buffers.data(), bufLen);
		}
	}
	
	FUNC(GetHash) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		if(args.Length() < 1)
			RETURN_ERROR("Region required");
		int idx = ARG_TO_NUM(Int32, args[0]);
		if(idx < 0 || idx >= self->numRegions)
			RETURN_ERROR("Invalid region specified");
		
		BUFFER_TYPE md5 = BUFFER_NEW(16);
		char* result = (char*)node::Buffer::Data(md5);
		self->hasher.get(idx, result);
		RETURN_BUFFER(md5);
	}
	
	FUNC(End) {
		FUNC_START;
		HasherOutput* self = node::ObjectWrap::Unwrap<HasherOutput>(args.This());
		if(self->isRunning)
			RETURN_ERROR("Process currently active");
		
		self->hasher.end();
	}
	
	explicit HasherOutput(unsigned regions, uv_loop_t* _loop) : ObjectWrap(), hasher(regions), loop(_loop), numRegions(regions), isRunning(false), buffers(regions) {
		// TODO: consider multi-threaded hashing
	}
	
	~HasherOutput() {
		// TODO: if isRunning, cancel
	}
};



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
	
	t = FunctionTemplate::New(ISOLATE HasherInput::New);
	HasherInput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherInput", t);
	
	t = FunctionTemplate::New(ISOLATE HasherOutput::New);
	HasherOutput::AttachMethods(t);
	SET_OBJ_FUNC(target, "HasherOutput", t);
	
	setup_hasher();
}

NODE_MODULE(parpar_gf, parpar_gf_init);
