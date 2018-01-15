#include <bdvmi/xencontrol.h>

#include <iostream>

int main(int argc, char* argv[])
{
	bdvmi::XenControl ctrl;
	std::cout << "XEN version " << ctrl.runtime_version.first << "." << ctrl.runtime_version.second << std::endl;
	return 0;
}
