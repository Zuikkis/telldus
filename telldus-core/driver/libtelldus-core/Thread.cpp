//
// C++ Implementation: Thread
//
// Description: 
//
//
// Author: Micke Prag <micke.prag@telldus.se>, (C) 2009
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include "Thread.h"
#ifdef _WINDOWS
	#include <windows.h>
#else
	#include <pthread.h>
#endif

using namespace TelldusCore;

class TelldusCore::ThreadPrivate {
public:
#ifdef _WINDOWS
	HANDLE thread;
	DWORD threadId;
#else
	pthread_t thread;
#endif
};

Thread::Thread() {
	d = new ThreadPrivate;
}

Thread::~Thread() {
	delete d;
}

void Thread::start() {
#ifdef _WINDOWS
	d->thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&Thread::exec, this, 0, &d->threadId);
#else
	pthread_create(&d->thread, NULL, &Thread::exec, this );
#endif
}

bool Thread::wait() {
#ifdef _WINDOWS
	WaitForSingleObject(d->thread, INFINITE);
	CloseHandle(d->thread);
#else
	pthread_join(d->thread, 0);
#endif
	return true;
}

void *Thread::exec( void *ptr ) {
	Thread *t = reinterpret_cast<Thread *>(ptr);
	if (t) {
		t->run();
	}
	return 0;
}