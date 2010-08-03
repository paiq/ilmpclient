/*
 * ILMP client library - http://opensource.implicit-link.com/
 * Copyright (c) 2010 Implicit Link
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ILMPCLIENT_ILMP_STREAM_H
#define ILMPCLIENT_ILMP_STREAM_H

#include <string>
#include <iostream>
#include <map>

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>

#include "TokenWalker.h"

#define ILMP_VERSION "1.0"
#define ILMP_PING_INTERVAL 60

using boost::asio::ip::tcp;

#define ILMPERR_NETWORK		1
#define ILMPERR_PROTOCOL	2
#define ILMPERR_PROTOVER	3

class IlmpStream;
class IlmpCommand;

// IlmpCallback. References to callbacks are kept in implementers of this structure. When
// data for a callback received, ::onData is invoked. When there are no more server-side
// references to a callback, it is destructed. Implementers can override the destructor to
// clean up any resources the callback logic might need.
class IlmpCallback {
	friend class IlmpCommand;

private:
	// for each ptr in ptrs: *ptr == this
	std::list<IlmpCallback**> ptrs;

public:
	IlmpStream *stream; // Weak ref

	int id; // cbid
	int pageviewId; // pvid

	IlmpCallback(IlmpStream* stream_, int pageviewId_) : stream(stream_), pageviewId(pageviewId_), id(0) {}

	virtual void onData(StringTokenWalker& params) { }
	virtual void onJsonData(const std::string& json) {
		std::cerr << "ILMP: Ignoring json data: " << json << std::endl;
	}

	void cancel();
	
	virtual ~IlmpCallback() {
#ifdef ILMPDEBUG
		std::cout << "Destroying IlmpCallback(id=" << id << "), referenced at " << ptrs.size() << " places" << std::endl;
#endif
		for (std::list<IlmpCallback**>::iterator it = ptrs.begin(); it != ptrs.end(); it++) **it = 0;
	}
};

// IlmpCallback wrapper for native function pointers expressed as boost::function. Ignores json data.
class IlmpCallbackNativeFunc : public IlmpCallback {
public:
	typedef boost::function<void(StringTokenWalker&)> NativeFunc;

	IlmpCallbackNativeFunc(IlmpStream* stream_, int pageviewId_, NativeFunc& func_) : IlmpCallback(stream_, pageviewId_), func(func_) {}

	void onData(StringTokenWalker& params) {
		func(params);
	}

private:
	NativeFunc func;
};

static int ids = 0;

// IlmpStream contains logic to communicate with an Implicit Link Comet Server.
//
// IlmpStream objects should be referenced through boost::smart_ptrs due to boost's
// implementation of sockets and other io_service services: _after_ destruction, the
// still-registered callbacks are invoked. Since these callbacks are bound to 
// instances of this class, we cannot use a normal destruction pattern. What
// ordinarily would have been destructor logic is now implemented in ::close.

class IlmpStream : boost::noncopyable, public boost::enable_shared_from_this<IlmpStream>
{
	friend class IlmpCommand;

private:
	boost::asio::io_service& ioService; 

	const std::string host;
	const std::string port;
	const std::string siteDir;

	typedef std::pair<int, IlmpCallback*> CallbackPair;
	typedef std::map<int, CallbackPair> CallbackMap;
	typedef std::map<int, CallbackMap> PageviewMap;
	PageviewMap callbacks;
		// pageviewId -> callbackId -> [refCount, callback]
	
	std::map<int, int> callbackAt;
		// pageviewId -> callbackAt

	bool pongWait;

	// In the current implementation, resolver, socket and pingTimer have a similar lifespan.
	tcp::resolver* resolver;
	tcp::socket* socket;
	boost::asio::deadline_timer* pingTimer;

	boost::asio::streambuf response;

	int respSeq;

public:
	boost::shared_ptr<IlmpStream> sharedPtr() {
		return shared_from_this();
	}

	boost::function<void()> onReady;
	boost::function<void(int,const std::string&)> onError;

	int id; // used for debugging

	IlmpStream(boost::asio::io_service& ioService, const std::string& _host, const std::string& _port = "80", const std::string& _siteDir = "") :
			host(_host), port(_port), ioService(ioService), siteDir(_siteDir == "" ? _host : _siteDir), wasConnected(false), pongWait(false), respSeq(0),
			resolver(0), socket(0), pingTimer(0) {
		static int ids = 0;
		id = ids++;
	}

	void connect()
	{
		close();

		resolver = new tcp::resolver(ioService);
		socket = new tcp::socket(ioService);
		pingTimer = new boost::asio::deadline_timer(ioService);

#ifdef ILMPDEBUG
		std::cout << id << ": Connecting to " << host << " port " << port << "\n";
#endif
		tcp::resolver::query query(host, port);
		resolver->async_resolve(query, boost::bind(&IlmpStream::onResolve, this->sharedPtr(),
				boost::asio::placeholders::error, boost::asio::placeholders::iterator)); 
	}

	void close() {
		if (resolver) {
			resolver->cancel();
			delete resolver;
			resolver = 0;
		}
		if (socket) {
			socket->close();
			delete socket;
			socket = 0;
#ifdef ILMPDEBUG
			std::cout << id << ": Closed stream\n";
#endif
		}
		if (pingTimer) {
			pingTimer->cancel();
			delete pingTimer;
			pingTimer = 0;
		}

		int i = 0;
		for (PageviewMap::iterator it = callbacks.begin(); it != callbacks.end(); it++) {
			for (CallbackMap::iterator it2 = (it->second).begin(); it2 != (it->second).end(); it2++) {
				delete (it2->second).second;
				i++;
			}
		}
		callbacks.clear();
		callbackAt.clear();
#ifdef ILMPDEBUG
		if (i > 0) std::cout << id << ": Deregistered " << i << " callbacks\n";
#endif
	}

#ifdef ILMPDEBUG
	~IlmpStream() {
		std::cout << id << ": Destroying IlmpStream object\n";
	}

	// Generates human-readable variant of given ILMP command.
	std::string readable(const std::string& ilmpData) const {
		std::stringstream r;
		for (std::string::const_iterator i = ilmpData.begin(); i != ilmpData.end(); i++) {
			if (*i >= '\000' && *i <= '\010') {
				char chr[4];
				snprintf(chr, 4, "%03d", int(*i));
				r << " [" << chr << "] ";
			} else
				r << *i;
		}
		return r.str();
	}

	// Dumps callbacks structure in readable format to std::cout.
	void debugCallbacks() const {
		std::cout << "\n----- CALLBACKS -----\n";
		for (PageviewMap::const_iterator i = callbacks.begin(); i != callbacks.end(); i++) {
			std::cout << "  pageviewId=" << (i->first) << ":\n";
			for (CallbackMap::const_iterator j = (i->second).begin(); j != (i->second).end(); j++) {
				std::cout << "    cbId=" << (j->first) << ", refCnt=" << (j->second).first << "\n";
			}
		}
		std::cout << "------- (end) -------\n\n";
	}
#endif

	// We take responsibility of destructing the IlmbCallback reference when the callback is no longer needed.
	int registerCallback(IlmpCallback* cb)
	{
		if (!cb->id)
			// Following js-implementation, just increment, starting at 1.
			cb->id = ++callbackAt[cb->pageviewId];
		
		callbacks[cb->pageviewId][cb->id] = CallbackPair(1, cb);
		return cb->id;
	}

	void cancelCallback(IlmpCallback* cb)
	{
		std::stringstream cmd;
		cmd << cb->pageviewId << '\002' << "C" << cb->id << '\001';
		write(cmd.str());
		
		delete callbacks[cb->pageviewId][cb->id].second;
		callbacks[cb->pageviewId].erase(cb->id);
	}

	bool wasConnected;

private:
	void write(const std::string& data)
	{
#ifdef ILMPDEBUG
		std::cout << " [ilmp:" << id << "] >> " << readable(data) << std::endl;
#endif

		if (!socket || !socket->is_open())
			return;
		
		std::string* _data = new std::string(data);

		boost::asio::async_write(*socket, boost::asio::buffer(*_data),
				boost::bind(&IlmpStream::onWritten, this->sharedPtr(), _data, boost::asio::placeholders::error));
	}

	void onWritten(std::string* dataBuf, const boost::system::error_code& err)
	{
		delete dataBuf;
		
		if (!socket || err == boost::asio::error::operation_aborted)
			return;
		else if (err) {
			std::stringstream msg; msg << "Error while writing: " << err.message();
			handleError(ILMPERR_NETWORK, msg.str());
			return;
		}
	}
	
	void onResolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_itr)
	{
#ifdef ILMPDEBUG
		std::cout << id << ": onResolve" << std::endl;
#endif
	
		if (!resolver || err == boost::asio::error::operation_aborted)
			return;
		else if (err) {
			std::stringstream msg; msg << "Unable to resolve hostname " << host << ": " << err.message();
			handleError(ILMPERR_NETWORK, msg.str());
			return;
		}
		
		tcp::endpoint endpoint = *endpoint_itr;
		socket->async_connect(endpoint, boost::bind(&IlmpStream::onConnect, this->sharedPtr(),
				boost::asio::placeholders::error, ++endpoint_itr)); 
	}
	
	void onConnect(const boost::system::error_code& err, tcp::resolver::iterator endpoint_itr)
	{
#ifdef ILMPDEBUG
		std::cout << id << ": onConnect" << std::endl;
#endif
		if (!socket || err == boost::asio::error::operation_aborted)
			return;
		else if (err && endpoint_itr != tcp::resolver::iterator()) {
			// Connection failed, but we can try the next endpoint.
			socket->close();
			tcp::endpoint endpoint = *endpoint_itr;
			std::cout << "Unable to connect to '" << endpoint << "'; trying next endpoint\n";
			socket->async_connect(endpoint, boost::bind(&IlmpStream::onConnect, this->sharedPtr(),
					boost::asio::placeholders::error, ++endpoint_itr));
			return;
		}
		else if (err) {
			std::cout << "Unable to connect to " << host << ":" << port << ": " << err.message();
			handleError(ILMPERR_NETWORK, err.message());
			return;
		}

		wasConnected = true;

		// Connected
		
		// Send post-connect gallantry
		std::string* req = new std::string("GET /ilcs? ILMP/" ILMP_VERSION "\n\n");
		boost::asio::async_write(*socket, boost::asio::buffer(*req),
				boost::bind(&IlmpStream::onWritten, this->sharedPtr(), req, boost::asio::placeholders::error));

		// Setup read callback
		boost::asio::async_read_until(*socket, response, '\001', boost::bind(&IlmpStream::onData,
				this->sharedPtr(), boost::asio::placeholders::error));
	
		// Schedule ping timer
		pingTimer->expires_from_now(boost::posix_time::seconds(ILMP_PING_INTERVAL));
		pingTimer->async_wait(boost::bind(&IlmpStream::onPingTimer,
				this->sharedPtr(), boost::asio::placeholders::error));

		if (onReady) onReady(); //ioService.post(onReady);
	}

	void onData(const boost::system::error_code& err)
	{
		if (!socket || err == boost::asio::error::operation_aborted)
			return;
		else if (err) {
			handleError(ILMPERR_NETWORK, "Error while reading data");
			return;
		}

		StreamTokenWalker commands(response, '\001');
		for (std::string command; commands.tryNext(command);) {

#ifdef ILMPDEBUG
			std::cout << " [ilmp:" << id << "] << " << readable(command) << "\n";
#endif
			
			StringTokenWalker tokens(command, '\002', true);
			int respId; tokens.next(respId);
			std::string pageviewId_; tokens.next(pageviewId_);

			if (respId != ++respSeq) {
				handleError(ILMPERR_PROTOCOL, "Response id sequence mismatch");
				return;
			}
			else if (pageviewId_ == "P") {
				// Pong
				pongWait = false;
				continue;
			}
			else if (pageviewId_ == "U") {
				// We need to update.
				std::cout << "Server instructed to update the client" << std::endl;
				std::string updateUrl; tokens.tryNext(updateUrl, "");
				handleError(ILMPERR_PROTOVER, updateUrl);
				return;
			}

			int pageviewId = atoi(pageviewId_.c_str());
			int callbackId; tokens.next(callbackId);
			std::string refUpdate; tokens.next(refUpdate);

			/*std::cout << "data: command\n"
				<< "         respId = " << respId << "\n"
				<< "     pageviewId = " << pageviewId << "\n"
				<< "     callbackId = " << callbackId << "\n"
				<< "      refUpdate = " << refUpdate << "\n";*/

			PageviewMap::iterator pvPtr = callbacks.find(pageviewId);

			if (pvPtr == callbacks.end()) {
#ifdef ILMPDEBUG
				std::cerr << "Ignoring unknown pageview " << pageviewId << " received in command '" << readable(command) << "'" << std::endl;
#else
				std::cerr << "Ignoring unknown pageview " << pageviewId << " received in command" << std::endl;
#endif
				continue;
			}

			CallbackMap& pvCallbacks = pvPtr->second;
				// Obtain value from key-value pair
			
			CallbackMap::iterator cbPtr = pvCallbacks.find(callbackId);
			
			if (cbPtr == pvCallbacks.end()) {
#ifdef ILMPDEBUG
				std::cerr << "Ignoring unknown callback " << callbackId << " for pageview " << pageviewId << " received in command '" << readable(command) << "'" << std::endl;
#else
				std::cerr << "Ignoring unknown callback " << callbackId << " for pageview " << pageviewId << " received in command" << std::endl;
#endif
				continue;
			}

			CallbackPair& cb = cbPtr->second;
				// Obtain value from key-value pair

			int& refCount = cb.first;
			IlmpCallback* theCallback = cb.second;

			for (std::string message; tokens.tryNext(message, "");) { // read messages 
				if (message.size() > 0 && message.at(0) == '\005') {
					// In the future, and when used extensively on larger json sets, we
					// might want to prevent the .substr() here.
					theCallback->onJsonData(message.substr(1));
				}
				else {
					StringTokenWalker params(message, '\004', true);
					theCallback->onData(params);
				}
			}

			if (refUpdate == "-") refCount--;
			else if (refUpdate == "+") refCount++;
			else if (refUpdate.size()) refCount = atoi(refUpdate.c_str());

			if (refCount <= 0) {
				//std::cout << "Refcount <= 0; destructing associated callback object\n";
				delete theCallback;
				pvCallbacks.erase(callbackId);
				if (pvCallbacks.empty()) callbacks.erase(pageviewId);
			}

			//debugCallbacks();
		}

		boost::asio::async_read_until(*socket, response, '\001', boost::bind(&IlmpStream::onData, this->sharedPtr(), boost::asio::placeholders::error));
	}
	
	void onPingTimer(const boost::system::error_code& err) {
		if (!pingTimer || err == boost::asio::error::operation_aborted)
			return;
		else if (err) {
			std::cerr << "Error when invoking ping timer callback: " << err << std::endl;
			return;
		}

		if (!socket || !socket->is_open()) {
			// If the connection is lost, we have nothing to do here.
			// The onConnect handler will reschedule us when we reconnect.
			return;
		}

		if (pongWait) {
			// We were still waiting on a pong for the previous ping.
			handleError(ILMPERR_NETWORK, "Ping/pong timeout");
			return;
		}

		write("P\001");
		pongWait = true;

		pingTimer->expires_from_now(boost::posix_time::seconds(ILMP_PING_INTERVAL));
		pingTimer->async_wait(boost::bind(&IlmpStream::onPingTimer, this->sharedPtr(), boost::asio::placeholders::error));
	}

	void handleError(int e, const std::string& str) {
		// Post to ioService, so any IlmpStream object may be destroyed by the error handler.
		if (onError)
			ioService.post(boost::bind(onError, e, str));
	}
};

void IlmpCallback::cancel() {
	stream->cancelCallback(this);
}

// JsonString is a string specialization that, when fed to IlmpCommand, is send as json. 
struct JsonString : public std::string {};

// IlmpCommand 
class IlmpCommand : boost::noncopyable 
{
private:
	IlmpStream* stream;
	int pageviewId;
	std::stringstream cmd;

public:
	IlmpCommand(IlmpStream* _stream, const std::string& _cmd, int _pageviewId = 1, const std::string& siteDir = "") :
		stream(_stream), pageviewId(_pageviewId), lastCb(0)
	{
		cmd << pageviewId << '\002' << "M" << (siteDir == "" ? stream->siteDir : siteDir) << "|" << _cmd; 
	}
	
	IlmpCommand& operator<<(int n) {
		cmd << '\003' << "j" << n;
		return *this;
	}

	IlmpCommand& operator<<(const JsonString& e) {
		std::string c(e);
		cmd << '\003' << "j" << escape(c);
		return *this;
	}

	IlmpCommand& operator<<(const std::string& s) {
		std::string c(s);
		cmd << '\003' << "p" << escape(c);
		return *this;
	}
	
	// Convenience for operator<<(IlmpCallback*) that accepts a boost::function to be wrapped
	// in a IlmpCallbackNativeFunc.
	IlmpCommand& operator<<(boost::function<void(StringTokenWalker&) > cb)
	{
		return operator<<(new IlmpCallbackNativeFunc(stream, pageviewId, cb));
	}

	IlmpCallback *lastCb;

	// The >> operator registers a IlmpCallback* pointer as 'wants to be reset when the
	// lastly-registered callback is destructed' and points the pointer to this callback.
	IlmpCommand& operator>>(IlmpCallback** cbPtr)
	{
		if (lastCb) {
			*cbPtr = lastCb;
			lastCb->ptrs.push_back(cbPtr);
		}
		return *this;
	}

	// We expect a heap-allocated IlmpCallback object. The IlmpStream will destruct it when
	// all channels are destroyed server-side.
	IlmpCommand& operator<<(IlmpCallback* c)
	{
		stream->registerCallback(c);
		cmd << '\003' << "c" << c->id;
		lastCb = c;

		return *this;
	}
	
	// This IlmpCommand object should not be used after send().
	void send()
	{
		cmd << '\001';
		stream->write(cmd.str());
	}

private:
	// Replaces \x00..\x05 with {\x05 [ascii representation of 0..5]}.
	std::string escape(std::string& s) {
		for (int i = 0; i < s.size(); i++) {
			if (s[i] >= '\x00' && s[i] <= '\x05') {
				char r[] = {'\x05', (char)(48+s[i])};
				s.replace(i, 1, r, 2);
				i++;
			}
		}
		return s;
	}
};


#endif
