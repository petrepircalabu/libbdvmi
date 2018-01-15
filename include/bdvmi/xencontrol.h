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
#define __BDVMIXENCTRL_H_INCLUDED__

#include <functional>
#include <memory>
#include "bdvmi/utils.h"

struct xc_interface_core;
typedef struct xc_interface_core xc_interface;

typedef int xc_domain_pause_func_t ( xc_interface*, uint32_t );
typedef int xc_domain_unpause_func_t ( xc_interface*, uint32_t );
typedef int xc_domain_shutdown_func_t ( xc_interface *xch, uint32_t domid, int reason );

namespace bdvmi {

class XenControlFactory;

using DomainPauseFunc = std::function< utils::remove_first_arg< xc_domain_pause_func_t >::type >;
using DomainUnpauseFunc = std::function< utils::remove_first_arg< xc_domain_unpause_func_t >::type >;
using DomainShutdownFunc = std::function< utils::remove_first_arg< xc_domain_shutdown_func_t >::type >;

class XenControl {
public:
	static XenControl& instance();

private:
	XenControl( );

	XenControl(const XenControl&) = delete;
	XenControl& operator=(const XenControl&) = delete;

	std::unique_ptr<XenControlFactory> factory_;

public:
	const std::pair<int, int> runtimeVersion;

	/*
	 * DOMAIN MANAGEMENT FUNCTIONS
	 */
	const DomainPauseFunc domainPause;
	const DomainUnpauseFunc domainUnpause;
	const DomainShutdownFunc domainShutdown;
};

} // namespace bdvmi

#endif // __BDVMIXENCTRL_H_INCLUDED__


