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
#include <dlfcn.h>
#include <type_traits>
#include <xenctrl.h>
#include <xen/xen.h>
#include <iostream>
#include <cerrno>
#include <cstring>

namespace bdvmi {

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

class XenControlFactory
{
public:
	XenControlFactory( );
	~XenControlFactory( );

	template < typename T >
	std::function<T> lookup( const std::string &name, bool required ) const;

	auto getDomainPause() const -> DomainPauseFunc;
	auto getDomainUnpause() const -> DomainUnpauseFunc;
	auto getDomainShutdown() const -> DomainShutdownFunc;
	auto getDomainGetInfo() const -> DomainGetInfoFunc;
	auto getDomainMaximumGpfn() const -> DomainMaximumGpfnFunc;
	auto getDomainDebugControl() const -> DomainDebugControlFunc;
	auto getDomainGetTscInfo() const -> DomainGetTscInfoFunc;

	std::pair< int, int > getVersion() const;

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

	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_pause) > ( "xc_domain_pause", true);
	return std::bind(f, getInterface(), _1);
}

auto XenControlFactory::getDomainUnpause() const -> DomainUnpauseFunc
{
	static_assert( std::is_same <xc_domain_unpause_func_t , decltype(xc_domain_unpause)>::value , "" );

	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_pause) > ( "xc_domain_unpause", true);
	return std::bind(f, getInterface(), _1);
}

auto XenControlFactory::getDomainShutdown() const -> DomainShutdownFunc
{
	static_assert( std::is_same <xc_domain_shutdown_func_t , decltype(xc_domain_shutdown)>::value , "" );

	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_shutdown) > ( "xc_domain_shutdown", true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getDomainGetInfo() const -> DomainGetInfoFunc
{
	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_getinfo) > ( "xc_domain_getinfo", true);
	return DomainGetInfoFunc(std::bind(f, getInterface(), _1, _2, _3));
}


auto XenControlFactory::getDomainMaximumGpfn() const -> DomainMaximumGpfnFunc
{
	static_assert( std::is_same <xc_domain_maximum_gpfn_func_t , decltype(xc_domain_maximum_gpfn)>::value , "" );

	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_maximum_gpfn) > ("xc_domain_maximum_gpfn" , true);
	return std::bind(f, getInterface(), _1, _2);
}

auto XenControlFactory::getDomainDebugControl() const -> DomainDebugControlFunc
{
	static_assert( std::is_same <xc_domain_debug_control_func_t, decltype(xc_domain_debug_control)>::value, "");

	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_debug_control) > ("xc_domain_debug_control", true);
	return std::bind(f, getInterface(), _1, _2, _3);
}

auto XenControlFactory::getDomainGetTscInfo() const -> DomainGetTscInfoFunc
{
	static_assert( std::is_same <xc_domain_get_tsc_info_func_t, decltype(xc_domain_get_tsc_info)>::value, "");
	using namespace std::placeholders;
	auto f = lookup< decltype(xc_domain_get_tsc_info) > ("xc_domain_get_tsc_info", true);
	return std::bind(f, getInterface(), _1, _2, _3, _4, _5);
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

XenControlFactory::~XenControlFactory( )
{
	if ( !libxcHandle_ )
		return;

	if ( xci_ )
		interfaceClose_( xci_ );

	::dlclose( libxcHandle_ );
}

XenControl& XenControl::instance()
{
	static XenControl instance;
	return instance;
}

XenControl::XenControl( ) :
	factory_(new XenControlFactory()),
	runtimeVersion(factory_->getVersion()),
	domainPause(factory_->getDomainPause()),
	domainUnpause(factory_->getDomainUnpause()),
	domainGetInfo(factory_->getDomainGetInfo()),
	domainMaximumGpfn(factory_->getDomainMaximumGpfn()),
	domainGetTscInfo(factory_->getDomainGetTscInfo())
{
}

} // namespace bdvmi
