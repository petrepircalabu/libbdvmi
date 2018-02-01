// Copyright (c) 2018 Bitdefender SRL, All rights reserved.
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

#include "bdvmi/xencontrol.h"
#include "bdvmi/statscollector.h"
#include "bdvmi/driver.h"
#include <dlfcn.h>
#include <type_traits>
#include <xenctrl.h>
#include <xen/xen.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace bdvmi {

using namespace std::placeholders;

DomInfo::DomInfo(): pimpl_(new xc_dominfo_t)
{
}

DomInfo::~DomInfo()
{
	delete pimpl_;
}

uint32_t DomInfo::domid() const
{
	return pimpl_->domid;
}

bool DomInfo::hvm() const
{
	return pimpl_->hvm != 0;
}

unsigned int DomInfo::max_vcpu_id() const
{
	return pimpl_->max_vcpu_id;
}

template <>
struct DomainGetInfo<int(uint32_t domid, DomInfo& domInfo)>

{
public:
	DomainGetInfo(const std::function< int(uint32_t, unsigned int, xc_dominfo_t*) > &fun):
		fun_(fun)
	{
	}

	int operator()(uint32_t domid, DomInfo& domInfo)
	{
		StatsCollector::instance().incStat( "xcDomainInfo" );
		return fun_(domid, 1, domInfo.pimpl_);
	}
private:
	const std::function< int(uint32_t, unsigned int, xc_dominfo_t*) > fun_;
};

XenDomainHandle::XenDomainHandle(const xen_domain_handle_t &handle):
	uuid_(XenDomainHandle::UuidToString(handle))
{
}

XenDomainHandle::XenDomainHandle(const std::string &str):
	uuid_(str)
{
}

std::string XenDomainHandle::UuidToString(const xen_domain_handle_t &handle)
{
	std::stringstream ss;
	ss.setf( std::ios::hex, std::ios::basefield );

	for ( int i = 0; i < 4; ++i ) {
		ss << ( handle[i] >> 4 );
		ss << ( handle[i] & 0x0f );
	}

	ss << '-';

	for ( int i = 4; i < 6; ++i ) {
		ss << ( handle[i] >> 4 );
		ss << ( handle[i] & 0x0f );
	}

	ss << '-';

	for ( int i = 6; i < 8; ++i ) {
		ss << ( handle[i] >> 4 );
		ss << ( handle[i] & 0x0f );
	}

	ss << '-';

	for ( int i = 8; i < 10; ++i ) {
		ss << ( handle[i] >> 4 );
		ss << ( handle[i] & 0x0f );
	}

	ss << '-';

	for ( int i = 10; i < 16; ++i ) {
		ss << ( handle[i] >> 4 );
		ss << ( handle[i] & 0x0f );
	}

	return ss.str();
}

bool XenDomainHandle::operator==(const std::string &str) const
{
	return uuid_ == str;
}

std::ostream& operator<<(std::ostream &out, const XenDomainHandle& uuid)
{
	out << uuid.uuid_;
	return out;
}

class XenControlFactory
{
public:
	static XenControlFactory& instance();

private:
	XenControlFactory( );
	~XenControlFactory( );

public:
	template < typename T >
	std::function<T> lookup( const std::string &name, bool required ) const;

	auto getDomainPause() const -> DomainPauseFunc;
	auto getDomainUnpause() const -> DomainUnpauseFunc;
	auto getDomainShutdown() const -> DomainShutdownFunc;
	auto getDomainGetInfo() const -> DomainGetInfoFunc;
	auto getDomainMaximumGpfn() const -> DomainMaximumGpfnFunc;
	auto getDomainDebugControl() const -> DomainDebugControlFunc;
	auto getDomainGetTscInfo() const -> DomainGetTscInfoFunc;
	auto getDomainSetAccessRequired() const -> DomainSetAccessRequiredFunc;
	auto getDomainHvmGetContext() const -> DomainHvmGetContextFunc;
	auto getDomainHvmGetContextPartial() const -> DomainHvmGetContextPartialFunc;
	auto getVcpuGetContext() const -> std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)>;
	auto getVcpuSetContext() const -> std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)>;
	auto getSetMemAccess() const -> std::function<int(uint32_t, const std::map<unsigned long, xenmem_access_t>&)>;
	auto getAltp2mSetMemAccess() const -> std::function<int(uint32_t, uint16_t, const std::map<unsigned long, xenmem_access_t>&)>;
	auto getAltp2mSetDomainState() const -> Altp2mSetDomainStateFunc;
	auto getAltp2mCreateView() const -> Altp2mCreateViewFunc;
	auto getAltp2mDestroyView() const -> Altp2mDestroyViewFunc;

	std::pair< int, int > getVersion() const;
	const std::string getCaps() const;
	const XenDomainHandle getUuid() const;

protected:
	xc_interface* getInterface() const
	{
		return xci_;
	}

private:
	void *libxcHandle_;
	xc_interface *xci_;

	std::function < decltype(xc_interface_open)  > interfaceOpen_;
	std::function < decltype(xc_interface_close) > interfaceClose_;
	std::function < decltype(xc_version)         > version_;
};

template < typename T>
std::function<T> XenControlFactory::lookup( const std::string &name, bool required ) const
{
	std::function<T> func = reinterpret_cast< T* >( ::dlsym( libxcHandle_, name.c_str() ) );
	if ( required && !func )
		throw std::runtime_error( "Failed to get the \"" + name + "\" function" );
	return func;
}

auto XenControlFactory::getDomainPause() const -> DomainPauseFunc
{
	static_assert( std::is_same <xc_domain_pause_func_t , decltype(xc_domain_pause)>::value , "" );

	auto f = lookup< decltype(xc_domain_pause) > ( "xc_domain_pause", true);
	return std::bind(f, getInterface(), _1);
}

auto XenControlFactory::getDomainUnpause() const -> DomainUnpauseFunc
{
	static_assert( std::is_same <xc_domain_unpause_func_t , decltype(xc_domain_unpause)>::value , "" );

	auto f = lookup< decltype(xc_domain_pause) > ( "xc_domain_unpause", true);
	return std::bind(f, getInterface(), _1);
}

auto XenControlFactory::getDomainShutdown() const -> DomainShutdownFunc
{
	static_assert( std::is_same <xc_domain_shutdown_func_t , decltype(xc_domain_shutdown)>::value , "" );

	auto f = lookup< decltype(xc_domain_shutdown) > ( "xc_domain_shutdown", true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getDomainGetInfo() const -> DomainGetInfoFunc
{
	auto f = lookup< decltype(xc_domain_getinfo) > ( "xc_domain_getinfo", true);
	return DomainGetInfoFunc(std::bind(f, getInterface(), _1, _2, _3));
}


auto XenControlFactory::getDomainMaximumGpfn() const -> DomainMaximumGpfnFunc
{
	static_assert( std::is_same <xc_domain_maximum_gpfn_func_t , decltype(xc_domain_maximum_gpfn)>::value , "" );

	auto f = lookup< decltype(xc_domain_maximum_gpfn) > ("xc_domain_maximum_gpfn" , true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getDomainDebugControl() const -> DomainDebugControlFunc
{
	static_assert( std::is_same <xc_domain_debug_control_func_t, decltype(xc_domain_debug_control)>::value, "");

	auto f = lookup< decltype(xc_domain_debug_control) > ("xc_domain_debug_control", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

auto XenControlFactory::getDomainGetTscInfo() const -> DomainGetTscInfoFunc
{
	static_assert( std::is_same <xc_domain_get_tsc_info_func_t, decltype(xc_domain_get_tsc_info)>::value, "");
	auto f = lookup< decltype(xc_domain_get_tsc_info) > ("xc_domain_get_tsc_info", true);
	return std::bind(f, getInterface(), _1, _2, _3, _4, _5);
}

auto XenControlFactory::getDomainSetAccessRequired() const -> DomainSetAccessRequiredFunc
{
	static_assert( std::is_same <xc_domain_set_access_required_func_t, decltype(xc_domain_set_access_required)>::value, "");
	auto f = lookup< decltype(xc_domain_set_access_required) > ("xc_domain_set_access_required", true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getDomainHvmGetContext() const -> DomainHvmGetContextFunc
{
	static_assert( std::is_same <xc_domain_hvm_getcontext_func_t, decltype(xc_domain_hvm_getcontext)>::value, "");
	auto f = lookup< decltype(xc_domain_hvm_getcontext) > ("xc_domain_hvm_getcontext", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

auto XenControlFactory::getDomainHvmGetContextPartial() const -> DomainHvmGetContextPartialFunc
{
	static_assert( std::is_same <xc_domain_hvm_getcontext_partial_func_t, decltype(xc_domain_hvm_getcontext_partial)>::value, "");
	auto f = lookup< decltype(xc_domain_hvm_getcontext_partial) > ("xc_domain_hvm_getcontext_partial", true);
	return std::bind(f, getInterface(), _1, _2, _3, _4, _5);
}

auto XenControlFactory::getVcpuGetContext() const -> std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)>
{
	auto f = lookup<decltype(xc_vcpu_getcontext)> ("xc_vcpu_getcontext", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

auto XenControlFactory::getVcpuSetContext() const -> std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)>
{
	auto f = lookup<decltype(xc_vcpu_setcontext)> ("xc_vcpu_setcontext", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

using SetMemAccessMultiFunc = std::function< remove_first_arg< decltype(xc_set_mem_access_multi) >::type >;
using SetMemAccessLegacyFunc = std::function< remove_first_arg< decltype(xc_set_mem_access) >::type >;

template <typename T>
using SetMemAccessImpl = AdapterFun<T, int(uint32_t domain, const std::map<unsigned long, xenmem_access_t> &access)> ;

template <>
int SetMemAccessImpl<SetMemAccessMultiFunc>::operator()(uint32_t domain, const std::map<unsigned long, xenmem_access_t> &access) const
{
	std::vector<uint8_t> access_type;
	std::vector<uint64_t> gfns;

	for ( auto &&item : access) {
		access_type.push_back( item.second );
		gfns.push_back( item.first );
	}
	return f_(domain, &access_type[0], &gfns[0], gfns.size());
}

template <>
int SetMemAccessImpl<SetMemAccessLegacyFunc>::operator()(uint32_t domain, const std::map<unsigned long, xenmem_access_t> &access) const
{
	for ( auto &&item : access)
		f_(domain, item.second, item.first, 1);
	return 0; //FIXME: value is ignored in the original code
}

auto XenControlFactory::getSetMemAccess() const -> std::function<int(uint32_t, const std::map<unsigned long, xenmem_access_t>&)>
{
	auto f = lookup<decltype(xc_set_mem_access_multi)>("xc_set_mem_access_multi", false);
	if (f)
		return SetMemAccessImpl<SetMemAccessMultiFunc>(std::bind(f, getInterface(), _1, _2, _3, _4));

	auto g = lookup<decltype(xc_set_mem_access)>("xc_set_mem_access", true);

	return SetMemAccessImpl<SetMemAccessLegacyFunc>(std::bind(g, getInterface(), _1, _2, _3, _4));
}

#ifdef HVMOP_altp2m_set_mem_access_multi
using Altp2mSetMemAccessMultiFunc = std::function< remove_first_arg< decltype(xc_altp2m_set_mem_access_multi) >::type >;
#endif
using Altp2mSetMemAccessLegacyFunc = std::function< remove_first_arg< decltype(xc_altp2m_set_mem_access) >::type >;

template <typename T>
using Altp2mSetMemAccessImpl = AdapterFun<T, int(uint32_t domain, uint16_t altp2mViewId, const std::map<unsigned long, xenmem_access_t> &access)> ;

#ifdef HVMOP_altp2m_set_mem_access_multi
template <>
int Altp2mSetMemAccessImpl<Altp2mSetMemAccessMultiFunc>::operator()(uint32_t domain, uint16_t altp2mViewId, const std::map<unsigned long, xenmem_access_t> &access) const
{
	std::vector<uint8_t> access_type;
	std::vector<uint64_t> gfns;

	for ( auto &&item : access) {
		access_type.push_back( item.second );
		gfns.push_back( item.first );
	}
	return f_(domain, altp2mViewId, &access_type[0], &gfns[0], gfns.size());
}
#endif

template <>
int Altp2mSetMemAccessImpl<Altp2mSetMemAccessLegacyFunc>::operator()(uint32_t domain, uint16_t altp2mViewId, const std::map<unsigned long, xenmem_access_t> &access) const
{
	for ( auto &&item : access)
		// the gfn & access parameters' order is reversed from xc_set_mem_access
		f_( domain, altp2mViewId, item.first, item.second );
	return 0; //FIXME: value is ignored in the original code
}

auto XenControlFactory::getAltp2mSetMemAccess() const -> std::function<int(uint32_t, uint16_t, const std::map<unsigned long, xenmem_access_t>&)>
{
#ifdef HVMOP_altp2m_set_mem_access_multi
	auto f = lookup<decltype(xc_altp2m_set_mem_access_multi)>("xc_altp2m_set_mem_access_multi", false);
	if (f)
		return Altp2mSetMemAccessImpl<Altp2mSetMemAccessMultiFunc>(std::bind(f, getInterface(), _1, _2, _3, _4, _5));
#endif

	auto g = lookup<decltype(xc_altp2m_set_mem_access)>("xc_altp2m_set_mem_access", true);

	return Altp2mSetMemAccessImpl<Altp2mSetMemAccessLegacyFunc>(std::bind(g, getInterface(), _1, _2, _3, _4));
}


auto XenControlFactory::getAltp2mSetDomainState() const -> Altp2mSetDomainStateFunc
{
	static_assert( std::is_same <xc_altp2m_set_domain_state_func_t, decltype(xc_altp2m_set_domain_state)>::value, "");
	auto f = lookup< decltype(xc_altp2m_set_domain_state) > ("xc_altp2m_set_domain_state", true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getAltp2mCreateView() const -> Altp2mCreateViewFunc
{
	static_assert( std::is_same <xc_altp2m_create_view_func_t, decltype(xc_altp2m_create_view)>::value, "");
	auto f = lookup< decltype(xc_altp2m_create_view) > ("xc_altp2m_create_view", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

auto XenControlFactory::getAltp2mDestroyView() const -> Altp2mDestroyViewFunc
{
	static_assert( std::is_same <xc_altp2m_destroy_view_func_t, decltype(xc_altp2m_destroy_view)>::value, "");
	auto f = lookup< decltype(xc_altp2m_destroy_view) > ("xc_altp2m_destroy_view", true);
	return std::bind(f, getInterface(), _1, _2);
}

XenControlFactory& XenControlFactory::instance()
{
	static XenControlFactory instance;
	return instance;
}

XenControlFactory::XenControlFactory( )
    : libxcHandle_( nullptr ), xci_( nullptr )
{
	libxcHandle_ = ::dlopen( "libxenctrl.so", RTLD_NOW | RTLD_GLOBAL );
	if ( !libxcHandle_ )
		throw std::runtime_error( "Failed to open the XEN control library." );

	interfaceOpen_  = lookup< decltype(xc_interface_open) > ( "xc_interface_open",  true );
	interfaceClose_ = lookup< decltype(xc_interface_close) >( "xc_interface_close", true );

	xci_ = interfaceOpen_( NULL, NULL, 0 );
	if ( !xci_ )
		throw std::runtime_error( "xc_interface_open() failed" );

	version_ = lookup <decltype(xc_version) > ( "xc_version", true );
}

std::pair< int, int > XenControlFactory::getVersion() const
{
	int ver = version_(xci_, XENVER_version, NULL);

	return std::make_pair<int, int>(ver >> 16, ver & ((1 << 16) - 1));
}

const std::string XenControlFactory::getCaps() const
{
	xen_capabilities_info_t caps;

	if ( version_( xci_, XENVER_capabilities, &caps ) != 0 )
		throw std::runtime_error( "Could not get Xen capabilities" );

	return std::string(caps);
}

const XenDomainHandle XenControlFactory::getUuid() const
{
	xen_domain_handle_t uuid;

	if ( version_( xci_, XENVER_guest_handle, &uuid ) != 0 )
		throw std::runtime_error( "Could not get local domain UUID" );

	return XenDomainHandle(uuid);
}

XenControlFactory::~XenControlFactory( )
{
	if ( !libxcHandle_ )
		return;

	if ( xci_ )
		interfaceClose_( xci_ );

	::dlclose( libxcHandle_ );
}

XenVcpuSetRegisters::XenVcpuSetRegisters(uint32_t domid):
	domid_( domid ),
	isX86_64_( XenControl::instance().caps.find( "x86_64" ) != std::string::npos )
{
}

void XenVcpuSetRegisters::operator()(unsigned short vcpu, const Registers &regs, bool setEip) const
{
	static std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)> vcpu_getcontext =
		XenControlFactory::instance().getVcpuGetContext();
	static std::function<int( uint32_t, uint32_t, vcpu_guest_context_any_t*)> vcpu_setcontext =
		XenControlFactory::instance().getVcpuSetContext();
	vcpu_guest_context_any_t ctxt;

	StatsCollector::instance().incStat( "xcGetVcpuContext" );

	if ( vcpu_getcontext( domid_, vcpu, &ctxt ) != 0 )
		throw std::runtime_error(std::string( "xc_vcpu_getcontext() failed: " ) + strerror( errno ) );

	if ( isX86_64_ ) {

		ctxt.x64.user_regs.rax = regs.rax;
		ctxt.x64.user_regs.rcx = regs.rcx;
		ctxt.x64.user_regs.rdx = regs.rdx;
		ctxt.x64.user_regs.rbx = regs.rbx;
		ctxt.x64.user_regs.rsp = regs.rsp;
		ctxt.x64.user_regs.rbp = regs.rbp;
		ctxt.x64.user_regs.rsi = regs.rsi;
		ctxt.x64.user_regs.rdi = regs.rdi;
		ctxt.x64.user_regs.r8 = regs.r8;
		ctxt.x64.user_regs.r9 = regs.r9;
		ctxt.x64.user_regs.r10 = regs.r10;
		ctxt.x64.user_regs.r11 = regs.r11;
		ctxt.x64.user_regs.r12 = regs.r12;
		ctxt.x64.user_regs.r13 = regs.r13;
		ctxt.x64.user_regs.r14 = regs.r14;
		ctxt.x64.user_regs.r15 = regs.r15;
		ctxt.x64.user_regs.rflags = regs.rflags;

		if ( setEip )
			ctxt.x64.user_regs.eip = regs.rip;

	} else {

		ctxt.x32.user_regs.eax = regs.rax;
		ctxt.x32.user_regs.ecx = regs.rcx;
		ctxt.x32.user_regs.edx = regs.rdx;
		ctxt.x32.user_regs.ebx = regs.rbx;
		ctxt.x32.user_regs.esp = regs.rsp;
		ctxt.x32.user_regs.ebp = regs.rbp;
		ctxt.x32.user_regs.esi = regs.rsi;
		ctxt.x32.user_regs.edi = regs.rdi;
		ctxt.x32.user_regs.eflags = regs.rflags;

		if ( setEip )
			ctxt.x32.user_regs.eip = regs.rip;

	}

	StatsCollector::instance().incStat( "xcSetContext" );

	if ( vcpu_setcontext( domid_, vcpu, &ctxt ) == -1 )
		throw std::runtime_error(std::string( "xc_vcpu_setcontext() failed: " ) + strerror( errno ) );
}

XenAltp2mDomainState::XenAltp2mDomainState(uint32_t domain) :
	domain_(domain),
	enabled_(true)
{
}


XenControl& XenControl::instance()
{
	static XenControl instance;
	return instance;
}

XenControl::XenControl( ) :
	runtimeVersion(XenControlFactory::instance().getVersion()),
	caps(XenControlFactory::instance().getCaps()),
	uuid(XenControlFactory::instance().getUuid()),
	domainPause(XenControlFactory::instance().getDomainPause()),
	domainUnpause(XenControlFactory::instance().getDomainUnpause()),
	domainGetInfo(XenControlFactory::instance().getDomainGetInfo()),
	domainMaximumGpfn(XenControlFactory::instance().getDomainMaximumGpfn()),
	domainGetTscInfo(XenControlFactory::instance().getDomainGetTscInfo()),
	domainSetAccessRequired(XenControlFactory::instance().getDomainSetAccessRequired()),
	domainHvmGetContext(XenControlFactory::instance().getDomainHvmGetContext()),
	domainHvmGetContextPartial(XenControlFactory::instance().getDomainHvmGetContextPartial()),
	setMemAccess(XenControlFactory::instance().getSetMemAccess()),
	altp2mSetMemAccess(XenControlFactory::instance().getAltp2mSetMemAccess()),
	altp2mSetDomainState(XenControlFactory::instance().getAltp2mSetDomainState()),
	altp2mCreateView(XenControlFactory::instance().getAltp2mCreateView()),
	altp2mDestroyView(XenControlFactory::instance().getAltp2mDestroyView())
{
}

} // namespace bdvmi
