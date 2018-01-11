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

typedef xc_interface* xc_interface_open_t ( xentoollog_logger*,  xentoollog_logger*, unsigned );
typedef int xc_interface_close_t ( xc_interface* );

namespace bdvmi {

	class XenControl {

public:
	XenControl( );
	~XenControl( );

protected:
	template < typename T >
	void lookup( std::function<T> &func, const std::string &name, bool required );

private:
	void *libxc_handle_;

	xc_interface *xci_;

	std::function < xc_interface_open_t > interface_open_;
	std::function < xc_interface_close_t > interface_close_;
};

} // namespace bdvmi

#endif // __BDVMIXENCTRL_H_INCLUDED__


