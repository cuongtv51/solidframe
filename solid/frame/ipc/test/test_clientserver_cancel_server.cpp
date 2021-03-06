//
// 1. Send one message from client to server
// 2. On server receive message, send messages as in test_protocol_cancel
// 3. On client receive first message cancel all messages from the server side


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
	bool		cancel;
	ulong		flags;
};

//NOTE: if making more messages non-cancelable, please consider to change the value of expected_transfered_count

InitStub initarray[] = {
	{100000,	false,	0},//first message must not be canceled
	{16384000,	false,	0},//not caceled
	{8192000,	true,	0|frame::ipc::MessageFlags::Synchronous},
	{4096000,	true,	0},
	{2048000,	false,	0},//not caceled
	{1024000,	true,	0},
	{512000,	false,	0|frame::ipc::MessageFlags::Synchronous},//not canceled
	{256000,	true,	0},
	{1280000,	true,	0},
	{6400000,	true,	0},
	{32000,		false,	0},//not canceled
	{1600000,	true,	0},
	{8000000,	true,	0},
	{4000000,	true,	0},
	{200000,	true,	0},
};

const size_t					expected_transfered_count = 5;

typedef std::vector<frame::ipc::MessageId>		MessageIdVectorT;

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
frame::ipc::Service				*pipcserver = nullptr;
std::atomic<uint64_t>				transfered_size(0);
std::atomic<size_t>				transfered_count(0);
frame::ipc::RecipientId			recipient_id;

MessageIdVectorT				message_uid_vec;


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
		idbg("DELETE ---------------- "<<(void*)this);
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
}

void server_connection_stop(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId()<<" error: "<<_rctx.error().message());
}

void server_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
}


void client_receive_message(frame::ipc::ConnectionContext &_rctx, std::shared_ptr<Message> &_rmsgptr){
	idbg(_rctx.recipientId());
	
	if(not _rmsgptr->check()){
		SOLID_THROW("Message check failed.");
	}
	
	size_t idx = static_cast<Message&>(*_rmsgptr).idx;
	SOLID_CHECK(not initarray[idx % initarraysize].cancel);
	
	//cout<< _rmsgptr->str.size()<<'\n';
	transfered_size += _rmsgptr->str.size();
	++transfered_count;
	
	if(!crtbackidx){
		idbg("canceling all messages");
		for(auto msguid:message_uid_vec){
			idbg("Cancel message: "<<msguid);
			pipcserver->cancelMessage(recipient_id, msguid);
		}
	}
	
	++crtbackidx;
	
	if(crtbackidx == expected_transfered_count){
		unique_lock<mutex> lock(mtx);
		running = false;
		cnd.notify_one();
	}
}

void client_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg(_rctx.recipientId());
	if(_rsent_msg_ptr.get()){
		if(!_rerror){
			++crtackidx;
		}
	}
	
	if(_rrecv_msg_ptr.get()){
		client_receive_message(_rctx, _rrecv_msg_ptr);
	}
}

void server_complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<Message> &_rsent_msg_ptr, std::shared_ptr<Message> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	idbg(_rctx.recipientId());
	if(_rrecv_msg_ptr.get()){
		idbg(_rctx.recipientId()<<" message id on sender "<<_rrecv_msg_ptr->requestId());
		if(not _rrecv_msg_ptr->check()){
			SOLID_THROW("Message check failed.");
		}
		
		if(!_rrecv_msg_ptr->isOnPeer()){
			SOLID_THROW("Message not on peer!.");
		}
		
		if(_rctx.recipientId().isInvalidConnection()){
			SOLID_THROW("Connection id should not be invalid!");
		}
		
		SOLID_CHECK(recipient_id.isInvalidConnection());
		
		recipient_id = _rctx.recipientId();
		
		// Step 2
		writecount = initarraysize;//start_count;//
		
		for(crtwriteidx = 0; crtwriteidx < writecount; ++crtwriteidx){
			frame::ipc::MessageId msguid;
			
			ErrorConditionT err = _rctx.service().sendMessage(
				recipient_id, frame::ipc::MessagePointerT(new Message(crtwriteidx)),
				msguid
			);
			
			if(err){
				SOLID_THROW_EX("Connection id should not be invalid!", err.message());
			}
			
			
			if(initarray[crtwriteidx % initarraysize].cancel){
				message_uid_vec.push_back(msguid); //we cancel this one
				idbg("schedule for cancel message: "<<message_uid_vec.back());
			}
		}
	}
}

char pattern_check[256];

void string_check(std::string const &_rstr, const char* _pb, size_t _len){
	if(_rstr.size() > 1024 and _len){
		
		SOLID_CHECK(pattern_check[static_cast<size_t>(_rstr.back())] == _pb[0]);
	}
}

}//namespace


namespace solid{namespace serialization {namespace binary{

extern StringCheckFncT pcheckfnc;

}}}

int test_clientserver_cancel_server(int argc, char **argv){
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("frame_ipc:ew frame_aio:ew any:ew");
	Debug::the().initStdErr(false, nullptr);
#endif
	
	solid::serialization::binary::pcheckfnc = &string_check;
	
	size_t max_per_pool_connection_count = 1;
	
// 	if(argc > 1){
// 		max_per_pool_connection_count = atoi(argv[1]);
// 	}
	
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
	
	for(auto it = pattern.cbegin(); it != pattern.cend(); ++it){
		pattern_check[static_cast<size_t>(*it)] = pattern[static_cast<size_t>(((it - pattern.cbegin()) + 1) % pattern.size())];
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
		
		std::string		server_port;
		
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
			
			cfg.listener_address_str = "0.0.0.0:0";
			cfg.connection_start_state = frame::ipc::ConnectionState::Active;
			
			cfg.writer.max_message_count_multiplex = 6;
			
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
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Active;
			cfg.connection_stop_fnc = client_connection_stop;
			cfg.connection_start_outgoing_fnc = client_connection_start;
			
			cfg.pool_max_active_connection_count = max_per_pool_connection_count;
			
			cfg.writer.max_message_count_multiplex = 6;
			
			cfg.name_resolve_fnc = frame::ipc::InternetResolverF(resolver, server_port.c_str()/*, SocketInfo::Inet4*/);
			
			err = ipcclient.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting client ipcservice: "<<err.message());
				//exiting
				return 1;
			}
		}
		
		pipcclient = &ipcclient;
		pipcserver = &ipcserver;
		
		{
			//Step 1.
			frame::ipc::MessagePointerT	msgptr(new Message(0));
			ipcclient.sendMessage(
				"localhost", msgptr,
				initarray[0].flags
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
		
		if(crtbackidx != expected_transfered_count){
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
