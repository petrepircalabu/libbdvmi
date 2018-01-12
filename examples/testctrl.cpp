#include <bdvmi/xencontrol.h>

#include <iostream>

int main(int argc, char* argv[])
{
	bdvmi::XenControl ctrl;

	std::cout << "XEN version " << ctrl.xen_major_version << " : "
		<< ctrl.xen_minor_version << std::endl;

	return 0;
}
