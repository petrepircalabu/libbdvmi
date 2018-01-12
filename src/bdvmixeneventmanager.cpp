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

#include "bdvmi/statscollector.h"
#include "bdvmi/xendriver.h"
#include "bdvmi/xeneventmanager.h"
#include "bdvmi/eventhandler.h"
#include "bdvmi/loghelper.h"
#include <sys/mman.h>
#include <poll.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <xen/memory.h>

extern "C" {
#include <xen/xen-compat.h>
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x00040600
#error unsupported Xen version
#endif
}

#define GLA_VALID( x ) ( x.u.mem_access.flags & MEM_ACCESS_GLA_VALID )
#define ACCESS_R( x ) ( x.u.mem_access.flags & MEM_ACCESS_R )
#define ACCESS_W( x ) ( x.u.mem_access.flags & MEM_ACCESS_W )
#define ACCESS_X( x ) ( x.u.mem_access.flags & MEM_ACCESS_X )

#ifndef HVMOP_TRAP_sw_exc
#define HVMOP_TRAP_sw_exc 6
#endif

#if VM_EVENT_INTERFACE_VERSION > 0x00000001
#define RSP_DATA( x ) x.data.emul.read.data
#define RSP_DATA_SIZE( x ) x.data.emul.read.size
#else
#define RSP_DATA( x ) x.data.emul_read_data.data
#define RSP_DATA_SIZE( x ) x.data.emul_read_data.size
#endif

#if VM_EVENT_INTERFACE_VERSION > 0x00000002
#define MSR_NEW_VALUE( x) x.u.mov_to_msr.new_value
#else
#define MSR_NEW_VALUE( x) x.u.mov_to_msr.value
#endif

#define LOG_ERROR( x )                                                                                                 \
	{                                                                                                              \
		if ( logHelper_ )                                                                                      \
			logHelper_->error( x );                                                                        \
	}

#define LOG_DEBUG( x )                                                                                                 \
	{                                                                                                              \
		if ( logHelper_ )                                                                                      \
			logHelper_->debug( x );                                                                        \
	}

/* From xen/include/asm-x86/x86-defns.h */
#define X86_CR4_PGE        0x00000080 /* enable global pages */

/* From xen/include/asm-x86/msr-index.h */
#define MSR_IA32_SYSENTER_CS            0x00000174
#define MSR_IA32_SYSENTER_ESP           0x00000175
#define MSR_IA32_SYSENTER_EIP           0x00000176
#define MSR_IA32_CR_PAT                 0x00000277
#define MSR_IA32_MISC_ENABLE            0x000001a0
#define MSR_IA32_MC0_CTL                0x00000400

#define MSR_EFER                0xc0000080 /* extended feature register */
#define MSR_STAR                0xc0000081 /* legacy mode SYSCALL target */
#define MSR_LSTAR               0xc0000082 /* long mode SYSCALL target */
#define MSR_FS_BASE             0xc0000100 /* 64bit FS base */
#define MSR_GS_BASE             0xc0000101 /* 64bit GS base */
#define MSR_SHADOW_GS_BASE      0xc0000102 /* SwapGS GS shadow */

namespace bdvmi {

XenEventManager::XenEventManager( XenDriver &driver, LogHelper *logHelper, bool useAltP2m )
    : driver_( driver ), xci_( driver.nativeHandle() ), domain_( driver.id() ), stop_( false ), xce_( nullptr ),
      port_( -1 ), xsh_( nullptr ), evtchnPort_( 0 ), ringPage_( nullptr ), memAccessOn_( false ), evtchnOn_( false ),
      evtchnBindOn_( false ), guestStillRunning_( true ), logHelper_( logHelper ), firstReleaseWatch_( true ),
      firstXenServerWatch_( true ), useAltP2m_( useAltP2m )
{
	initXenStore();

#ifndef DISABLE_MEM_EVENT
	initAltP2m();
	initMemAccess();

#endif // DISABLE_MEM_EVENT

	if ( logHelper_ ) {
		std::stringstream ss;
		ss << "Running on Xen " << driver_.xenVersionMajor() << "."
			<< driver_.xenVersionMinor();

		logHelper_->info( ss.str() );
	}

#ifdef DEBUG_DUMP_EVENTS
	std::stringstream ss;
	ss << "/tmp/" << driver.uuid() << ".events";

	eventsFile_.open( ss.str().c_str(), std::ios_base::out | std::ios_base::trunc );
#endif
}

XenEventManager::~XenEventManager()
{
	handler( nullptr );
	stop();

	xc_monitor_software_breakpoint( xci_, domain_, 0 );

#if __XEN_LATEST_INTERFACE_VERSION__ >= 0x00040900
	xc_monitor_guest_request( xci_, domain_, false, true, true );
#else
	xc_monitor_guest_request( xci_, domain_, false, true );
#endif

	// cleanup events
	try {
		for ( int i = 0; i < 3; ++i )
			waitForEvents();
	} catch ( const std::exception &e ) {
		if ( logHelper_ )
			logHelper_->warning( e.what() );
	} catch ( ... ) {
		// std::runtime_errors not allowed to escape destructors
	}

	cleanup();
}

void XenEventManager::cleanup()
{
#ifndef DISABLE_MEM_EVENT
	if ( useAltP2m_ ) {
		unsigned int cpus = 0;
		driver_.cpuCount( cpus );

		for ( unsigned int vcpu = 0; vcpu < cpus; ++vcpu )
			xc_domain_debug_control( xci_, domain_, vcpu, XEN_DOMCTL_DEBUG_OP_SINGLE_STEP_OFF );
	}

	/* Tear down domain xenaccess in Xen */
	if ( ringPage_ )
		munmap( ringPage_, XC_PAGE_SIZE );

	if ( memAccessOn_ ) {
		xc_monitor_disable( xci_, domain_ );
	}

	// Unbind VIRQ
	if ( evtchnBindOn_ )
		xc_evtchn_unbind( xce_, port_ );

	if ( evtchnOn_ )
		xc_evtchn_close( xce_ );
#endif // DISABLE_MEM_EVENT

	if ( xsh_ ) {
		xs_unwatch( xsh_, "@releaseDomain", watchToken_.c_str() );
		xs_unwatch( xsh_, watchToken_.c_str(), watchToken_.c_str() );

		xs_unwatch( xsh_, controlXenStorePath_.c_str(), watchToken_.c_str() );
		xs_rm( xsh_, XBT_NULL, controlXenStorePath_.c_str() );

		xs_close( xsh_ );
	}
}

bool XenEventManager::enableMsrEvents( unsigned int msr, bool &oldValue )
{
	oldValue = ( enabledMsrs_.find( msr ) != enabledMsrs_.end() );

	if ( !oldValue ) { 
		enabledMsrs_.insert( msr );
#if VM_EVENT_INTERFACE_VERSION > 0x00000002
		return ( xc_monitor_mov_to_msr( xci_, domain_, msr, 1, true ) == 0 );
#else
		return ( xc_monitor_mov_to_msr( xci_, domain_, msr, 1 ) == 0 );
#endif
	}

	return true;
}

bool XenEventManager::disableMsrEvents( unsigned int msr, bool &oldValue )
{
	oldValue = ( enabledMsrs_.find( msr ) != enabledMsrs_.end() );

	if ( oldValue ) { 
		enabledMsrs_.erase( msr );
#if VM_EVENT_INTERFACE_VERSION > 0x00000002
		return ( xc_monitor_mov_to_msr( xci_, domain_, msr, 0, true ) == 0 );
#else
		return ( xc_monitor_mov_to_msr( xci_, domain_, msr, 0 ) == 0 );
#endif
	}

	return true;
}

bool XenEventManager::setCrEvents( unsigned int cr, bool enable )
{
	uint16_t index;
	bool retval;
#if XEN_DOMCTL_INTERFACE_VERSION >= 0x0000000e
	uint64_t bitmask = 0;
#endif

	if ( enable && enabledCrs_.find( cr ) != enabledCrs_.end() )
		return true; // Already enabled

	if ( !enable && enabledCrs_.find( cr ) == enabledCrs_.end() )
		return true; // Already disabled

	switch ( cr ) {
	case 0:
		index = VM_EVENT_X86_CR0;
		break;
	case 4:
		index = VM_EVENT_X86_CR4;
#if XEN_DOMCTL_INTERFACE_VERSION >= 0x0000000e
		bitmask = X86_CR4_PGE;
#endif
		break;
	case 3:
		index = VM_EVENT_X86_CR3;
		break;
	default:
		return false; // Unsupported CR index
	}

#if XEN_DOMCTL_INTERFACE_VERSION >= 0x0000000e
	retval = xc_monitor_write_ctrlreg( xci_, domain_, index, enable, 1, bitmask, 1 );
#else
	retval = xc_monitor_write_ctrlreg( xci_, domain_, index, enable, 1, 1 );
#endif

	if ( retval ) {
		LOG_ERROR( std::string( "[Xen events] could not set up CR" ) +
		           std::to_string( cr ) + " event handler" );
		return false;
	}

	if ( enable )
		enabledCrs_.insert( cr );
	else
		enabledCrs_.erase( cr );

	return true;
}

bool XenEventManager::enableCrEvents( unsigned int cr )
{
	return setCrEvents( cr, true );
}

bool XenEventManager::disableCrEvents( unsigned int cr )
{
	return setCrEvents( cr, false );
}

inline void copyRegisters( Registers &regs, const vm_event_request_t &req )
{
	regs.sysenter_cs = req.data.regs.x86.sysenter_cs;
	regs.sysenter_esp = req.data.regs.x86.sysenter_esp;
	regs.sysenter_eip = req.data.regs.x86.sysenter_eip;
	regs.msr_efer = req.data.regs.x86.msr_efer;
	regs.msr_star = req.data.regs.x86.msr_star;
	regs.msr_lstar = req.data.regs.x86.msr_lstar;
	regs.fs_base = req.data.regs.x86.fs_base;
	regs.gs_base = req.data.regs.x86.gs_base;
	/*
	regs.idtr_base    = req.data.regs.x86.idtr_base;
	regs.idtr_limit   = req.data.regs.x86.idtr_limit;
	regs.gdtr_base    = req.data.regs.x86.gdtr_base;
	regs.gdtr_limit   = req.data.regs.x86.gdtr_limit;
	*/
	regs.rflags = req.data.regs.x86.rflags;
	regs.rax = req.data.regs.x86.rax;
	regs.rcx = req.data.regs.x86.rcx;
	regs.rdx = req.data.regs.x86.rdx;
	regs.rbx = req.data.regs.x86.rbx;
	regs.rsp = req.data.regs.x86.rsp;
	regs.rbp = req.data.regs.x86.rbp;
	regs.rsi = req.data.regs.x86.rsi;
	regs.rdi = req.data.regs.x86.rdi;
	regs.r8 = req.data.regs.x86.r8;
	regs.r9 = req.data.regs.x86.r9;
	regs.r10 = req.data.regs.x86.r10;
	regs.r11 = req.data.regs.x86.r11;
	regs.r12 = req.data.regs.x86.r12;
	regs.r13 = req.data.regs.x86.r13;
	regs.r14 = req.data.regs.x86.r14;
	regs.r15 = req.data.regs.x86.r15;
	regs.rip = req.data.regs.x86.rip;
	regs.cr0 = req.data.regs.x86.cr0;
	regs.cr2 = req.data.regs.x86.cr2;
	regs.cr3 = req.data.regs.x86.cr3;
	regs.cr4 = req.data.regs.x86.cr4;

	regs.cs_arbytes = req.data.regs.x86.cs_arbytes;

	int32_t x86Mode = XenDriver::guestX86Mode( regs );

	switch ( x86Mode ) {
		case 2:
			regs.guest_x86_mode = Registers::CS_TYPE_16;
			break;
		case 4:
			regs.guest_x86_mode = Registers::CS_TYPE_32;
			break;
		case 8:
			regs.guest_x86_mode = Registers::CS_TYPE_64;
			break;
		default:
			regs.guest_x86_mode = Registers::ERROR;
			break;
	}
}

void XenEventManager::setRegisters( vm_event_response_t &rsp )
{
	XenDriver::DelayedWrite &dw = driver_.delayedWrite();

	if ( !dw.pending_ )
		return;

	rsp.data.regs.x86.rflags = dw.registers_.rflags;
	rsp.data.regs.x86.rax = dw.registers_.rax;
	rsp.data.regs.x86.rcx = dw.registers_.rcx;
	rsp.data.regs.x86.rdx = dw.registers_.rdx;
	rsp.data.regs.x86.rbx = dw.registers_.rbx;
	rsp.data.regs.x86.rsp = dw.registers_.rsp;
	rsp.data.regs.x86.rbp = dw.registers_.rbp;
	rsp.data.regs.x86.rsi = dw.registers_.rsi;
	rsp.data.regs.x86.rdi = dw.registers_.rdi;
	rsp.data.regs.x86.r8 = dw.registers_.r8;
	rsp.data.regs.x86.r9 = dw.registers_.r9;
	rsp.data.regs.x86.r10 = dw.registers_.r10;
	rsp.data.regs.x86.r11 = dw.registers_.r11;
	rsp.data.regs.x86.r12 = dw.registers_.r12;
	rsp.data.regs.x86.r13 = dw.registers_.r13;
	rsp.data.regs.x86.r14 = dw.registers_.r14;
	rsp.data.regs.x86.r15 = dw.registers_.r15;

	rsp.data.regs.x86.rip = dw.registers_.rip;

#ifdef VM_EVENT_FLAG_SET_REGISTERS
	rsp.flags |= VM_EVENT_FLAG_SET_REGISTERS;
#else
#warning "VM_EVENT_FLAG_SET_REGISTERS is not available, try a newer Xen!"
#endif

	dw.pending_ = false;
}

void XenEventManager::waitForEvents()
{
	EventHandler *h = handler();
	bool shuttingDown = false;

	for ( ;; ) {

		waitForEventOrTimeout( 100 );

		if ( sigStop_ && *sigStop_ )
			stop();

		if ( stop_ )
			shuttingDown = true;

#ifndef DISABLE_MEM_EVENT
		vm_event_request_t req;
		vm_event_response_t rsp;

		int events = 0;

		while ( RING_HAS_UNCONSUMED_REQUESTS( &backRing_ ) ) {
			getRequest( &req );

#ifdef DEBUG_DUMP_EVENTS
			eventsFile_.write( ( const char * )&req, sizeof( req ) );
#endif
			++events;

			StatsCollector::instance().incStat( "eventCount" );

			memset( &rsp, 0, sizeof( rsp ) );
			rsp.vcpu_id = req.vcpu_id;
			rsp.flags = req.flags & ~VM_EVENT_FLAG_ALTERNATE_P2M;
			rsp.reason = req.reason;
			rsp.altp2m_idx = req.altp2m_idx;
			rsp.data.regs.x86 = req.data.regs.x86;

			rsp.version = VM_EVENT_INTERFACE_VERSION;
			rsp.u.mem_access.flags = req.u.mem_access.flags;

			driver_.enableCache( req.vcpu_id );

			if ( h )
				h->runPreEvent();

			/*
			if ( stop_ && logHelper_ ) {
				std::stringstream ss;
				ss << "Got post-stop event (type: " << req.reason << ", handler: " << h << ")";
				logHelper_->debug(ss.str());
			}
			*/

			switch ( req.reason ) {

				case VM_EVENT_REASON_MEM_ACCESS: {
					Registers regs;
					uint32_t rspDataSize = sizeof( RSP_DATA( rsp ) );

					StatsCollector::instance().incStat( "eventsMemAccess" );

					copyRegisters( regs, req );

					rsp.flags |= VM_EVENT_FLAG_EMULATE;
					rsp.u.mem_access.gfn = req.u.mem_access.gfn;

					if ( h ) {
						uint64_t gva = 0;
						bool read = ( ACCESS_R( req ) != 0 );
						bool write = ( ACCESS_W( req ) != 0 );
						bool execute = ( ACCESS_X( req ) != 0 );
						HVAction action = NONE;
						unsigned short instructionSize = 0;

						if ( GLA_VALID( req ) )
							gva = req.u.mem_access.gla;

						uint64_t gpa = ( req.u.mem_access.gfn << XC_PAGE_SHIFT ) +
						               req.u.mem_access.offset;

						if ( req.u.mem_access.flags & MEM_ACCESS_FAULT_IN_GPT )
							break;

						h->handlePageFault( req.vcpu_id, regs, gpa, gva, read, write, execute,
						                    action, RSP_DATA( rsp ), rspDataSize,
						                    instructionSize );

						switch ( action ) {
							case EMULATE_NOWRITE:
#ifndef VM_EVENT_FLAG_SET_REGISTERS
							case SKIP_INSTRUCTION:
#endif
								rsp.flags |= VM_EVENT_FLAG_EMULATE_NOWRITE;
								break;
#ifdef VM_EVENT_FLAG_SET_REGISTERS
							case SKIP_INSTRUCTION:
								rsp.data.regs.x86.rip =
								        req.data.regs.x86.rip + instructionSize;
								rsp.flags |= VM_EVENT_FLAG_SET_REGISTERS;
								rsp.flags &= ~VM_EVENT_FLAG_EMULATE;
								break;
#endif
							case ALLOW_VIRTUAL:
								// go on, but don't emulate (monitoring
								// application
								// changed EIP)
								rsp.flags &= ~VM_EVENT_FLAG_EMULATE;
								break;

							case EMULATE_SET_CTXT:
								RSP_DATA_SIZE( rsp ) = rspDataSize;
								rsp.flags |= VM_EVENT_FLAG_SET_EMUL_READ_DATA;
								break;

							case NONE:
							default:
								if ( useAltP2m_ &&
								     req.flags & VM_EVENT_FLAG_ALTERNATE_P2M ) {
									rsp.flags = req.flags | VM_EVENT_FLAG_TOGGLE_SINGLESTEP;
									rsp.flags &= ~VM_EVENT_FLAG_EMULATE;
									rsp.altp2m_idx = 0;
								}
								break;
						}
					}

					break;
				}

				case VM_EVENT_REASON_SINGLESTEP:
					StatsCollector::instance().incStat( "eventsSingleStep" );

					if ( useAltP2m_ ) {
						rsp.reason = req.reason;
						rsp.flags |= VM_EVENT_FLAG_ALTERNATE_P2M | VM_EVENT_FLAG_TOGGLE_SINGLESTEP;
						rsp.altp2m_idx = driver_.altp2mViewId();
					}

					break;

				case VM_EVENT_REASON_WRITE_CTRLREG: {
					Registers regs;
					unsigned short crNumber = 3;

					StatsCollector::instance().incStat( "eventsWriteCtrlReg" );

					rsp.u.write_ctrlreg.index = req.u.write_ctrlreg.index;

					if ( req.u.write_ctrlreg.index == VM_EVENT_X86_XCR0 ) {
						if ( h )
							h->handleXSETBV( req.vcpu_id, req.u.write_ctrlreg.new_value );

						break;
					}

					switch ( req.u.write_ctrlreg.index ) {
						case VM_EVENT_X86_CR0:
							crNumber = 0;
							break;
						case VM_EVENT_X86_CR4:
							crNumber = 4;
							break;
						case VM_EVENT_X86_CR3:
						default:
							crNumber = 3;
							break;
					}

					copyRegisters( regs, req );

					if ( h ) {
						HVAction action = NONE;

						h->handleCR( req.vcpu_id, crNumber, regs, req.u.write_ctrlreg.old_value,
						             req.u.write_ctrlreg.new_value, action );

						if ( action == SKIP_INSTRUCTION || action == EMULATE_NOWRITE )
							rsp.flags |= VM_EVENT_FLAG_DENY;
					}

					break;
				}

				case VM_EVENT_REASON_MOV_TO_MSR:
					StatsCollector::instance().incStat( "eventsMovToMsr" );

					if ( h ) {
						HVAction action = NONE;

						auto i = msrOldValueCache_.find( req.vcpu_id );
						uint64_t oldValue;
#if VM_EVENT_INTERFACE_VERSION > 0x00000002
						oldValue = req.u.mov_to_msr.old_value;
#else

						if ( i == msrOldValueCache_.end() ) // not found
							oldValue = getMsr( req.vcpu_id, req.u.mov_to_msr.msr );
						else {
							auto j = i->second.find( req.u.mov_to_msr.msr );

							if ( j == i->second.end() ) // not found
								oldValue = getMsr( req.vcpu_id, req.u.mov_to_msr.msr );
							else
								oldValue = j->second;
						}
#endif

						h->handleMSR( req.vcpu_id, req.u.mov_to_msr.msr, oldValue,
						              MSR_NEW_VALUE( req ), action );

						if ( action == SKIP_INSTRUCTION || action == EMULATE_NOWRITE )
							rsp.flags |= VM_EVENT_FLAG_DENY;
#if VM_EVENT_INTERFACE_VERSION <= 0x00000002
						else
							msrOldValueCache_[req.vcpu_id][req.u.mov_to_msr.msr] =
								req.u.mov_to_msr.value;
#endif
					}

					break;

				case VM_EVENT_REASON_GUEST_REQUEST: {
					StatsCollector::instance().incStat( "eventsGuestRequest" );

					if ( h ) {
						Registers regs;
						copyRegisters( regs, req );

						h->handleVMCALL( req.vcpu_id, regs );
					}

					break;
				}

				case VM_EVENT_REASON_SOFTWARE_BREAKPOINT: {
					StatsCollector::instance().incStat( "eventsBreakPoint" );

					bool reinject = ( h != nullptr );

					if ( h ) {
						Registers regs;
						copyRegisters( regs, req );

						reinject = !h->handleBreakpoint( req.vcpu_id, regs,
						                                 req.u.software_breakpoint.gfn );
					}

					if ( reinject ) {
						if ( xc_hvm_inject_trap( xci_, domain_, req.vcpu_id, 3,
						                         HVMOP_TRAP_sw_exc, ~0u, 1, 0 ) < 0 ) {

							if ( logHelper_ )
								logHelper_->error( "Could not reinject breakpoint" );
						} else {
							if ( logHelper_ ) {
								std::stringstream ss;
								ss << "Reinjecting breakpoint (VCPU: " << req.vcpu_id
									<< ", GFN: " << std::showbase << std::hex
									<< req.u.software_breakpoint.gfn
									<< ", RIP: " << req.data.regs.x86.rip << ")";
								logHelper_->warning( ss.str() );
							}
						}
					}

					break;
				}

#ifdef VM_EVENT_REASON_INTERRUPT
				case VM_EVENT_REASON_INTERRUPT:
					if ( h ) {
						Registers regs;
						copyRegisters( regs, req );

						h->handleInterrupt( req.vcpu_id, regs, req.u.interrupt.x86.vector,
						                    req.u.interrupt.x86.error_code, req.u.interrupt.x86.cr2 );
					}

					break;
#endif
				default:
					// unknown reason code
					break;
			}

			if ( driver_.pendingInjection( req.vcpu_id ) ) {
#ifdef VM_EVENT_REASON_INTERRUPT
				rsp.flags |= VM_EVENT_FLAG_GET_NEXT_INTERRUPT;
#else
#warning "VM_EVENT_REASON_INTERRUPT is not available, try a newer Xen!"
#endif
				driver_.clearInjection( req.vcpu_id );
			}

			setRegisters( rsp );
			driver_.flushPageProtections();

			if ( h )
				h->runPostEvent();

			driver_.disableCache();

			/* Put the page info on the ring */
			putResponse( &rsp );
			resumePage();
		}

		// if ( events )
		//	resumePage();
#endif // DISABLE_MEM_EVENT

		if ( shuttingDown )
			return;
	}
}

void XenEventManager::stop()
{
	if ( stop_ ) // It's already been called
		return;

	EventHandler *h = handler();

	if ( h )
		h->handleSessionOver( guestStillRunning_ );

	stop_ = true;

#if XEN_DOMCTL_INTERFACE_VERSION >= 0x0000000e
	xc_monitor_write_ctrlreg( xci_, domain_, VM_EVENT_X86_XCR0, 0, 1, 0, 1 );
#else
	xc_monitor_write_ctrlreg( xci_, domain_, VM_EVENT_X86_XCR0, 0, 1, 1 );
#endif

#ifndef DISABLE_MEM_EVENT
	disableCrEvents( 0 );
	disableCrEvents( 3 );
	disableCrEvents( 4 );
#endif // DISABLE_MEM_EVENT
}

void XenEventManager::initXenStore()
{
	xsh_ = xs_open( 0 );

	if ( !xsh_ )
		throw std::runtime_error( "[Xen events] xs_open() failed" );

	std::stringstream ss;
	ss << "/local/domain/0/device-model/" << domain_;

	watchToken_ = ss.str();

	ss.str("");
	ss << "/local/domain/" << domain_ << "/vm-data/introspection-control";

	controlXenStorePath_ = ss.str();

	std::string value = "started";

	if ( !xs_write( xsh_, XBT_NULL, controlXenStorePath_.c_str(), value.c_str(), value.length() ) ) {
		if ( logHelper_ )
			logHelper_->error( std::string("Could not write XenStore key ") + controlXenStorePath_ );
	}

	if ( !xs_watch( xsh_, "@releaseDomain", watchToken_.c_str() ) ||
	     !xs_watch( xsh_, watchToken_.c_str(), watchToken_.c_str() ) ||
	     !xs_watch( xsh_, controlXenStorePath_.c_str(), watchToken_.c_str() ) ) {
		xs_close( xsh_ );
		throw std::runtime_error( "[Xen events] xs_watch() failed" );
	}
}

void XenEventManager::initEventChannels()
{
	/* Open event channel */
	xce_ = xc_evtchn_open( nullptr, 0 );

	if ( !xce_ ) {
		cleanup();
		throw std::runtime_error( "[Xen events] failed to open event channel" );
	}

	evtchnOn_ = true;

	/* Bind event notification */
	port_ = xc_evtchn_bind_interdomain( xce_, domain_, evtchnPort_ );

	if ( port_ < 0 ) {
		cleanup();
		throw std::runtime_error( "[Xen events] failed to bind event channel" );
	}

	evtchnBindOn_ = true;

/* Initialise ring */
#define private rprivate
	SHARED_RING_INIT( ( vm_event_sring_t * )ringPage_ );
	BACK_RING_INIT( &backRing_, ( vm_event_sring_t * )ringPage_, XC_PAGE_SIZE );
#undef private
}

void XenEventManager::initMemAccess()
{
	ringPage_ = xc_monitor_enable( xci_, domain_, &evtchnPort_ );

	if ( ringPage_ == nullptr ) {
		cleanup();

		switch ( errno ) {
			case EBUSY:
				throw std::runtime_error(
				        "[Xen events] the domain is either already connected "
				        "with a monitoring application, or such an application crashed after "
				        "connecting to it" );
			case ENODEV:
				throw std::runtime_error( "[Xen events] EPT not supported for this guest" );
			default:
				throw std::runtime_error( std::string( "[Xen events] error initialising shared page: " )
					+ strerror( errno ) );
		}
	}

	memAccessOn_ = true;

	initEventChannels();

	xc_domain_set_access_required( xci_, domain_, 0 );


#if __XEN_LATEST_INTERFACE_VERSION__ >= 0x00040900
	xc_monitor_guest_request( xci_, domain_, true, true, true );
#else
	xc_monitor_guest_request( xci_, domain_, true, true );
#endif
#if XEN_DOMCTL_INTERFACE_VERSION >= 0x0000000e
	xc_monitor_write_ctrlreg( xci_, domain_, VM_EVENT_X86_XCR0, 1, 1, 0, 1 );
#else
	xc_monitor_write_ctrlreg( xci_, domain_, VM_EVENT_X86_XCR0, 1, 1, 1 );
#endif
	xc_monitor_software_breakpoint( xci_, domain_, 1 );
}

void XenEventManager::initAltP2m()
{
	if ( !useAltP2m_ )
		return;

	if ( xc_monitor_singlestep( xci_, domain_, 1 ) < 0 ) {
		cleanup();
		throw std::runtime_error( "[ALTP2M] could not enable singlestep monitoring" );
	}
}

int XenEventManager::waitForEventOrTimeout( int ms )
{
#ifndef DISABLE_MEM_EVENT
	struct pollfd fd[2];

	fd[0].fd = xs_fileno( xsh_ );
	fd[0].events = POLLIN | POLLERR;
	fd[1].fd = xc_evtchn_fd( xce_ );
	fd[1].events = POLLIN | POLLERR;

	int rc = poll( fd, 2, ms );
#else
	struct pollfd fd[1];

	fd[0].fd = xs_fileno( xsh_ );
	fd[0].events = POLLIN | POLLERR;

	int rc = poll( fd, 1, ms );
#endif

	if ( rc == 0 ) // poll() timeout
		return 0;

	if ( rc < 0 ) {
		if ( errno == EINTR ) // interrupted by signal
			return 0;

		throw std::runtime_error( "[Xen events] poll() failed" );
	}

	if ( fd[0].revents & POLLIN ) { // a XenStore event

		unsigned int num;
		char **vec = xs_read_watch( xsh_, &num );

		if ( vec && watchToken_ == vec[XS_WATCH_TOKEN] ) {
			/* Our domain is being shut down */

			if ( watchToken_ == vec[XS_WATCH_PATH] ) {

				if ( firstReleaseWatch_ ) {
					// Ignore first triggered watch, xs_watch() does that.
					firstReleaseWatch_ = false;
				} else {

					unsigned int len = 0;
					xs_transaction_t th = xs_transaction_start( xsh_ );
					void *buf = xs_directory( xsh_, th, vec[XS_WATCH_PATH], &len );

					if ( !buf ) {
						guestStillRunning_ = ( xs_is_domain_introduced( xsh_, domain_ ) != 0 );
						stop();
					}

					free( buf );
					xs_transaction_end( xsh_, th, 0 );
				}
			} else if ( vec && controlXenStorePath_ == vec[XS_WATCH_PATH] ) {

				if ( firstXenServerWatch_ ) {
					// Ignore first triggered watch, xs_watch() does that.
					firstXenServerWatch_ = false;
				} else {
					char *value = static_cast<char *>(
					        xs_read_timeout( xsh_, XBT_NULL, vec[XS_WATCH_PATH], nullptr, 1 ) );

					if ( value ) {
						std::string tmp = value;
						free( value );

						if ( logHelper_ )
							logHelper_->info( std::string( "Received control command: " ) + tmp );

						if ( tmp == "shutdown" ) {
							guestStillRunning_ = true;
							stop();
						}
					}
				}
			} else if ( vec && std::string( "@releaseDomain" ) == vec[XS_WATCH_PATH] ) {
				if ( !xs_is_domain_introduced( xsh_, domain_ ) ) {
					guestStillRunning_ = false;
					stop();
				}
			}

			free( vec );
			return 0;
		}

		free( vec );
	}

#ifndef DISABLE_MEM_EVENT
	if ( fd[1].revents & POLLIN ) { // a vm_event
		int port = xc_evtchn_pending( xce_ );

		if ( port == -1 )
			throw std::runtime_error( "[Xen events] failed to read port from event channel" );

		if ( xc_evtchn_unmask( xce_, port ) != 0 )
			throw std::runtime_error( "[Xen events] failed to unmask event channel port" );

		return port;
	}
#endif

	// shouldn't be here
	throw std::runtime_error( "[Xen events] error getting event" );
}

void XenEventManager::getRequest( vm_event_request_t *req )
{
	vm_event_back_ring_t *back_ring;
	RING_IDX req_cons;

	back_ring = &backRing_;
	req_cons = back_ring->req_cons;

	/* Copy request */
	memcpy( req, RING_GET_REQUEST( back_ring, req_cons ), sizeof( *req ) );
	++req_cons;

	/* Update ring */
	back_ring->req_cons = req_cons;
	back_ring->sring->req_event = req_cons + 1;
}

void XenEventManager::putResponse( vm_event_response_t *rsp )
{
	vm_event_back_ring_t *back_ring;
	RING_IDX rsp_prod;

	back_ring = &backRing_;
	rsp_prod = back_ring->rsp_prod_pvt;

	/* Copy response */
	memcpy( RING_GET_RESPONSE( back_ring, rsp_prod ), rsp, sizeof( *rsp ) );
	++rsp_prod;

	/* Update ring */
	back_ring->rsp_prod_pvt = rsp_prod;
	RING_PUSH_RESPONSES( back_ring );
}

void XenEventManager::resumePage()
{
	/* Tell Xen page is ready */
	// xc_monitor_resume(xci_, domain_);

	if ( xc_evtchn_notify( xce_, port_ ) < 0 )
		throw std::runtime_error( "[Xen events] error resuming page" );
}

std::string XenEventManager::uuid()
{
	return driver_.uuid();
}

uint64_t XenEventManager::getMsr( unsigned short vcpu, uint32_t msr ) const
{
	bdvmi::Registers regs;

	if ( !driver_.registers( vcpu, regs ) )
		return 0;

	switch ( msr ) {
		case MSR_IA32_SYSENTER_CS:
			return regs.sysenter_cs;
		case MSR_IA32_SYSENTER_ESP:
			return regs.sysenter_esp;
		case MSR_IA32_SYSENTER_EIP:
			return regs.sysenter_eip;
		case MSR_EFER:
			return regs.msr_efer;
		case MSR_LSTAR:
			return regs.msr_lstar;
		case MSR_FS_BASE:
			return regs.fs_base;
		case MSR_GS_BASE:
			return regs.gs_base;
		case MSR_STAR:
			return regs.msr_star;
		case MSR_IA32_CR_PAT:
			return regs.msr_pat;
		case MSR_SHADOW_GS_BASE:
			return regs.shadow_gs;
		case MSR_IA32_MISC_ENABLE:
		case MSR_IA32_MC0_CTL:
		default:
			return 0;
	}
}

} // namespace bdvmi
