// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// PORT(dpnet) (docs/dpnet.md): the IDirectPlay8Server, IDirectPlay8Client and IDirectPlay8Address
// objects of the port's own DirectPlay 8 implementation, DPNet, for the builds that do not use
// Microsoft's dpnet.dll (FREELANCER_DPNET: the x64, macOS and Linux builds). CDPServer and
// CDPClient take their objects from here instead of CoCreateInstance and call them exactly as
// they call Microsoft's.
//
// Only what DALib's CDPServer and CDPClient use is implemented (docs/dpnet.md lists it); every
// other method returns DPNERR_UNSUPPORTED.

#include <dplay8.h>

namespace DPNetAdapter
{
    // CoCreateInstance(CLSID_DirectPlay8Server, ..., IID_IDirectPlay8Server, Out).
    HRESULT CreateServer(IDirectPlay8Server** Out);
    // CoCreateInstance(CLSID_DirectPlay8Client, ..., IID_IDirectPlay8Client, Out).
    HRESULT CreateClient(IDirectPlay8Client** Out);
    // CoCreateInstance(CLSID_DirectPlay8Address, ..., IID_IDirectPlay8Address, Out).
    HRESULT CreateAddress(IDirectPlay8Address** Out);
}
