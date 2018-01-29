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

#include "bdvmi/xsutils.h"

extern "C" {
#include <xenstore.h>
}

#include <cstring>
#include <sstream>

namespace bdvmi {

XSUtils& XSUtils::instance( )
{
	static XSUtils instance;
	return instance;
}

XSUtils::XSUtils( ) :
	xsh_( nullptr )
{
	xsh_ = xs_open( 0 );
	if ( !xsh_ )
		throw std::runtime_error( "xs_open() failed" );
}

XSUtils::~XSUtils( )
{
	if ( !xsh_ ) {
		xs_close( xsh_ );
		xsh_ = nullptr;
	}
}

void *XSUtils::read_timeout(xs_transaction_t t, const char *path, unsigned int *len, unsigned int timeout )
{
	struct timespec tim, tim2;
	const long nanosec_sleep = 1000000;
	float seconds_timeout = timeout;
	void *ret = nullptr;
	int saved_errno;

	do {
		tim.tv_sec = 0;
		tim.tv_nsec = nanosec_sleep;

		ret = xs_read( xsh_, t, path, len );

		if ( ret || errno != EPERM )
			break;

		saved_errno = errno;

		if ( nanosleep( &tim, &tim2 ) != 0 && errno == EINTR )
			tim.tv_nsec -= tim2.tv_nsec;

		errno = saved_errno;
		seconds_timeout -= 1.0e-9 * tim.tv_nsec;

	} while ( seconds_timeout > 0 );

	return ret;
}

std::string XSUtils::getUUID(uint32_t domain)
{
	std::stringstream ss;
	unsigned int size;
	std::string uuid;

	ss.str( "" );
	ss << "/local/domain/" << domain << "/vm";

	char *path = static_cast<char *>( read_timeout( XBT_NULL, ss.str().c_str(), &size, 1 ) );

	if ( path && path[0] != '\0' ) {
		ss.str( "" );
		ss << path << "/uuid";

		free( path );
		size = 0;

		path = static_cast<char *>( read_timeout( XBT_NULL, ss.str().c_str(), &size, 1 ) );

		if ( path && path[0] != '\0' )
			uuid = path;
	}

	free( path );

	return uuid;

}

uint32_t XSUtils::getDomainID(const std::string &uuid)
{
	uint32_t domainId = 0;
	unsigned int size = 0;

	char **domains = xs_directory( xsh_, XBT_NULL, "/local/domain", &size );

	if ( size == 0 )
		throw std::runtime_error( std::string( "Failed to retrieve domain ID by UUID [" ) + uuid + "]: " + strerror( errno ) );

	for ( unsigned int i = 0; i < size; ++i ) {

		std::string tmp = std::string( "/local/domain/" ) + domains[i] + "/vm";

		char *path = static_cast<char *>( read_timeout( XBT_NULL, tmp.c_str(), nullptr, 1 ) );

		if ( path && path[0] != '\0' ) {
			tmp = std::string( path ) + "/uuid";

			char *tmpUuid = static_cast<char *>( read_timeout( XBT_NULL, tmp.c_str(), nullptr, 1 ) );

			if ( tmpUuid && uuid == tmpUuid ) {
				domainId = atoi( domains[i] );
				free( tmpUuid );
				free( path );
				break;
			}

			free( tmpUuid );
		}

		free( path );
	}

	free( domains );

	return domainId;
}

uint32_t XSUtils::getStartTime(uint32_t domain)
{
	uint32_t startTime = ( uint32_t )-1;
	unsigned int size = 0;
	std::stringstream ss;

	ss << "/local/domain/" << domain << "/vm";

	char *path = static_cast<char *>( read_timeout( XBT_NULL, ss.str().c_str(), &size, 1 ) );

	if ( path && path[0] != '\0' ) {
		ss.str( "" );
		ss << path << "/start_time";

		std::string path1 = ss.str();

		ss.str( "" );
		ss << path << "/domains/" << domain << "/create-time";

		std::string path2 = ss.str();

		free( path );
		size = 0;

		path = static_cast<char *>( read_timeout( XBT_NULL, path1.c_str(), &size, 1 ) );

		if ( path  && path[0] != '\0' )
			startTime = strtoul( path, nullptr, 10 );

		free( path );
		path = nullptr;
		size = 0;

		if ( startTime == ( uint32_t )-1 ) // XenServer
			path = static_cast<char *>( read_timeout( XBT_NULL, path2.c_str(), &size, 1 ) );

		if ( path  && path[0] != '\0' )
			startTime = strtoul( path, nullptr, 10 );
	}

	free( path );
	return startTime;
}

void XSUtils::update(uint32_t domain)
{
	std::stringstream ss;
	ss << "/local/domain/" << domain << "/data/updated";

	xs_write( xsh_, XBT_NULL, ss.str().c_str(), "now", 3 );
}

} // namespace bdvmi
