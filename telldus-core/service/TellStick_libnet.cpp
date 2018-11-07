//
// Copyright (C) 2012 Telldus Technologies AB. All rights reserved.
//
// Copyright: See COPYING file that comes with this distribution
//
//
//#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <list>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "service/TellStick.h"
#include "service/Log.h"
#include "service/Settings.h"
#include "client/telldus-core.h"
#include "common/Thread.h"
#include "common/Mutex.h"
#include "common/Strings.h"
#include "common/common.h"

typedef struct _EVENT_HANDLE {
	pthread_cond_t eCondVar;
	pthread_mutex_t eMutex;
} EVENT_HANDLE;
typedef int DWORD;

class TellStick::PrivateData {
public:
	bool open;
	int vid, pid, sd, type;
	std::string serial, message, address;
	EVENT_HANDLE eh;
	bool running;
	TelldusCore::Mutex mutex;
};

TellStick::TellStick(int controllerId, TelldusCore::EventRef event, TelldusCore::EventRef updateEvent, const TellStickDescriptor &td )
	:Controller(controllerId, event, updateEvent) {
	d = new PrivateData;
	d->open = false;
	d->vid = td.vid;
	d->pid = td.pid;
	d->serial = td.serial;
	d->address = td.address;
	d->running = false;
	int ret = 0;
	Settings set;

	Log::notice("Connecting to TellStick (%X/%X) with serial %s", d->vid, d->pid, d->serial.c_str());

	d->open = true;
	d->sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (d->open && d->sd != -1) {
		struct sockaddr_in addr; // Make an endpoint
		memset(&addr, 0, sizeof addr);
		addr.sin_family = AF_INET;
		inet_pton(AF_INET, d->address.c_str(), &addr.sin_addr); // Set the  IP address
		addr.sin_port = htons(42314);
		char *tempMessage = "B:reglistener";
		ret = sendto(d->sd, tempMessage, strlen(tempMessage), 0, (struct sockaddr*)&addr, sizeof addr);

		this->start();
	} else {
		Log::warning("Failed to open TellStick");
	}
}

TellStick::~TellStick() {
	Log::warning("Disconnected TellStick (%X/%X) with serial %s", d->vid, d->pid, d->serial.c_str());
	if (d->running) {
		stop();
	}
	close(d->sd);
	delete d;
}

int TellStick::pid() const {
	return d->pid;
}

int TellStick::vid() const {
	return d->vid;
}

std::string TellStick::serial() const {
	return d->serial;
}

bool TellStick::isOpen() const {
	return d->open;
}

bool TellStick::isSameAsDescriptor(const TellStickDescriptor &td) const {
	if (td.vid != d->vid) {
		return false;
	}
	if (td.pid != d->pid) {
		return false;
	}
	if (td.serial != d->serial) {
		return false;
	}
	return true;
}

void TellStick::processData( const std::string &data ) {
	std::string modified = data.substr(10);
	size_t pos = modified.find(":");
	std::string finished;
	int checkeven = 0;

	int prevpos = 0;

//        Log::notice("processdata: %s", data.c_str());

	while(pos != std::string::npos) {// Magic for not modifying firmware too much..
		unsigned int x;
		std::stringstream ss;
		ss << std::hex << modified.substr(prevpos, pos);
		ss >> x;
		prevpos = pos + x + 1;
		finished.append(modified.substr(pos+1, x));
		if(!(checkeven %2 == 0)) {
			finished.append(";");
		} else {
			finished.append(":");
		}
		pos = modified.find(":", ++pos);
		checkeven++;
	}
	pos = modified.find(":datai");
	finished.append(modified.substr(pos+6,modified.size()-pos-6-2).c_str());
	finished.append(";");

	this->decodePublishData(finished);

}

int TellStick::reset() {
	int ret;
	if (d->open && d->sd != -1) {
		struct sockaddr_in addr; // Make an endpoint
		memset(&addr, 0, sizeof addr);
		addr.sin_family = AF_INET;
		inet_pton(AF_INET, d->address.c_str(), &addr.sin_addr); // Set the  IP address
		addr.sin_port = htons(42314);
		char *tempMessage = "A:disconnect";
		ret = sendto(d->sd, tempMessage, strlen(tempMessage), 0, (struct sockaddr*)&addr, sizeof addr);

		this->start();
	} else {
		Log::warning("Failed to reset TellStick");
	}	int success = -1;

	if(success < 0) {
		return TELLSTICK_ERROR_UNKNOWN;
	}
	return TELLSTICK_SUCCESS;
}

void TellStick::run() {
	unsigned char buf[1024];
	pthread_mutex_init(&d->eh.eMutex, NULL);
	pthread_cond_init(&d->eh.eCondVar, NULL);

	{
		TelldusCore::MutexLocker locker(&d->mutex);
		d->running = true;
	}

	int numbytes;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, d->address.c_str(), &addr.sin_addr); // Set the  IP address
	addr.sin_port = htons(42314);
	socklen_t addrsize = sizeof(addr);

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 100000;
	if (setsockopt(d->sd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
	    perror("Error");
	}

	while(1) {
		// Is there any better way then sleeping between reads?
		msleep(100);
		TelldusCore::MutexLocker locker(&d->mutex);
		if (!d->running) {
			break;
		}
		memset(buf, 0, sizeof(buf));
		numbytes = 0;

		if ((numbytes = recvfrom(d->sd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrsize)) == -1) {
			continue;
		}
		if (numbytes < 1) {
			continue;
		}
		//Log::debug("CLAES in run() recieved %d bytes with %s", numbytes, buf);

		processData( reinterpret_cast<char *>(&buf) );
	}
}
#include <algorithm>
#include <stdexcept>

std::string string_to_hex(const std::string& input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

int TellStick::send( const std::string &strMessage ) {
	std::string msg=strMessage;
	std::stringstream sm;
	int ret;

	if (!d->open) {
		return TELLSTICK_ERROR_NOT_FOUND;
	} else if (strMessage.compare("N+") == 0) {
		return TELLSTICK_SUCCESS;
	}

// urgh, convert T back to S message
	if (strMessage[0] == 'T') {
		int len=strMessage[5],idx,shift=6,pos=6;

		sm << 'S';
		while (len--) {
			idx=(strMessage[pos]>>shift) & 0x3;
			sm << strMessage[idx+1];
			shift-=2;
			if (shift<0) {
				shift=6;
				pos++;
			}
		}
		sm << '+';
		msg= sm.str();
	}

	std::string strMessage2 = msg.substr(1,msg.size());//-1
	int msgSize = strMessage2.size();
	std::stringstream out;
	out << std::hex << msgSize;
	std::string s = out.str();

	std::string compiledMessage = std::string("4:sendh1:S") + s + ":" + strMessage2;
	unsigned char *tempMessage = new unsigned char[compiledMessage.size()];
	memcpy(tempMessage, compiledMessage.c_str(), compiledMessage.size());

	// This lock does two things
	//  1 Prevents two calls from different threads to this function
	//  2 Prevents our running thread from receiving the data we are interested in here
	TelldusCore::MutexLocker locker(&d->mutex);
	struct sockaddr_in addr; // Make an endpoint
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, d->address.c_str(), &addr.sin_addr); // Set the  IP address
	addr.sin_port = htons(42314);

	ret = sendto(d->sd, tempMessage, compiledMessage.length(), 0, (struct sockaddr*)&addr, sizeof addr);
	if (ret<0) {
		return TELLSTICK_ERROR_COMMUNICATION;
	}
	return TELLSTICK_SUCCESS;
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

std::list<TellStickDescriptor> TellStick::findAll() {
	std::list<TellStickDescriptor> tellstick;
	int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sd<=0) {
		Log::debug("Could not open socket");
	}
	int broadcastEnable=1;
	int ret=setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
	if (ret) {
		Log::debug("Error: Could not open set socket to broadcast mode");
		close(sd);
	}
	 struct sockaddr_in broadcastAddr; // Make an endpoint
	memset(&broadcastAddr, 0, sizeof broadcastAddr);
	broadcastAddr.sin_family = AF_INET;
	inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr); // Set the broadcast IP address
	broadcastAddr.sin_port = htons(30303); // Set port 30303

	char *request = "D";
	ret = sendto(sd, request, strlen(request), 0, (struct sockaddr*)&broadcastAddr, sizeof broadcastAddr);
	if (ret<0) {
		Log::debug("Error: Could not open send broadcast");

	}
	else {
		char buf[2048];
		int numbytes=0;
		struct sockaddr_storage sender;
		socklen_t sendsize = sizeof(sender);
		bzero(&sender, sizeof(sender));

		char ipstr[INET6_ADDRSTRLEN];
		if ((numbytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr*)&sender, &sendsize)) == -1) {
			Log::debug("Failed to receive");
		}
		std::vector<std::string> x = split(std::string(buf), ':');
		if (sender.ss_family == AF_INET) {
			 inet_ntop(sender.ss_family,
					&(((struct sockaddr_in *)&sender)->sin_addr),
					ipstr,
					sizeof ipstr);
		} else {
			((struct sockaddr_in6 *)&sender)->sin6_addr;
		}
		TellStickDescriptor td;
		td.vid = 0x9999;
		td.pid = 0x9999;
		td.serial = x.at(2).c_str();
		td.address = std::string(ipstr);
		tellstick.push_back(td);

	        Log::notice("Found TellStick (%X/%X) with serial %s", td.vid, td.pid, td.serial.c_str());

	}
	close(sd);

	return tellstick;
}



bool TellStick::stillConnected() const {
	return d->open;
}

void TellStick::stop() {

	if (d->running) {
		{
			TelldusCore::MutexLocker locker(&d->mutex);
				d->running = false;
		}
		// Unlock the wait-condition

		pthread_cond_broadcast(&d->eh.eCondVar);
	}
	this->wait();
}
