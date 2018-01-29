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

#ifndef __BDVMIXSUTILS_H_INCLUDED__
#define __BDVMIXSUTILS_H_INCLUDED__

#include <memory>

struct xs_handle;
typedef uint32_t xs_transaction_t;

namespace bdvmi {

class XSUtils
{
public:
	static XSUtils& instance();
	~XSUtils( );

	std::string getUUID(uint32_t domain);

	uint32_t getDomainID(const std::string &uuid);

	uint32_t getStartTime(uint32_t domain);

	void update(uint32_t domain);

protected:
	void *read_timeout(xs_transaction_t t, const char *path, unsigned int *len, unsigned int timeout );

private:
	XSUtils( );
	XSUtils( const XSUtils& ) = delete;
	XSUtils& operator=( const XSUtils& ) = delete;

	xs_handle *xsh_;
};

} // namespace bdvmi

#endif // __BDVMIXSUTILS_H_INCLUDED__
