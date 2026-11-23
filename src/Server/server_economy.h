// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.

#pragma once

// PORT(deviation) SERVER-ECONOMY: not part of retail (docs/re/deviations.md).
//
// Retail's server never prices anything. A docked client buys, sells and trades in ships by
// telling the server the outcome: GFGoodBuy / GFGoodSell (which only notify the controller),
// ReqAddItem, ReqRemoveItem, ReqEquipment, ReqShipArch, and ReqChangeCash / ReqSetCash with
// whatever amount it likes. These checks hold each request to what the retail client's
// dealer screens send (src/freelancer/Client: nn_inventory.cpp BuyFromDealer / SellSelected,
// nn_dealer.cpp BuyShip, nn_repair.cpp), priced from the server's own market data with the
// base's price_variance as the tolerance. Multiplayer only; each Ledger_* returns true (and
// changes nothing) in single player.
//
// Requests are checked as they arrive, in the order a client sends them (one reliable,
// ordered channel). A refused request changes nothing, and the server sends the client its
// real cash or equipment back, so the client's screens show the truth again.

#include "server_playerdata.h"

struct SGFGoodBuyInfo;
struct SGFGoodSellInfo;
class EquipDescList;

// The player's own ship landed on BaseId (SystemPlayerView_OnObjectLanded).
void Ledger_NoteLanded(unsigned int SessionId, unsigned int BaseId);
// BaseEnter: the base the player landed on, or the one the server put it in (character load,
// respawn: PlayerData::current_base).
bool Ledger_MayEnterBase(PlayerSlot& Slot, unsigned int SessionId, unsigned int BaseId);
// BaseEnter accepted: a fresh dock.
void Ledger_OnBaseEnter(unsigned int SessionId, unsigned int BaseId);
// BaseExit, disconnect, logout: what was bought and not paid for is charged, a sale credit
// that never matched a sale is dropped, and an unfinished ship purchase is closed.
void Ledger_Settle(PlayerSlot& Slot, unsigned int SessionId);
void Ledger_Forget(unsigned int SessionId);

// False: drop the request (the client has been resynchronised).
bool Ledger_OnGoodBuy(PlayerSlot& Slot, unsigned int SessionId, SGFGoodBuyInfo const& Info);
bool Ledger_OnGoodSell(PlayerSlot& Slot, unsigned int SessionId, SGFGoodSellInfo const& Info);
bool Ledger_MayAddItem(PlayerSlot& Slot, unsigned int SessionId, unsigned int ArchId, int Count,
                       float Health);
bool Ledger_MayModifyItem(PlayerSlot& Slot, unsigned int SessionId, unsigned short EquipId,
                          int Count, float Health);
bool Ledger_MayLoadEquipment(PlayerSlot& Slot, unsigned int SessionId,
                             EquipDescList const& Equipment);
bool Ledger_MaySetShipArch(PlayerSlot& Slot, unsigned int SessionId, unsigned int ArchId);
// After the hull has changed: the purchase is complete.
void Ledger_OnShipArchSet(PlayerSlot& Slot, unsigned int SessionId);
bool Ledger_MaySetCash(PlayerSlot& Slot, unsigned int SessionId, int Cash);
// ReqChangeCash. True: apply Delta now (a payment). False: the ledger has taken it (a sale
// credit, paid out when the sale it belongs to is complete) or refused it.
bool Ledger_OnChangeCash(PlayerSlot& Slot, unsigned int SessionId, int Delta);
// ReqRemoveItem. Removes the item, and pays the pending sale credit when this removal is
// the sale it was for.
void Ledger_RemoveItem(PlayerSlot& Slot, unsigned int SessionId, unsigned short EquipId, int Count);
