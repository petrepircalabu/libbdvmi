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

#ifndef __BDVMIXENCTRL_H_INCLUDED__
#define __BDVMIXENCTRL_H_INCLUDED_

#include <functional>
#include <xenctrl.h>
#include <xen/xen.h>

namespace bdvmi {

	class XenControl {

public:
	XenControl( );
	~XenControl( );

	int xen_major_version;	// XEN Major version
	int xen_minor_version;	// XEN Minor version

protected:
	template < typename T >
	std::function<T> lookup( const std::string &name, bool required );

private:
	void *libxc_handle_;

	xc_interface *xci_;


	std::function < decltype(xc_interface_open)  > interface_open_;
	std::function < decltype(xc_interface_close) > interface_close_;
	std::function < decltype(xc_version)         > version_;
};

} // namespace bdvmi

#endif // __BDVMIXENCTRL_H_INCLUDED__


