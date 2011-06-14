/*!
 * \file Mrrocpp_Proxy.cpp
 * \brief MRROC++ Proxy.
 *
 * \date 15-10-2010
 * \author Mateusz Boryn
 */

#include <limits>
#include <ctime>

#include "Mrrocpp_Proxy.hpp"
#include "Common/Thread.hpp"

namespace Proxies {
namespace Mrrocpp {

using namespace std;
using namespace boost;

Mrrocpp_Proxy::Mrrocpp_Proxy(const std::string & name) :
	Base::Component(name), state(MPS_NOT_INITIALIZED), port("port", 1, "range")
{
	LOG(LTRACE) << "Mrrocpp_Proxy::Mrrocpp_Proxy\n";

	header_iarchive = boost::shared_ptr <xdr_iarchive <> >(new xdr_iarchive <> );
	iarchive = boost::shared_ptr <xdr_iarchive <> >(new xdr_iarchive <> );
	header_oarchive = boost::shared_ptr <xdr_oarchive <> >(new xdr_oarchive <> );
	oarchive = boost::shared_ptr <xdr_oarchive <> >(new xdr_oarchive <> );

	memset(&imh, 0, sizeof(imh));
	memset(&rmh, 0, sizeof(rmh));

	// determine imh size in XDR format
	*header_oarchive << imh;
	initiate_message_header_size = header_oarchive->getArchiveSize();
	header_oarchive->clear_buffer();

	port.addConstraint("0");
	port.addConstraint("65535");
	registerProperty(port);

	waitForRequestTimeout = 0.12;
	acceptConnectionTimeout = 0.1;
	//waitForRequestTimeout = numeric_limits <double>::infinity();
}

Mrrocpp_Proxy::~Mrrocpp_Proxy()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::~Mrrocpp_Proxy\n";
}

bool Mrrocpp_Proxy::onStart()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onStart\n";
	return true;
}

bool Mrrocpp_Proxy::onInit()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onInit\n";

	registerStream("reading", &reading);

	rpcCall = registerEvent("rpcCall");
	registerStream("rpcParam", &rpcParam);
	registerStream("rpcResult", &rpcResult);
	h_onRpcResult.setup(this, &Mrrocpp_Proxy::onRpcResult);
	registerHandler("onRpcResult", &h_onRpcResult);

	serverSocket.setupServerSocket(port);

	readingMessage.reset();
	rpcResultMessage.reset();

	state = MPS_LISTENING;

	return true;
}

bool Mrrocpp_Proxy::onStop()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onStop\n";
	return true;
}

bool Mrrocpp_Proxy::onFinish()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onFinish\n";
	serverSocket.closeSocket();

	readingMessage.reset();
	rpcResultMessage.reset();

	state = MPS_NOT_INITIALIZED;

	return true;
}

bool Mrrocpp_Proxy::onStep()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onStep\n";

	timer.restart();

	switch (state)
	{
		case MPS_LISTENING:
			tryAcceptConnection();
			break;
		case MPS_CONNECTED:
			tryReceiveFromMrrocpp();
			break;
		case MPS_WAITING_FOR_RPC_RESULT:
			Common::Thread::msleep(50);
			break;
		default:
			throw logic_error("Mrrocpp_Proxy::onStep(): wrong state");
	}

	loops++;
	total += timer.elapsed();
	if (loops >= 100) {
		LOG(LWARNING) << "Mrrocpp_Proxy execution time: " << 0.01 * total << " s\n";
		loops = 0;
		total = 0;
	}

	return true;
}

void Mrrocpp_Proxy::tryAcceptConnection()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::tryAcceptConnection()\n";
	if (!serverSocket.isDataAvailable(acceptConnectionTimeout)) {
		LOG(LTRACE) << "if (!serverSocket.isDataAvailable()) {\n";
		return;
	}
	clientSocket = serverSocket.acceptConnection();

	readingMessage.reset();
	rpcResultMessage.reset();

	LOG(LNOTICE) << "Client connected.";
	state = MPS_CONNECTED;
}

void Mrrocpp_Proxy::tryReceiveFromMrrocpp()
{
	try {
		//LOG(LFATAL)<<"Mrrocpp_Proxy::tryReceiveFromMrrocpp() begin\n";
		if (clientSocket->isDataAvailable(waitForRequestTimeout)) {
			receiveBuffersFromMrrocpp();
			if (imh.is_rpc_call) {
				LOG(LNOTICE)<<"RPC Call received.";
				rpcCallMutex.lock();
				rpcParam.write(*iarchive); // send RPC param
				rpcCall->raise();
				state = MPS_WAITING_FOR_RPC_RESULT; // wait for RPC result
			} else {
				oarchive->clear_buffer();
				if(!reading.empty()){
					readingMessage = reading.read();
					rmh.is_rpc_call = false;
					readingMessage->send(oarchive);
				}

				sendBuffersToMrrocpp();
				readingMessage.reset();
			}
		}
	} catch (std::exception& ex) {
		LOG(LERROR) << "Mrrocpp_Proxy::tryReceiveFromMrrocpp(): Probably client disconnected: " << ex.what();
		LOG(LNOTICE) << "Closing socket.\n";
		clientSocket->closeSocket();
		state = MPS_LISTENING;
	}
	//LOG(LFATAL)<<"Mrrocpp_Proxy::tryReceiveFromMrrocpp() end\n";
}

void Mrrocpp_Proxy::onRpcResult()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::onRpcResult\n";
	rpcResultMessage = rpcResult.read();

	if (state != MPS_WAITING_FOR_RPC_RESULT) {
		LOG(LFATAL) << "Mrrocpp_Proxy::onRpcResult(): state != MPS_WAITING_FOR_RPC_RESULT\n";
		return;
	}

	rmh.is_rpc_call = true;

	oarchive->clear_buffer();
	rpcResultMessage->send(oarchive);

	sendBuffersToMrrocpp();

	rpcCallMutex.unlock();
	state = MPS_CONNECTED;
}

void Mrrocpp_Proxy::receiveBuffersFromMrrocpp()
{
	LOG(LTRACE) << "Mrrocpp_Proxy::receiveBuffersFromMrrocpp() begin\n";
	header_iarchive->clear_buffer();

	clientSocket->read(header_iarchive->get_buffer(), initiate_message_header_size);

	*header_iarchive >> imh;

	iarchive->clear_buffer();
	clientSocket->read(iarchive->get_buffer(), imh.data_size);

	LOG(LDEBUG) << "imh.data_size: " << imh.data_size << endl;
}

void Mrrocpp_Proxy::sendBuffersToMrrocpp()
{
	LOG(LTRACE) << "sendBuffersToMrrocpp() begin\n";

	rmh.data_size = oarchive->getArchiveSize();
	//LOG(LFATAL) << "sendBuffersToMrrocpp(): rmh.data_size = " << rmh.data_size;
	//LOG(LFATAL) << "sendBuffersToMrrocpp(): rmh.readingTimeSeconds = "<<rmh.readingTimeSeconds<<"; rmh.readingTimeNanoseconds = " << rmh.readingTimeNanoseconds << "\n";
	struct timespec ts;
	if(clock_gettime(CLOCK_REALTIME, &ts) == 0){
		rmh.sendTimeSeconds = ts.tv_sec;
		rmh.sendTimeNanoseconds = ts.tv_nsec;
	} else {
		rmh.sendTimeSeconds = 0;
		rmh.sendTimeNanoseconds = 0;
	}

	header_oarchive->clear_buffer();
	*header_oarchive << rmh;

	clientSocket->writev2(header_oarchive->get_buffer(), header_oarchive->getArchiveSize(), oarchive->get_buffer(), oarchive->getArchiveSize());
	header_oarchive->clear_buffer();
	oarchive->clear_buffer();
}

} // namespace Mrrocpp
} // namespace Proxies
