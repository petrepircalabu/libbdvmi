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
#include <xenctrl.h>
#include <xen/xen.h>

namespace bdvmi {

class XenControlFactory
{
public:
	XenControlFactory( );
	~XenControlFactory( );

	template < typename T >
	std::function<T> lookup( const std::string &name, bool required );

	std::pair< int, int > get_version() const;

private:
	void *libxc_handle_;
	xc_interface *xci_;

	std::function < decltype(xc_interface_open)  > interface_open_;
	std::function < decltype(xc_interface_close) > interface_close_;
	std::function < decltype(xc_version)         > version_;
};

template < typename T>
std::function<T> XenControlFactory::lookup( const std::string &name, bool required )
{
	std::function<T> func = reinterpret_cast< T* >( ::dlsym( libxc_handle_, name.c_str() ) );
	if ( required && !func )
		throw std::runtime_error( "Failed to get the \"" + name + "\" function" );
	return func;
}

XenControlFactory::XenControlFactory( )
    : libxc_handle_( nullptr ), xci_( nullptr )
{
	libxc_handle_ = ::dlopen( "libxenctrl.so", RTLD_NOW | RTLD_GLOBAL );
	if ( !libxc_handle_ )
		throw std::runtime_error( "Failed to open the XEN control library." );

	interface_open_  = lookup< decltype(xc_interface_open) > ( "xc_interface_open",  true );
	interface_close_ = lookup< decltype(xc_interface_close) >( "xc_interface_close", true );

	xci_ = interface_open_( NULL, NULL, 0 );
	if ( !xci_ )
		throw std::runtime_error( "xc_interface_open() failed" );

	version_ = lookup <decltype(xc_version) > ( "xc_version", true );
}

std::pair< int, int > XenControlFactory::get_version() const
{
	int ver = version_(xci_, XENVER_version, NULL);

	return std::make_pair<int, int>(ver >> 16, ver & ((1 << 16) - 1));
}

XenControlFactory::~XenControlFactory( )
{
	if ( !libxc_handle_ )
		return;

	if ( xci_ )
		interface_close_( xci_ );

	::dlclose( libxc_handle_ );
}

XenControl& XenControl::Instance()
{
	static XenControl instance;
	return instance;
}

XenControl::XenControl( ) :
	factory_(new XenControlFactory()),
	runtime_version(factory_->get_version())
{
}

} // namespace bdvmi
