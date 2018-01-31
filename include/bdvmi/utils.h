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

#include <vector>

namespace bdvmi {

namespace utils {

template <typename F>
struct remove_first_arg;

template <typename R, typename A, typename... Args>
struct remove_first_arg<R(A, Args... )>
{
	using type=R(Args... );
};


class Observable
{
public:
	void attach(std::function<void()> observer)
	{
		observers.push_back(observer);
	}

	void notify()
	{
		for (auto f:observers)
			f();
	}

private:
	std::vector<std::function<void()> > observers;
};

template <typename F>
class ObservableFun;

template <typename R, typename... Args>
class ObservableFun<R(Args ...)> : public Observable
{
public:
	ObservableFun(std::function<R(Args ...)>f) : f_(f) {}

	R operator()(Args ... args)
	{
		notify();
		return f_(args...);
	}
private:
	std::function<R(Args ...)> f_;
};

} // namespace utils

} // namespace bdvmi

#endif // __BDVMIXENINLINES_H_INCLUDED__

