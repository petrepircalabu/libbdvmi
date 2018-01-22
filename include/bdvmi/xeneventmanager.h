// Copyright (c) 2015-2017 Bitdefender SRL, All rights reserved.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library.

#ifndef __BDVMIXENEVENTMANAGER_H_INCLUDED__
#define __BDVMIXENEVENTMANAGER_H_INCLUDED__

#include "eventmanager.h"
#include <fstream>
#include <stdint.h>
#include <string>

extern "C" {
#include <xen/xen-compat.h>
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x00040600
#error unsupported Xen version
#endif

#include <xenstore.h>
#include <xenctrl.h>
#include <xen/hvm/save.h>

#define private rprivate /* private is a C++ keyword */
#include <xen/vm_event.h>
#undef private
}

#include "xeninlines.h"
#include "xencontrol.h"

namespace bdvmi {

class XenDriver;
class LogHelper;

class XenEventManager : public EventManager {

public:
	XenEventManager( XenDriver &driver, LogHelper *logHelper, bool useAltP2m = false );

	virtual ~XenEventManager();

public:
	// Loop waiting for events
	virtual void waitForEvents();

	// Stop the event loop
	virtual void stop();

	virtual bool enableMsrEvents( unsigned int msr, bool &oldValue );

	virtual bool disableMsrEvents( unsigned int msr, bool &oldValue );

	virtual bool enableCrEvents( unsigned int cr );

	virtual bool disableCrEvents( unsigned int cr );

private:
	void initXenStore();

	void initEventChannels();

	void initMemAccess();

	void initAltP2m();

	int waitForEventOrTimeout( int ms );

	void getRequest( vm_event_request_t *req );

	void putResponse( vm_event_response_t *rsp );

	void resumePage();

	std::string uuid();

	void cleanup();

	void setRegisters( vm_event_response_t &rsp );

	bool setCrEvents( unsigned int cr, bool enable );

	uint64_t getMsr( unsigned short vcpu, uint32_t msr ) const;

private:
	// Don't allow copying for these objects
	XenEventManager( const XenEventManager & );

	// Don't allow copying for these objects
	XenEventManager &operator=( const XenEventManager & );

private:
	XenDriver &driver_;
	xc_interface *xci_;
	domid_t domain_;
	bool stop_;
	xc_evtchn *xce_;
	int port_;
	xs_handle *xsh_;
	uint32_t evtchnPort_;
	vm_event_back_ring_t backRing_;
	void *ringPage_;
	std::string watchToken_;
	std::string controlXenStorePath_;
	bool memAccessOn_;
	bool evtchnOn_;
	bool evtchnBindOn_;
	bool guestStillRunning_;
	LogHelper *logHelper_;
	bool firstReleaseWatch_;
	bool firstXenServerWatch_;
	bool useAltP2m_;
	std::set<unsigned int> enabledCrs_;
	std::set<unsigned int> enabledMsrs_;

	typedef std::map<uint32_t, uint64_t>                msrs_values_map_t;
	typedef std::map<unsigned short, msrs_values_map_t> vcpu_msrs_t;
	vcpu_msrs_t msrOldValueCache_;

#ifdef DEBUG_DUMP_EVENTS
	std::ofstream eventsFile_;
#endif
	std::function<int(uint32_t, uint32_t)> debug_control_;
	std::function<int(unsigned int)> set_access_required_;
};

} // namespace bdvmi

#endif // __BDVMIXENEVENTMANAGER_H_INCLUDED__
