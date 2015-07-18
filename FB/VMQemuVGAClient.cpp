/*
 *  VMQemuVGAClient.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 4th 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include "VMQemuVGAClient.h"
#include "VMQemuVGA.h"

#define super IOUserClient

OSDefineMetaClassAndStructors(VMQemuVGAClient, IOUserClient);


static IOExternalMethod const iofbFuncsCache[1] =
{
	{0, reinterpret_cast<IOMethod>(&VMQemuVGA::CustomMode), kIOUCStructIStructO, sizeof(CustomModeData), sizeof(CustomModeData)}
};

IOExternalMethod* VMQemuVGAClient::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	IOLog( "%s: index=%u.\n", __FUNCTION__, static_cast<unsigned>(index));
	if (!targetP)
		return 0;
	if (index != 0 && index != 3) {
		IOLog( "%s: Invalid index %u.\n",
				  __FUNCTION__, static_cast<unsigned>(index));
		return 0;
	}
	*targetP = getProvider();
	return const_cast<IOExternalMethod*>(&iofbFuncsCache[0]);
}

IOReturn VMQemuVGAClient::clientClose()
{
	IOLog( "%s()\n", __FUNCTION__);
	if (!terminate())
		IOLog( "%s: terminate failed.\n", __FUNCTION__);
	return kIOReturnSuccess;
}

bool VMQemuVGAClient::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	if (!super::initWithTask(owningTask, securityToken, type) ||
		clientHasPrivilege(securityToken, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
		return false;
	return true;
}
