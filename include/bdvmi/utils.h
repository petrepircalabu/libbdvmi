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

#ifndef __BDVMIUTILS_H_INCLUDED__
#define __BDVMIUTILS_H_INCLUDED__

namespace bdvmi {

namespace utils {

template <typename F>
struct remove_first_arg;

template <typename R, typename A, typename... Args>
struct remove_first_arg<R(A, Args... )>
{
    using type=R(Args... );
};

} // namespace utils

} // namespace bdvmi

#endif // __BDVMIXENINLINES_H_INCLUDED__

