#include "test_protocol_common.hpp"
#include "solid/system/exception.hpp"

using namespace solid;


namespace{

struct InitStub{
	size_t		size;
	ulong		flags;
};

InitStub initarray[] = {
	{100000, 0|frame::ipc::MessageFlags::Synchronous},
	{16384000, 0|frame::ipc::MessageFlags::Synchronous},
	{8192000, 0|frame::ipc::MessageFlags::Synchronous},
	{4096000, 0|frame::ipc::MessageFlags::Synchronous},
	{2048000, 0|frame::ipc::MessageFlags::Synchronous},
	{1024000, 0|frame::ipc::MessageFlags::Synchronous},
	{512000, 0|frame::ipc::MessageFlags::Synchronous},
	{256000, 0|frame::ipc::MessageFlags::Synchronous},
	{128000, 0|frame::ipc::MessageFlags::Synchronous},
	{64000, 0|frame::ipc::MessageFlags::Synchronous},
	{32000, 0|frame::ipc::MessageFlags::Synchronous},
	{16000, 0|frame::ipc::MessageFlags::Synchronous},
	{8000, 0|frame::ipc::MessageFlags::Synchronous},
	{4000, 0|frame::ipc::MessageFlags::Synchronous},
	{2000, 0|frame::ipc::MessageFlags::Synchronous},
};

std::string						pattern;
const size_t					initarraysize = sizeof(initarray)/sizeof(InitStub);

size_t							crtwriteidx = 0;
size_t							crtreadidx  = 0;
size_t							writecount = 0;


size_t real_size(size_t _sz){
	//offset + (align - (offset mod align)) mod align
	return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
}

struct Message: frame::ipc::Message{
	uint32_t							idx;
    std::string						str;
	
	Message(uint32_t _idx):idx(_idx){
		idbg("CREATE ---------------- "<<(void*)this<<" idx = "<<idx);
		init();
		
	}
	Message(){
		idbg("CREATE ---------------- "<<(void*)this);
	}
	~Message(){
		idbg("DELETE ---------------- "<<(void*)this);
	}

	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(idx, "idx");
		
	}
	
	void init(){
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		str.resize(sz);
		const size_t	count = sz / sizeof(uint64_t);
		uint64_t			*pu  = reinterpret_cast<uint64_t*>(const_cast<char*>(str.data()));
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		for(uint64_t i = 0; i < count; ++i){
			pu[i] = pup[i % pattern_size];//pattern[i % pattern.size()];
		}
	}
	bool check()const{
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		idbg("str.size = "<<str.size()<<" should be equal to "<<sz);
		if(sz != str.size()){
			return false;
		}
		const size_t	count = sz / sizeof(uint64_t);
		const uint64_t	*pu = reinterpret_cast<const uint64_t*>(str.data());
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		
		for(uint64_t i = 0; i < count; ++i){
			if(pu[i] != pup[i % pattern_size]) return false;
		}
		return true;
	}
	
};

void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	frame::ipc::MessagePointerT &_rmessage_ptr,
	frame::ipc::MessagePointerT &_rresponse_ptr,
	ErrorConditionT const &_rerr
);


struct Context{
	Context():ipcreaderconfig(nullptr), ipcwriterconfig(nullptr), ipcprotocol(nullptr), ipcmsgreader(nullptr), ipcmsgwriter(nullptr){}
	
	frame::ipc::ReaderConfiguration	*ipcreaderconfig;
	frame::ipc::WriterConfiguration	*ipcwriterconfig;
	frame::ipc::Protocol			*ipcprotocol;
	frame::ipc::MessageReader		*ipcmsgreader;
	frame::ipc::MessageWriter		*ipcmsgwriter;

	
}								ctx;

frame::ipc::ConnectionContext	&ipcconctx(frame::ipc::TestEntryway::createContext());


void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	frame::ipc::MessagePointerT &_rmessage_ptr,
	frame::ipc::MessagePointerT &_rresponse_ptr,
	ErrorConditionT const &_rerr
){
	if(_rerr){
		SOLID_THROW("Message complete with error");
	}
	if(_rmessage_ptr.get()){
		idbg(static_cast<Message*>(_rmessage_ptr.get())->idx);
	}
	
	if(_rresponse_ptr.get()){
		
		size_t msgidx = static_cast<Message&>(*_rresponse_ptr).idx;
		
		if(not static_cast<Message&>(*_rresponse_ptr).check()){
			SOLID_THROW("Message check failed.");
		}
		
		if(msgidx != crtreadidx){
			SOLID_THROW("Message index invalid - SynchronousFlagE failed.");
		}
		
		++crtreadidx;
		
		idbg(crtreadidx);
		
		if(crtwriteidx < writecount){
			frame::ipc::MessageBundle	msgbundle;
			frame::ipc::MessageId		writer_msg_id;
			frame::ipc::MessageId		pool_msg_id;
			
			msgbundle.message_flags = initarray[crtwriteidx % initarraysize].flags;
			msgbundle.message_ptr = std::move(frame::ipc::MessagePointerT(new Message(crtwriteidx)));
			//msgbundle.complete_fnc = std::move(response_fnc);
			msgbundle.message_type_id = ctx.ipcprotocol->typeIndex(msgbundle.message_ptr.get());
			
			bool rv = ctx.ipcmsgwriter->enqueue(
				*ctx.ipcwriterconfig, msgbundle, pool_msg_id, writer_msg_id
			);
			
			idbg("enqueue rv = "<<rv<<" writer_msg_id = "<<writer_msg_id);
			idbg(frame::ipc::MessageWriterPrintPairT(*ctx.ipcmsgwriter, frame::ipc::MessageWriter::PrintInnerListsE));
			++crtwriteidx;
		}
	}
}

}//namespace
 
int test_protocol_synchronous(int argc, char **argv){
	
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("any:ew");
	Debug::the().initStdErr(false, nullptr);
#endif
	
	for(int i = 0; i < 127; ++i){
		if(isprint(i) and !isblank(i)){
			pattern += static_cast<char>(i);
		}
	}
	
	size_t	sz = real_size(pattern.size());
	
	if(sz > pattern.size()){
		pattern.resize(sz - sizeof(uint64_t));
	}else if(sz < pattern.size()){
		pattern.resize(sz);
	}
	
	const uint16_t							bufcp(1024*4);
	char									buf[bufcp];
	
	frame::ipc::WriterConfiguration			ipcwriterconfig;
	frame::ipc::ReaderConfiguration			ipcreaderconfig;
	frame::ipc::serialization_v1::Protocol	ipcprotocol;
	frame::ipc::MessageReader				ipcmsgreader;
	frame::ipc::MessageWriter				ipcmsgwriter;
	
	ErrorConditionT							error;
	
	
	ctx.ipcreaderconfig		= &ipcreaderconfig;
	ctx.ipcwriterconfig		= &ipcwriterconfig;
	ctx.ipcprotocol			= &ipcprotocol;
	ctx.ipcmsgreader		= &ipcmsgreader;
	ctx.ipcmsgwriter		= &ipcmsgwriter;
	
	ipcprotocol.registerType<::Message>(
		complete_message
	);
	
	const size_t					start_count = 10;
	
	writecount = 16;//start_count;//
	
	for(; crtwriteidx < start_count; ++crtwriteidx){
		frame::ipc::MessageBundle	msgbundle;
		frame::ipc::MessageId		writer_msg_id;
		frame::ipc::MessageId		pool_msg_id;
		
		msgbundle.message_flags = initarray[crtwriteidx % initarraysize].flags;
		msgbundle.message_ptr = std::move(frame::ipc::MessagePointerT(new Message(crtwriteidx)));
		msgbundle.message_type_id = ctx.ipcprotocol->typeIndex(msgbundle.message_ptr.get());
		
		
		bool rv = ipcmsgwriter.enqueue(
			ipcwriterconfig, msgbundle, pool_msg_id, writer_msg_id
		);
		SOLID_CHECK(rv);
		idbg("enqueue rv = "<<rv<<" writer_msg_id = "<<writer_msg_id);
		idbg(frame::ipc::MessageWriterPrintPairT(ipcmsgwriter, frame::ipc::MessageWriter::PrintInnerListsE));
	}
	
	
	{
		auto	reader_complete_lambda(
			[&ipcprotocol](const frame::ipc::MessageReader::Events _event, frame::ipc::MessagePointerT & _rresponse_ptr, const size_t _message_type_id){
				switch(_event){
					case frame::ipc::MessageReader::MessageCompleteE:{
						idbg("reader complete message");
						frame::ipc::MessagePointerT		message_ptr;
						ErrorConditionT					error;
						ipcprotocol[_message_type_id].complete_fnc(ipcconctx, message_ptr, _rresponse_ptr, error);
					}break;
					case frame::ipc::MessageReader::KeepaliveCompleteE:
						idbg("complete keepalive");
						break;
				}
			}
		);
		
		auto	writer_complete_lambda(
			[&ipcprotocol](frame::ipc::MessageBundle &_rmsgbundle, frame::ipc::MessageId const &_rmsgid){
				idbg("writer complete message");
				frame::ipc::MessagePointerT		response_ptr;
				ErrorConditionT					error;
				ipcprotocol[_rmsgbundle.message_type_id].complete_fnc(ipcconctx, _rmsgbundle.message_ptr, response_ptr, error);
				return ErrorConditionT();
			}
		);
		
		frame::ipc::MessageReader::CompleteFunctionT	readercompletefnc(std::cref(reader_complete_lambda));
		frame::ipc::MessageWriter::CompleteFunctionT	writercompletefnc(std::cref(writer_complete_lambda));
		
		ipcmsgreader.prepare(ipcreaderconfig);
		
		bool is_running = true;
		
		while(is_running and !error){
			uint32_t bufsz = ipcmsgwriter.write(buf, bufcp, false, writercompletefnc, ipcwriterconfig, ipcprotocol, ipcconctx, error);
			
			if(!error and bufsz){
				
				ipcmsgreader.read(buf, bufsz, readercompletefnc, ipcreaderconfig, ipcprotocol, ipcconctx, error);
			}else{
				is_running = false;
			}
		}
	}
	
	return 0;
}

