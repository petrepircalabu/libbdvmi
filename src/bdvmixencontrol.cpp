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
#include <dlfcn.h>
#include <type_traits>
#include <xenctrl.h>
#include <xen/xen.h>
#include <iostream>

namespace bdvmi {

class XenControlFactory
{
public:
	XenControlFactory( );
	~XenControlFactory( );

	template < typename T >
	std::function<T> lookup( const std::string &name, bool required ) const;

	auto getDomainPause() const -> DomainPauseFunc;
	auto getDomainUnpause() const -> DomainUnpauseFunc;

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

XenControl& XenControl::Instance()
{
	static XenControl instance;
	return instance;
}

XenControl::XenControl( ) :
	factory_(new XenControlFactory()),
	runtimeVersion(factory_->getVersion()),
	domainPause(factory_->getDomainPause()),
	domainUnpause(factory_->getDomainUnpause())
{
}

} // namespace bdvmi
