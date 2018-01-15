#include <bdvmi/xencontrol.h>

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
	bdvmi::XenControl &ctrl = bdvmi::XenControl::instance();
	std::cout << "XEN version " << ctrl.runtimeVersion.first << "." << ctrl.runtimeVersion.second << std::endl;
	std::cout << "Unpausing domain " << std::stol(argv[1]) << std::endl;
	ctrl.domainUnpause(std::stol(argv[1]));
	return 0;
}
