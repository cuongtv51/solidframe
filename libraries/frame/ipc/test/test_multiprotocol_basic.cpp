#include "frame/manager.hpp"
#include "frame/scheduler.hpp"
#include "frame/service.hpp"

#include "frame/aio/aioreactor.hpp"
#include "frame/aio/aioobject.hpp"
#include "frame/aio/aiolistener.hpp"
#include "frame/aio/aiotimer.hpp"
#include "frame/aio/aioresolver.hpp"

#include "frame/aio/openssl/aiosecurecontext.hpp"
#include "frame/aio/openssl/aiosecuresocket.hpp"

#include "frame/ipc/ipcservice.hpp"
#include "frame/ipc/ipcconfiguration.hpp"
#include "frame/ipc/ipcprotocol_serialization_v1.hpp"


#include "system/thread.hpp"
#include "system/mutex.hpp"
#include "system/condition.hpp"
#include "system/exception.hpp"

#include "system/debug.hpp"

#include "test_multiprotocol_basic/alpha/server/alphaserver.hpp"
#include "test_multiprotocol_basic/beta/server/betaserver.hpp"
#include "test_multiprotocol_basic/gamma/server/gammaserver.hpp"


#include "test_multiprotocol_basic/alpha/client/alphaclient.hpp"
#include "test_multiprotocol_basic/beta/client/betaclient.hpp"
#include "test_multiprotocol_basic/gamma/client/gammaclient.hpp"

#include <iostream>

#include "test_multiprotocol_basic/clientcommon.hpp"

using namespace std;
using namespace solid;

typedef frame::aio::openssl::Context			SecureContextT;


namespace{

std::atomic<size_t>				wait_count(0);
Mutex							mtx;
Condition						cnd;

std::atomic<uint64_t>				transfered_size(0);
std::atomic<size_t>				transfered_count(0);
std::atomic<size_t>				connection_count(0);


void server_connection_stop(frame::ipc::ConnectionContext &_rctx, ErrorConditionT const&){
	idbg(_rctx.recipientId());
}

void server_connection_start(frame::ipc::ConnectionContext &_rctx){
	idbg(_rctx.recipientId());
}

}//namespace



int test_multiprotocol_basic(int argc, char **argv){
	Thread::init();
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("frame_ipc:ew any:ew frame:ew");
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
	
	
	{
		AioSchedulerT			sch_client;
		AioSchedulerT			sch_server;
			
			
		frame::Manager			m;
		frame::ipc::Service		ipcserver(m);
		frame::ipc::Service		ipcclient(m);
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
			
			gamma_server::register_messages(*proto);
			beta_server::register_messages(*proto);
			alpha_server::register_messages(*proto);
			
			cfg.connection_stop_fnc = server_connection_stop;
			cfg.connection_start_incoming_fnc = server_connection_start;
			
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Active;
			cfg.listener_address_str = "0.0.0.0:0";
			
			err = ipcserver.reconfigure(std::move(cfg));
			
			if(err){
				edbg("starting server ipcservice: "<<err.message());
				Thread::waitAll();
				return 1;
			}
			
			{
				std::ostringstream oss;
				oss<<ipcserver.configuration().listenerPort();
				server_port = oss.str();
				idbg("server listens on port: "<<server_port);
			}
		}
		
		Context		ctx(sch_client, m, resolver, max_per_pool_connection_count, server_port, wait_count, mtx, cnd);
		
		err = alpha_client::start(ctx);
		
		if(err){
			edbg("starting alpha ipcservice: "<<err.message());
			Thread::waitAll();
			return 1;
		}
		
		err = beta_client::start(ctx);
		
		if(err){
			edbg("starting alpha ipcservice: "<<err.message());
			Thread::waitAll();
			return 1;
		}
		
		err = gamma_client::start(ctx);
		
		if(err){
			edbg("starting gamma ipcservice: "<<err.message());
			Thread::waitAll();
			return 1;
		}
		
		Locker<Mutex>	lock(mtx);
		
		while(wait_count){
			//cnd.wait(lock);
			TimeSpec	abstime = TimeSpec::createRealTime();
			abstime += (120 * 1000);//ten seconds
			cnd.wait(lock);
			bool b = true;//cnd.wait(lock, abstime);
			if(!b){
				//timeout expired
				SOLID_THROW("Process is taking too long.");
			}
		}
		
		m.stop();
	}
	
	Thread::waitAll();
	
	std::cout<<"Transfered size = "<<(transfered_size * 2)/1024<<"KB"<<endl;
	std::cout<<"Transfered count = "<<transfered_count<<endl;
	std::cout<<"Connection count = "<<connection_count<<endl;
	return 0;
}