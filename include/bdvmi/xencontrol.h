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

typedef unsigned long xen_pfn_t;
struct xc_interface_core;
typedef struct xc_interface_core xc_interface;

typedef int xc_domain_pause_func_t ( xc_interface*, uint32_t );
typedef int xc_domain_unpause_func_t ( xc_interface*, uint32_t );
typedef int xc_domain_shutdown_func_t ( xc_interface *xch, uint32_t domid, int reason );
typedef int xc_domain_maximum_gpfn_func_t ( xc_interface *xch, uint32_t domid, xen_pfn_t *gpfns );
typedef int xc_domain_debug_control_func_t (xc_interface *xch, uint32_t domid, uint32_t sop, uint32_t vcpu);

struct xc_dominfo;
typedef struct xc_dominfo xc_dominfo_t;

namespace bdvmi {

template <typename T>
struct DomainGetInfo;

struct DomInfo;
using DomainGetInfoFunc = DomainGetInfo<int(uint32_t domid, DomInfo& domInfo)>;

struct DomInfo
{
public:
	DomInfo();
	~DomInfo();

	uint32_t domid() const;
	bool hvm() const;
	unsigned int max_vcpu_id() const;

	friend DomainGetInfoFunc;

private:
	xc_dominfo_t *pimpl_;
};

using DomainPauseFunc = std::function< utils::remove_first_arg< xc_domain_pause_func_t >::type >;
using DomainUnpauseFunc = std::function< utils::remove_first_arg< xc_domain_unpause_func_t >::type >;
using DomainShutdownFunc = std::function< utils::remove_first_arg< xc_domain_shutdown_func_t >::type >;
using DomainMaximumGpfnFunc = std::function< utils::remove_first_arg< xc_domain_maximum_gpfn_func_t >::type >;
using DomainDebugControlFunc = std::function< utils::remove_first_arg< xc_domain_debug_control_func_t >::type >;

class XenControlFactory;

class XenControl
{
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
	const std::function<int(uint32_t domid, DomInfo& domInfo)> domainGetInfo;
	const DomainMaximumGpfnFunc domainMaximumGpfn;
	const DomainDebugControlFunc domainDebugControl;
};

} // namespace bdvmi

#endif // __BDVMIXENCTRL_H_INCLUDED__
