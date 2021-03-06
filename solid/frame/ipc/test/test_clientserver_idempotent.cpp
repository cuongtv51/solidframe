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

#include <signal.h>
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
	{88192000, 0},
	{16384000, 0},
	{26384000, 0},
	{16384000, 0},
	{8192000, 0}
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
bool							start_sleep = false;
mutex							mtx;
condition_variable				cnd;
frame::ipc::Service				*pipcclient = nullptr;
std::atomic<uint64_t>			transfered_size(0);
std::atomic<size_t>				transfered_count(0);


size_t real_size(size_t _sz){
	//offset + (align - (offset mod align)) mod align
	return _sz + ((sizeof(uint64_t) - (_sz % sizeof(uint64_t))) % sizeof(uint64_t));
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
		idbg("DELETE ---------------- "<<(void*)this<<" idx = "<<idx<<" str.size = "<<str.size());
// 		if(not serialized and not this->isBackOnSender() and idx != 0){
// 			SOLID_THROW("Message not serialized.");
// 		}
	}

	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(idx, "idx");
		
		if(S::IsSerializer){
			serialized = true;
		}
		
		if(isOnPeer()){
			unique_lock<mutex> lock(mtx);
			if(!start_sleep){
				start_sleep = true;
				cnd.notify_one();
				return;
			}
		}
	}
	
	void init(){
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		str.resize(sz);
		const size_t	count = sz / sizeof(uint64_t);
		uint64_t			*pu  = reinterpret_cast<uint64_t*>(const_cast<char*>(str.data()));
		const uint64_t	*pup = reinterpret_cast<const uint64_t*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64_t);
		for(uint64_t i = 0; i < count; ++i){
			pu[i] = pup[(idx + i) % pattern_size];//pattern[i % pattern.size()];
		}
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

void client_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	auto lambda =  [](frame::ipc::ConnectionContext&, ErrorConditionT const& _rerror){
		idbg("enter active error: "<<_rerror.message());
		return frame::ipc::MessagePointerT();
	};
	_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId(), lambda);
}

void server_connection_stop(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId()<<" error: "<<_rctx.error().message());
}

void server_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
	auto lambda =  [](frame::ipc::ConnectionContext&, ErrorConditionT const& _rerror){
		idbg("enter active error: "<<_rerror.message());
		return frame::ipc::MessagePointerT();
	};
	_rctx.service().connectionNotifyEnterActiveState(_rctx.recipientId(), lambda);
}


void client_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg(_rctx.recipientId());
	
	++crtackidx;
	
	if(_rsent_msg_ptr.get()){
		idbg("idx = "<<_rsent_msg_ptr->idx);
		if(!_rerror){
		}else{
			wdbg("send message complete: "<<_rerror.message());
			SOLID_CHECK(_rsent_msg_ptr->idx == 0 or _rsent_msg_ptr->idx == 2);
			SOLID_CHECK(
				_rerror == frame::ipc::error_message_connection and
				(
					(_rctx.error() == frame::aio::error_stream_shutdown and not _rctx.systemError())
					or
					(_rctx.error() and _rctx.systemError())
				)
			);
		}
	}
	if(_rrecv_msg_ptr.get()){
		idbg("idx = "<<_rrecv_msg_ptr->idx);
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		SOLID_CHECK(not _rerror);
		
		//cout<< _rmsgptr->str.size()<<'\n';
		
		if(!_rrecv_msg_ptr->isBackOnSender()){
			SOLID_THROW("Message not back on sender!.");
		}
		
		SOLID_CHECK(_rrecv_msg_ptr->idx == 1 or _rrecv_msg_ptr->idx == 3 or _rrecv_msg_ptr->idx == 4);
		
		transfered_size += _rrecv_msg_ptr->str.size();
		++transfered_count;
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
	if(_rrecv_msg_ptr.get()){
		idbg(_rctx.recipientId()<<" received message with id on sender "<<_rrecv_msg_ptr->requestId());
		
		//SOLID_CHECK(_rrecv_msg_ptr->idx != 0);
		
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		if(!_rrecv_msg_ptr->isOnPeer()){
			SOLID_THROW("Message not on peer!.");
		}
		
		if(_rrecv_msg_ptr->idx == 0){
			return;//ignore the first message
		}
		
		//send message back
		if(_rctx.recipientId().isInvalidConnection()){
			SOLID_THROW("Connection id should not be invalid!");
		}
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		if(err){
			SOLID_THROW_EX("Connection id should not be invalid!", err.message());
		}
	}
	if(_rsent_msg_ptr.get()){
		idbg(_rctx.recipientId()<<" done sent message "<<_rsent_msg_ptr.get());
	}
}

}//namespace

int test_clientserver_idempotent(int argc, char **argv){
	
	signal(SIGPIPE, SIG_IGN);
	
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("frame_ipc:ew any:ew");
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
		
		std::string		server_port("39999");
		
		{//ipc server initialization
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(sch_server, proto);
			
			proto->registerType<Message>(
				server_complete_message
			);
			
			//cfg.recv_buffer_capacity = 1024;
			//cfg.send_buffer_capacity = 1024;
			
			cfg.connection_stop_fnc = server_connection_stop;
			cfg.connection_start_incoming_fnc = server_connection_start;
			
			cfg.listener_address_str = "0.0.0.0:";
			cfg.listener_address_str += server_port;
			
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
			
			//cfg.recv_buffer_capacity = 1024;
			//cfg.send_buffer_capacity = 1024;
			
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
		
		std::vector<std::shared_ptr<Message>> msg_vec{
			std::make_shared<Message>(0),
			std::make_shared<Message>(1),
			std::make_shared<Message>(2),
			std::make_shared<Message>(3),
			std::make_shared<Message>(4)
		};
		{
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msg_vec[0],
				 0|frame::ipc::MessageFlags::WaitResponse
			);
		}
		
		{
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msg_vec[1],
				 frame::ipc::MessageFlags::WaitResponse | frame::ipc::MessageFlags::Idempotent
			);
		}
		
		{
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msg_vec[2],
				 0|frame::ipc::MessageFlags::OneShotSend
			);
		}
		
		{
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msg_vec[3],
				frame::ipc::MessageFlags::WaitResponse | frame::ipc::MessageFlags::Idempotent | frame::ipc::MessageFlags::Synchronous
			);
		}
		
		{
			++crtwriteidx;
			ipcclient.sendMessage(
				"localhost", msg_vec[4],
				 frame::ipc::MessageFlags::WaitResponse | frame::ipc::MessageFlags::Synchronous
			);
		}
		
		writecount = 3;
		
		{
			unique_lock<mutex>	lock(mtx);
			while(!start_sleep){
				if(cnd.wait_for(lock, std::chrono::milliseconds(10*1000)) == std::cv_status::timeout){
					//timeout expired
					SOLID_THROW("Process is taking too long.");
				}
			}
		}
		
		idbg("---- Before server stopping ----");
		ipcserver.stop();
		idbg("---- After server stopped ----");
		
		std::this_thread::sleep_for(std::chrono::milliseconds(5*1000));
		
		idbg("---- Before server start ----");
		ipcserver.start();
		idbg("---- After server started ----");
		
		unique_lock<mutex>	lock(mtx);
		
		while(running){
			//cnd.wait(lock);
			//cnd.wait(lock);
			if(cnd.wait_for(lock, std::chrono::milliseconds(30*1000)) == std::cv_status::timeout){
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
