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

#ifndef __BDVMIXENALTP2M_H_INCLUDED__
#define __BDVMIXENALTP2M_H_INCLUDED__

#ifndef __XEN_TOOLS__
#define __XEN_TOOLS__ 1
#endif

#include <functional>
#include <map>
#include <memory>

extern "C" {
#include <xen/memory.h>
}

namespace bdvmi {

class XenAltp2mView
{
public:
	XenAltp2mView();
	~XenAltp2mView();

	uint16_t getId() const;

	void switchToView();

private:
	uint16_t view_id_;
};

class XenAltp2mDomainState
{
public:
	XenAltp2mDomainState(uint32_t domain);
	~XenAltp2mDomainState();

	uint16_t createView();

	void destroyView( uint16_t view_id );

	void switchToView( uint16_t view_id );

private:
	uint32_t domain_;
	bool enabled_;
	std::function<int(bool)> enableAltp2m_;
	std::function<int(xenmem_access_t, uint16_t *)> createView_;
	std::function<int(uint16_t)> destroyView_;
	std::function<int(uint16_t)> switchToView_;
	std::map<uint16_t, std::unique_ptr<XenAltp2mView> > views_;
	uint16_t current_view_;
};

} // namespace bdvmi
#endif //__BDVMIXENALTP2M_H_INCLUDED__
