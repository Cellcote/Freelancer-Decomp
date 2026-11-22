// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.

// PORT(deviation) SERVER-ECONOMY: see server_economy.h and docs/re/deviations.md.

#include "server_economy.h"

#include "common_goods.h"
#include "common_misc.h"
#include "common_server_types.h"
#include "rpc_iclient.h"
#include "server_basedb.h"

#include <algorithm>
#include <map>

extern IClient* g_pClientInterface;

namespace
{
constexpr unsigned int kIdMask = 0x3fffffffu;
constexpr long long kMaxCash = 999999999;
    // The client's market prices are ftol(price * scale), at most 9999999
    // (ClientMarketGetPrice, client_base_market.cpp).
constexpr double kMaxClientPrice = 9999999.0;

    // One dock's worth of what the client has asked for and not yet completed.
struct FLedger
{
    unsigned int LandedBase = 0;           // the base the player's own ship last landed on
    std::map<unsigned int, long long> Allowance; // archetype -> count ReqAddItem may add
    long long Balance = 0;                 // payments less the least the purchases cost
    unsigned int FreeAmmoGood = 0;         // a gun's ammo package, bought after the gun
    int FreeAmmoLeft = 0;

    bool bCredit = false;                  // a sale credit, waiting for its sale
    int Credit = 0;
    bool bSaleNamed = false;               // GFGoodSell has named the good
    unsigned int SaleGood = 0;
    int SaleCount = 0;

    bool bShip = false;                    // a ship purchase (NnDealer::BuyShip) is open
    unsigned int ShipGood = 0;             // the package
    int StartCash = 0;
    std::map<unsigned int, long long> OldCounts;
    bool bHullSet = false;                 // ReqShipArch arrived
    unsigned int OldHull = 0;
};

std::map<unsigned int, FLedger> GLedgers;

FLedger& LedgerOf(unsigned int SessionId)
{
    return GLedgers[SessionId];
}

bool Docked(PlayerSlot const& Slot)
{
    return (Slot.entered_base_id & kIdMask) != 0 && Slot.entered_base_id == Slot.current_base;
}

BaseData const* FindBaseData(unsigned int BaseId)
{
    if ((BaseId & kIdMask) == 0)
        return nullptr;
    FBaseEntry* const Entry = GetBaseDB()->find_base(BaseId);
    return Entry != nullptr ? Entry->Data : nullptr;
}

    // The base's price for a good, as pub::Market::GetPrice and the client's market have it:
    // the market entry's, else the good's nominal price.
double MarketPrice(BaseData const* Base, unsigned int GoodId)
{
    if (Base != nullptr)
    {
        std::map<unsigned int, MarketGoodInfo>::const_iterator It = Base->Market.find(GoodId);
        if (It != Base->Market.end())
            return It->second.Price;
    }
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    return Good != nullptr ? Good->Price : 0.0;
}

double Variance(BaseData const* Base)
{
    double const V = Base != nullptr ? Base->PriceVariance : 0.0;
    return V < 0.0 ? 0.0 : (V > 1.0 ? 1.0 : V);
}

    // The client scales each price by a random factor in [1 - variance, 1 + variance) and
    // truncates it (client_base_market.cpp). The bounds carry one credit of rounding slack.
long long MaxUnit(BaseData const* Base, unsigned int GoodId)
{
    double High = MarketPrice(Base, GoodId) * (1.0 + Variance(Base));
    if (High > kMaxClientPrice)
        High = kMaxClientPrice;
    return High > 0.0 ? static_cast<long long>(High) + 1 : 1;
}

long long MinUnit(BaseData const* Base, unsigned int GoodId)
{
    double Low = MarketPrice(Base, GoodId) * (1.0 - Variance(Base));
    if (Low > kMaxClientPrice)
        Low = kMaxClientPrice;
    long long const Unit = static_cast<long long>(Low) - 1;
    return Unit > 0 ? Unit : 0;
}

    // What the client's dealer offers: a market entry with stock (ClientMarket build,
    // client_base_market.cpp: a minimum and a positive maximum).
bool Sells(BaseData const* Base, unsigned int GoodId)
{
    if (Base == nullptr)
        return false;
    std::map<unsigned int, MarketGoodInfo>::const_iterator It = Base->Market.find(GoodId);
    return It != Base->Market.end() && It->second.MinInventory != 0 && It->second.MaxInventory > 0;
}

void CountByArch(EquipDescList const& List, std::map<unsigned int, long long>& Out)
{
    for (std::list<EquipDesc>::const_iterator It = List.Items.begin(); It != List.Items.end(); ++It)
        if (It->Count > 0)
            Out[It->ArchId] += It->Count;
}

int ClampCash(long long Cash)
{
    return static_cast<int>(Cash < 0 ? 0 : (Cash > kMaxCash ? kMaxCash : Cash));
}

void ResyncCash(PlayerSlot& Slot)
{
    g_pClientInterface->SendServerSetCash(Slot.client_id, Slot.cash);
}

void ResyncEquipment(PlayerSlot& Slot)
{
    EquipDescVector Wire(Slot.equipment);
    g_pClientInterface->SendServerSetEquipment(Slot.client_id, Wire);
}

    // Purchases: charge what was bought and not paid for, and close the allowances.
void SettleBuys(PlayerSlot& Slot, FLedger& Ledger)
{
    if (Ledger.Balance < 0)
        PlayerData_SetCash(Slot, ClampCash(static_cast<long long>(Slot.cash) + Ledger.Balance));
    Ledger.Balance = 0;
    Ledger.Allowance.clear();
    Ledger.FreeAmmoGood = 0;
    Ledger.FreeAmmoLeft = 0;
}

    // A sale credit that no sale completed: never paid, and the client is told its real cash.
void DropCredit(PlayerSlot& Slot, FLedger& Ledger)
{
    if (Ledger.bCredit)
        ResyncCash(Slot);
    Ledger.bCredit = false;
    Ledger.Credit = 0;
    Ledger.bSaleNamed = false;
    Ledger.SaleGood = 0;
    Ledger.SaleCount = 0;
}

    // NnDealer::BuyShip sets the cash to `credits - (hull price - trade-in - resale of the
    // items left in the dealer's window + resale of the player's items in its own)`
    // (NnLoadoutResaleValue counts each window's own items). So the cash may rise by at most
    // the old hull's price (when the hull changed) and the price of every item, old or from
    // the package, that the new loadout leaves behind, less the least the new hull costs.
void FinishShip(PlayerSlot& Slot, FLedger& Ledger)
{
    if (!Ledger.bShip)
        return;
    Ledger.bShip = false;
    BaseData const* const Base = FindBaseData(Slot.current_base);
    GoodInfo const* const Package = GoodList::find_by_id(Ledger.ShipGood);
    GoodInfo const* const Hull =
        Package != nullptr ? GoodList::find_by_id(Package->HullGoodId) : nullptr;

    long long Ceiling = Ledger.StartCash;
    if (Ledger.bHullSet)
    {
        if (Hull != nullptr)
            Ceiling -= MinUnit(Base, Hull->Id);
        GoodInfo const* const OldHull = GoodList_get()->find_by_ship_arch(Ledger.OldHull);
        if (OldHull != nullptr)
            Ceiling += MaxUnit(Base, OldHull->Id);
    }

        // Everything of the old loadout and the package's addons that the new loadout does not
        // hold may have been sold back.
    std::map<unsigned int, long long> Pool = Ledger.OldCounts;
    if (Package != nullptr)
    {
        std::vector<EquipDesc> const* const Lists[3] = {&Package->AddonLow, &Package->AddonMid,
                                                        &Package->AddonHigh};
        for (std::vector<EquipDesc> const* List : Lists)
            for (std::vector<EquipDesc>::const_iterator It = List->begin(); It != List->end(); ++It)
                if (It->Count > 0)
                    Pool[It->ArchId] += It->Count;
    }
    std::map<unsigned int, long long> NewCounts;
    CountByArch(Slot.equipment, NewCounts);
    for (std::map<unsigned int, long long>::const_iterator It = Pool.begin(); It != Pool.end();
         ++It)
    {
        long long const Sold = std::max<long long>(0, It->second - NewCounts[It->first]);
        if (Sold > 0)
            Ceiling += Sold * MaxUnit(Base, Arch2Good(It->first));
    }

    if (Slot.cash > Ceiling)
        PlayerData_SetCash(Slot, ClampCash(Ceiling));
}

void SettleAll(PlayerSlot& Slot, FLedger& Ledger)
{
    SettleBuys(Slot, Ledger);
    DropCredit(Slot, Ledger);
    FinishShip(Slot, Ledger);
}
}

void Ledger_NoteLanded(unsigned int SessionId, unsigned int BaseId)
{
    LedgerOf(SessionId).LandedBase = BaseId;
}

bool Ledger_MayEnterBase(PlayerSlot& Slot, unsigned int SessionId, unsigned int BaseId)
{
    if (SinglePlayer())
        return true;
    // The base the server put the player in (character load sets current_base).
    if ((Slot.current_base & kIdMask) != 0)
        return BaseId == Slot.current_base;
    // The base the player's own ship landed on.
    FLedger const& Ledger = LedgerOf(SessionId);
    if (Ledger.LandedBase != 0 && BaseId == Ledger.LandedBase)
        return true;
    // Without a ship in space, the last base: a dead player starts again there, and a player
    // who left a base and has not launched may go back in.
    return Slot.ship_object_id == 0 && Slot.last_base_id != 0 && BaseId == Slot.last_base_id;
}

void Ledger_OnBaseEnter(unsigned int SessionId)
{
    GLedgers[SessionId] = FLedger();
}

void Ledger_Settle(PlayerSlot& Slot, unsigned int SessionId)
{
    if (SinglePlayer())
        return;
    std::map<unsigned int, FLedger>::iterator It = GLedgers.find(SessionId);
    if (It != GLedgers.end())
        SettleAll(Slot, It->second);
}

void Ledger_Forget(unsigned int SessionId)
{
    GLedgers.erase(SessionId);
}

bool Ledger_OnGoodBuy(PlayerSlot& Slot, unsigned int SessionId, SGFGoodBuyInfo const& Info)
{
    if (SinglePlayer())
        return true;
    // {base, 0, good, count} (ClientMarketRequestBuy, client_base_market.cpp).
    unsigned int const BaseId = Info.Field_0x00;
    unsigned int const GoodId = Info.Field_0x08;
    long long const Count = static_cast<int>(Info.Field_0x0c);
    if (!Docked(Slot) || BaseId != Slot.current_base || Count < 1)
        return false;
    BaseData const* const Base = FindBaseData(BaseId);
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    if (Good == nullptr || !Sells(Base, GoodId))
        return false;

    FLedger& Ledger = LedgerOf(SessionId);
    // A gun's ammo package comes after the gun is paid for, and its price is part of the
    // gun's package price (NnInventory::GetMarketPackagePrice), so it adds no cost here.
    if (Ledger.FreeAmmoLeft > 0 && GoodId == Ledger.FreeAmmoGood && Count <= Ledger.FreeAmmoLeft)
    {
        Ledger.FreeAmmoLeft -= static_cast<int>(Count);
        Ledger.Allowance[Good->ArchId] += Count;
        return true;
    }

    SettleAll(Slot, Ledger);
    if (Good->Category == GoodCategoryShip)
    {
        if (Count != 1)
            return false;
        Ledger.bShip = true;
        Ledger.ShipGood = GoodId;
        Ledger.StartCash = Slot.cash;
        Ledger.OldCounts.clear();
        CountByArch(Slot.equipment, Ledger.OldCounts);
        Ledger.OldHull = Slot.ship_archetype;
        Ledger.bHullSet = false;
        return true;
    }

    Ledger.Allowance[Good->ArchId] += Count;
    Ledger.Balance -= Count * MinUnit(Base, GoodId);
    if (Good->FreeAmmoArchId != 0 && Good->FreeAmmoCount > 0)
    {
        Ledger.FreeAmmoGood = Arch2Good(Good->FreeAmmoArchId);
        Ledger.FreeAmmoLeft = Good->FreeAmmoCount;
    }
    return true;
}

bool Ledger_OnGoodSell(PlayerSlot& Slot, unsigned int SessionId, SGFGoodSellInfo const& Info)
{
    if (SinglePlayer())
        return true;
    // {base, good, count} (ClientMarketRequestSell). The client sells any good it holds,
    // listed by the base or not, so the market is not checked.
    unsigned int const BaseId = Info.Field_0x00;
    FLedger& Ledger = LedgerOf(SessionId);
    if (!Docked(Slot) || BaseId != Slot.current_base || static_cast<int>(Info.Field_0x08) < 1)
    {
        DropCredit(Slot, Ledger);
        return false;
    }
    if (Ledger.bCredit && !Ledger.bSaleNamed)
    {
        Ledger.bSaleNamed = true;
        Ledger.SaleGood = Info.Field_0x04;
        Ledger.SaleCount = static_cast<int>(Info.Field_0x08);
    }
    else
    {
        DropCredit(Slot, Ledger);
    }
    return true;
}

bool Ledger_MayAddItem(PlayerSlot& Slot, unsigned int SessionId, unsigned int ArchId, int Count,
                       float Health)
{
    if (SinglePlayer())
        return true;
    // The dealer adds what was just bought, at full health (BuyFromDealer).
    FLedger& Ledger = LedgerOf(SessionId);
    std::map<unsigned int, long long>::iterator It = Ledger.Allowance.find(ArchId);
    if (Count < 1 || !(Health <= 1.0f) || It == Ledger.Allowance.end() || It->second < Count)
    {
        ResyncEquipment(Slot);
        return false;
    }
    It->second -= Count;
    return true;
}

bool Ledger_MayModifyItem(PlayerSlot& Slot, unsigned int SessionId, unsigned short EquipId,
                          int Count, float Health)
{
    (void)SessionId;
    if (SinglePlayer())
        return true;
    // Mounting and unmounting resend the item's own count and health (nn_inventory.cpp).
    EquipDesc const* const Item = Slot.equipment.find_equipment_item(EquipId);
    if (Item == nullptr || Count < 1 || Count > Item->Count || !(Health <= Item->Health + 0.001f))
    {
        ResyncEquipment(Slot);
        return false;
    }
    return true;
}

bool Ledger_MayLoadEquipment(PlayerSlot& Slot, unsigned int SessionId,
                             EquipDescList const& Equipment)
{
    if (SinglePlayer())
        return true;
    // Mounting, unmounting and repair resend the same items (repair at full health); a ship
    // purchase sends the package's addons plus what comes over from the old ship.
    FLedger& Ledger = LedgerOf(SessionId);
    std::map<unsigned int, long long> Allowed;
    if (Ledger.bShip)
    {
        Allowed = Ledger.OldCounts;
        GoodInfo const* const Package = GoodList::find_by_id(Ledger.ShipGood);
        if (Package != nullptr)
        {
            std::vector<EquipDesc> const* const Lists[3] = {&Package->AddonLow, &Package->AddonMid,
                                                            &Package->AddonHigh};
            for (std::vector<EquipDesc> const* List : Lists)
                for (std::vector<EquipDesc>::const_iterator It = List->begin(); It != List->end();
                     ++It)
                    if (It->Count > 0)
                        Allowed[It->ArchId] += It->Count;
        }
    }
    else
    {
        CountByArch(Slot.equipment, Allowed);
    }

    std::map<unsigned int, long long> Wanted;
    bool bOk = true;
    for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin();
         It != Equipment.Items.end(); ++It)
    {
        if (It->Count < 0 || !(It->Health <= 1.0f + 0.001f))
            bOk = false;
        else
            Wanted[It->ArchId] += It->Count;
    }
    for (std::map<unsigned int, long long>::const_iterator It = Wanted.begin();
         bOk && It != Wanted.end(); ++It)
        if (It->second > Allowed[It->first])
            bOk = false;
    if (!bOk)
        ResyncEquipment(Slot);
    return bOk;
}

bool Ledger_MaySetShipArch(PlayerSlot& Slot, unsigned int SessionId, unsigned int ArchId)
{
    if (SinglePlayer())
        return true;
    // Only the hull of the package being bought (NnDealer::BuyShip: Hull->ShipArchId).
    FLedger& Ledger = LedgerOf(SessionId);
    GoodInfo const* const Package = Ledger.bShip ? GoodList::find_by_id(Ledger.ShipGood) : nullptr;
    GoodInfo const* const Hull =
        Package != nullptr ? GoodList::find_by_id(Package->HullGoodId) : nullptr;
    if (Hull == nullptr || Hull->ShipArchId != ArchId)
    {
        g_pClientInterface->SendServerSetShipArch(Slot.client_id, Slot.ship_archetype);
        return false;
    }
    return true;
}

void Ledger_OnShipArchSet(PlayerSlot& Slot, unsigned int SessionId)
{
    if (SinglePlayer())
        return;
    FLedger& Ledger = LedgerOf(SessionId);
    Ledger.bHullSet = true;
    FinishShip(Slot, Ledger);
}

bool Ledger_MaySetCash(PlayerSlot& Slot, unsigned int SessionId, int Cash)
{
    (void)Cash;
    if (SinglePlayer())
        return true;
    // Only a ship purchase sets the cash outright; FinishShip holds it to what the trade
    // could have paid.
    if (!LedgerOf(SessionId).bShip)
    {
        ResyncCash(Slot);
        return false;
    }
    return true;
}

bool Ledger_OnChangeCash(PlayerSlot& Slot, unsigned int SessionId, int Delta)
{
    if (SinglePlayer())
        return true;
    FLedger& Ledger = LedgerOf(SessionId);
    if (Delta <= 0)
    {
        Ledger.Balance -= Delta;   // a payment
        return true;
    }
    // A sale credit comes first, then GFGoodSell and ReqRemoveItem (SellSelected). It is paid
    // when the removal completes the sale.
    SettleAll(Slot, Ledger);
    Ledger.bCredit = true;
    Ledger.Credit = Delta;
    return false;
}

void Ledger_RemoveItem(PlayerSlot& Slot, unsigned int SessionId, unsigned short EquipId, int Count)
{
    unsigned int ArchId = 0;
    int Held = 0;
    if (EquipDesc const* const Item = Slot.equipment.find_equipment_item(EquipId))
    {
        ArchId = Item->ArchId;
        Held = Item->Count;
    }
    PlayerData_RemoveEquipment(Slot, EquipId, Count, /*bNotify*/ false);
    if (SinglePlayer())
        return;

    FLedger& Ledger = LedgerOf(SessionId);
    if (!Ledger.bCredit)
        return;
    if (!Ledger.bSaleNamed || ArchId == 0 || Count < 1 || Arch2Good(ArchId) != Ledger.SaleGood)
    {
        DropCredit(Slot, Ledger);
        return;
    }
    // The client pays price * count, or health * count * (0.3 * price + 0.5) for a used item,
    // at its scaled price (SellSelected).
    long long const Sold = std::min<long long>((std::min)(Count, Held), Ledger.SaleCount);
    long long const Bound = Sold * MaxUnit(FindBaseData(Slot.current_base), Ledger.SaleGood);
    long long const Pay = std::min<long long>(Ledger.Credit, Bound);
    Ledger.bCredit = false;
    Ledger.Credit = 0;
    Ledger.bSaleNamed = false;
    PlayerData_SetCash(Slot, ClampCash(static_cast<long long>(Slot.cash) + Pay));
}
