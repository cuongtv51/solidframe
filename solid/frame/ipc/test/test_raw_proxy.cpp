#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/aio/aioreactor.hpp"
#include "solid/frame/aio/aioobject.hpp"
#include "solid/frame/aio/aiolistener.hpp"
#include "solid/frame/aio/aiotimer.hpp"
#include "solid/frame/aio/aioresolver.hpp"

#include "solid/frame/aio/openssl/aiosecurecontext.hpp"
#include "solid/frame/aio/openssl/aiosecuresocket.hpp"

#include "solid/frame/ipc/ipcservice.hpp"
#include "solid/frame/ipc/ipcconfiguration.hpp"
#include "solid/frame/ipc/ipcprotocol_serialization_v1.hpp"


#include <mutex>
#include <thread>
#include <condition_variable>
#include "solid/system/exception.hpp"

#include "solid/system/debug.hpp"

#include <iostream>

using namespace std;
using namespace solid;

typedef frame::Scheduler<frame::aio::Reactor>	AioSchedulerT;
typedef frame::aio::openssl::Context			SecureContextT;

namespace{

struct InitStub{
	size_t		size;
	ulong		flags;
};

InitStub initarray[] = {
	{100000, 0},
	{2000, 0},
	{4000, 0},
	{8000, 0},
	{16000, 0},
	{32000, 0},
	{64000, 0},
	{128000, 0},
	{256000, 0},
	{512000, 0},
	{1024000, 0},
	{2048000, 0},
	{4096000, 0},
	{8192000, 0},
	{16384000, 0}
};

std::string						pattern;
const size_t					initarraysize = sizeof(initarray)/sizeof(InitStub);

std::atomic<size_t>				crtwriteidx(0);
std::atomic<size_t>				crtreadidx(0);
std::atomic<size_t>				crtbackidx(0);
std::atomic<size_t>				crtackidx(0);
std::atomic<size_t>				writecount(0);

size_t							connection_count(0);

bool							running = true;
mutex							mtx;
condition_variable					cnd;
frame::ipc::Service				*pipcclient = nullptr;
std::atomic<uint64_t>				transfered_size(0);
std::atomic<size_t>				transfered_count(0);
std::string						raw_data;


size_t real_size(size_t _sz){
	//offset + (align - (offset mod align)) mod align
	return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
}

void init_string(std::string &_rstr, const size_t _idx){
	const size_t	sz = real_size(initarray[_idx % initarraysize].size);
	_rstr.resize(sz);
	const size_t	count = sz / sizeof(uint64_t);
	uint64_t			*pu  = reinterpret_cast<uint64_t*>(const_cast<char*>(_rstr.data()));
	const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
	const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
	for(uint64_t i = 0; i < count; ++i){
		pu[i] = pup[(_idx + i) % pattern_size];//pattern[i % pattern.size()];
	}
}

struct Message: frame::ipc::Message{
	
	
	uint32_t							idx;
    std::string						str;
	bool							serialized;
	
	Message(uint32_t _idx):idx(_idx), serialized(false){
		idbg("CREATE ---------------- "<<(void*)this<<" idx = "<<idx);
		init();
		
	}
	Message(): serialized(false){
		idbg("CREATE ---------------- "<<(void*)this);
	}
	~Message(){
		idbg("DELETE ---------------- "<<(void*)this);
		if(not serialized and not this->isBackOnSender()){
			SOLID_THROW("Message not serialized.");
		}
	}

	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(idx, "idx");
		
		if(S::IsSerializer){
			serialized = true;
		}
	}
	
	void init(){
		init_string(str, idx);
	}
	
	bool check()const{
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		idbg("str.size = "<<str.size()<<" should be equal to "<<sz);
		if(sz != str.size()){
			return false;
		}
		//return true;
		const size_t	count = sz / sizeof(uint64_t);
		const uint64_t	*pu = reinterpret_cast<const uint64_t*>(str.data());
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		
		for(uint64_t i = 0; i < count; ++i){
			if(pu[i] != pup[(i + idx) % pattern_size]){
				SOLID_THROW("Message check failed.");
				return false;
			}
		}
		return true;
	}
	
};

void client_connection_stop(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId()<<" error: "<<_rctx.error().message());
	if(!running){
		++connection_count;
	}
}

template <class F>
struct RecvClosure{
	std::string data;
	F			finalize_fnc;
	
	RecvClosure(F && _finalize_fnc): finalize_fnc(std::move(_finalize_fnc)){}
	
	void operator()(frame::ipc::ConnectionContext& _rctx, const char *_pdata, size_t &_rsz, ErrorConditionT const& _rerror){
		idbg("received raw data: sz = "<<_rsz<<" error = "<<_rerror.message());
		SOLID_CHECK(not _rerror);
		SOLID_CHECK(_rsz != 0);
		
		size_t towrite = raw_data.size() - data.size();
		
		if(towrite > _rsz){
			towrite = _rsz;
		}
		
		data.append(_pdata, towrite);
		
		_rsz = towrite;
		
		if(raw_data.size() == data.size()){
			finalize_fnc(_rctx, _rerror, std::move(data));
		}else{
			//continue reading
			_rctx.service().connectionNotifyRecvSomeRawData(_rctx.recipientId(), std::move(*this));
		}
	}
};

void client_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	
	auto lambda =  [](frame::ipc::ConnectionContext& _rctx, ErrorConditionT const& _rerror){
		idbg("sent raw data: "<<_rerror.message());
		SOLID_CHECK(not _rerror);
		//sent the raw_data, prepare receive the message back:
		
		auto lambda =  [](frame::ipc::ConnectionContext&_rctx, ErrorConditionT const& _rerror, std::string &&_rdata){
			idbg("received back raw data: "<<_rerror.message()<<" data.size = "<<_rdata.size());
			SOLID_CHECK(not _rerror);
			SOLID_CHECK(_rdata == raw_data);
			//activate concetion
			_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId());
		};
		
		_rctx.service().connectionNotifyRecvSomeRawData(_rctx.recipientId(), RecvClosure<decltype(lambda)>(std::move(lambda)));
	};
	_rctx.service().connectionNotifySendAllRawData(_rctx.recipientId(), lambda, std::string(raw_data));
}

void server_connection_stop(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId()<<" error: "<<_rctx.error().message());
}


void server_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	
	auto lambda =  [](frame::ipc::ConnectionContext&_rctx, ErrorConditionT const& _rerror, std::string &&_rdata){
		
		auto lambda =  [](frame::ipc::ConnectionContext&_rctx, ErrorConditionT const& _rerror){
			idbg("sent raw data: "<<_rerror.message());
			SOLID_CHECK(not _rerror);
			//now that we've sent the raw string back, activate the connection
			_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId());
		};
		
		idbg("received raw data: "<<_rerror.message()<<" data_size: "<<_rdata.size());
		
		_rctx.service().connectionNotifySendAllRawData(_rctx.recipientId(), lambda, std::move(_rdata));
		
	};
	
	_rctx.service().connectionNotifyRecvSomeRawData(_rctx.recipientId(), RecvClosure<decltype(lambda)>(std::move(lambda)));
}


void client_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg(_rctx.recipientId());
	
	if(_rsent_msg_ptr){
		if(!_rerror){
			++crtackidx;
		}
	}
	if(_rrecv_msg_ptr){
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		//cout<< _rmsgptr->str.size()<<'\n';
		transfered_size += _rrecv_msg_ptr->str.size();
		++transfered_count;
		
		if(!_rrecv_msg_ptr->isBackOnSender()){
			SOLID_THROW("Message not back on sender!.");
		}
		
		++crtbackidx;
		
		if(crtbackidx == writecount){
			unique_lock<mutex> lock(mtx);
			running = false;
			cnd.notify_one();
		}
	}
}

void server_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	if(_rrecv_msg_ptr){
		idbg(_rctx.recipientId()<<" received message with id on sender "<<_rrecv_msg_ptr->requestId());
		
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		if(!_rrecv_msg_ptr->isOnPeer()){
			SOLID_THROW("Message not on peer!.");
		}
		
		//send message back
		if(_rctx.recipientId().isInvalidConnection()){
			SOLID_THROW("Connection id should not be invalid!");
		}
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
		
		++crtreadidx;
		idbg(crtreadidx);
	}
	if(_rsent_msg_ptr){
		idbg(_rctx.recipientId()<<" done sent message "<<_rsent_msg_ptr.get());
	}
}

}//namespace

int test_raw_proxy(int argc, char **argv){
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("view");
	Debug::the().moduleMask("frame_ipc:view any:view");
	Debug::the().initStdErr(false, nullptr);
	//Debug::the().initFile("test_clientserver_basic", false);
#endif
	
	size_t max_per_pool_connection_count = 1;
	
	if(argc > 1){
		max_per_pool_connection_count = atoi(argv[1]);
		if(max_per_pool_connection_count == 0){
			max_per_pool_connection_count = 1;
		}
		if(max_per_pool_connection_count > 100){
			max_per_pool_connection_count = 100;
		}
	}
	
	for(int j = 0; j < 1; ++j){
		for(int i = 0; i < 127; ++i){
			int c = (i + j) % 127;
			if(isprint(c) and !isblank(c)){
				pattern += static_cast<char>(c);
			}
		}
	}
	
	size_t	sz = real_size(pattern.size());
	
	if(sz > pattern.size()){
		pattern.resize(sz - sizeof(uint64_t));
	}else if(sz < pattern.size()){
		pattern.resize(sz);
	}
	
	init_string(raw_data, 0);
	
	{
		AioSchedulerT			sch_client;
		AioSchedulerT			sch_server;
			
			
		frame::Manager			m;
		frame::ipc::ServiceT	ipcserver(m);
		frame::ipc::ServiceT	ipcclient(m);
		ErrorConditionT			err;
		
		frame::aio::Resolver	resolver;
		
		err = sch_client.start(1);
		
		if(err){
			edbg("starting aio client scheduler: "<<err.message());
			return 1;
		}
		
		err = sch_server.start(1);
		
		if(err){
			edbg("starting aio server scheduler: "<<err.message());
			return 1;
		}
		
		err = resolver.start(1);
		
		if(err){
			edbg("starting aio resolver: "<<err.message());
			return 1;
		}
		
		std::string		server_port;
		
		{//ipc server initialization
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(sch_server, proto);
			
			proto->registerType<Message>(
				server_complete_message
			);
			
			cfg.connection_recv_buffer_max_capacity_kb = 1;
			cfg.connection_send_buffer_max_capacity_kb = 1;
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Raw;
			cfg.connection_stop_fnc = server_connection_stop;
			cfg.connection_start_incoming_fnc = server_connection_start;
			
			cfg.listener_address_str = "0.0.0.0:0";
			
			err = ipcserver.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting server ipcservice: "<<err.message());
				//exiting
				return 1;
			}
			
			{
				std::ostringstream oss;
				oss<<ipcserver.configuration().listenerPort();
				server_port = oss.str();
				idbg("server listens on port: "<<server_port);
			}
		}
		
		{//ipc client initialization
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(sch_client, proto);
			
			proto->registerType<Message>(
				client_complete_message
			);
			
			cfg.connection_recv_buffer_max_capacity_kb = 1;
			cfg.connection_send_buffer_max_capacity_kb = 1;
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Raw;
			
			cfg.connection_stop_fnc = client_connection_stop;
			cfg.connection_start_outgoing_fnc = client_connection_start;
			
			cfg.pool_max_active_connection_count = max_per_pool_connection_count;
			
			cfg.name_resolve_fnc = frame::ipc::InternetResolverF(resolver, server_port.c_str()/*, SocketInfo::Inet4*/);
			
			err = ipcclient.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting client ipcservice: "<<err.message());
				//exiting
				return 1;
			}
		}
		
		pipcclient  = &ipcclient;
		
		const size_t		start_count = 10;
		
		writecount = 10;//initarraysize * 10;//start_count;//
		
		for(; crtwriteidx < start_count;){
			frame::ipc::MessagePointerT	msgptr(new Message(crtwriteidx));
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msgptr,
				initarray[crtwriteidx % initarraysize].flags | frame::ipc::MessageFlags::WaitResponse
			);
		}
		
		unique_lock<mutex>	lock(mtx);
		
		while(running){
			//cnd.wait(lock);
			NanoTime	abstime = NanoTime::createRealTime();
			abstime += (120 * 1000);//ten seconds
			cnd.wait(lock);
			bool b = true;//cnd.wait(lock, abstime);
			if(!b){
				//timeout expired
				SOLID_THROW("Process is taking too long.");
			}
		}
		
		if(crtwriteidx != crtackidx){
			SOLID_THROW("Not all messages were completed");
		}
		
		//m.stop();
	}
	
	//exiting
	
	std::cout<<"Transfered size = "<<(transfered_size * 2)/1024<<"KB"<<endl;
	std::cout<<"Transfered count = "<<transfered_count<<endl;
	std::cout<<"Connection count = "<<connection_count<<endl;
	
	return 0;
}
