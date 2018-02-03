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

#include "bdvmi/xenaltp2m.h"
#include "bdvmi/xencontrol.h"

#include <string>

#include <cerrno>
#include <cstring>
#include <sstream>

using namespace std::placeholders;

namespace bdvmi {

XenAltp2mView::XenAltp2mView()
{
}

XenAltp2mView::~XenAltp2mView()
{
}

uint16_t XenAltp2mView::getId() const
{
	return view_id_;
}

void XenAltp2mView::switchToView()
{
}

XenAltp2mDomainState::XenAltp2mDomainState(uint32_t domain) :
	domain_(domain),
	enabled_(true),
	enableAltp2m_( std::bind(XenControl::instance().altp2mSetDomainState, domain, _1) ),
	current_view_( 0 )

{
	if ( enableAltp2m_(true) < 0 )
		throw std::runtime_error( std::string( "[ALTP2M] could not enable altp2m on domain: " ) +
					  strerror( errno ) );
}

XenAltp2mDomainState::~XenAltp2mDomainState()
{
	enableAltp2m_(false);
}

uint16_t XenAltp2mDomainState::createView()
{
	XenAltp2mView *pView = new XenAltp2mView();
	uint16_t id = pView->getId();
	views_.emplace( id, std::unique_ptr<XenAltp2mView>(pView) );
	return id;
}

//TODO: Add error handling
void XenAltp2mDomainState::destroyView( uint16_t view_id )
{
	for (auto it = views_.cbegin(); it != views_.cend(); ) {
		if ( it->first == view_id )
			it = views_.erase(it);
		else
			++it;
	}
}

void XenAltp2mDomainState::switchToView( uint16_t view_id )
{
	if ( view_id == current_view_ )
		return;

	if ( !view_id ) {
		XenControl::instance().altp2mSwitchToView( domain_, 0 );
		current_view_ = 0;
	}

	auto it = views_.find(view_id);
	if ( it == views_.end() )
		throw std::runtime_error( std::string( "[ALTP2M] cannot switch view: invalid id" ) + std::to_string(view_id ) );

	it->second->switchToView();
	current_view_ = view_id;
}

} // namespace bdvmi