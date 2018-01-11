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

namespace bdvmi {

template < typename T>
void XenControl::lookup( std::function<T> &func, const std::string &name, bool required )
{
	func = reinterpret_cast< T* >( ::dlsym( libxc_handle_, name.c_str() ) );
	if ( required && !func )
		throw std::runtime_error( "Failed to get the \"" + name + "\" function" );
}

XenControl::XenControl ( )
    : libxc_handle_( nullptr ), xci_( nullptr )
{
	libxc_handle_ = ::dlopen( "libxenctrl.so", RTLD_NOW | RTLD_GLOBAL );
	if ( !libxc_handle_ )
		throw std::runtime_error( "Failed to open the XEN control library." );

	lookup< xc_interface_open_t >( interface_open_, "xc_interface_open", true );
	lookup< xc_interface_close_t >( interface_close_, "xc_interface_close", true );

	xci_ = interface_open_( NULL, NULL, 0 );
	if ( !xci_ )
		throw std::runtime_error( "xc_interface_open() failed" );
}

XenControl::~XenControl ( )
{
	if ( !libxc_handle_ )
		return;

	if ( xci_ )
		interface_close_( xci_ );


	::dlclose( libxc_handle_ );
}

} // namespace bdvmi
