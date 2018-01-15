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
#include <memory>

namespace bdvmi {

class XenControlFactory;

class XenControl {
public:
	XenControl( );
	~XenControl( );

private:
	std::unique_ptr<XenControlFactory> factory_;

public:
	const std::pair<int, int> runtime_version;
};

} // namespace bdvmi

#endif // __BDVMIXENCTRL_H_INCLUDED__


