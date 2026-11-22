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
    // A refused GFGoodBuy: the client sends its payment anyway (BuyFromDealer does not wait),
    // and that payment, the next cash change, is refused too.
    bool bRefusedBuy = false;
    // The price the client paid per unit this dock, by good. A dock's prices are fixed
    // (LoadLocationBase fills the market once per dock), and the client buys and sells a good
    // at the same price, so a sale this dock pays no more than that.
    unsigned int PricedGood = 0; // the purchase whose payment is due next
    long long PricedCount = 0;
    std::map<unsigned int, long long> UnitPaid;

    bool bCredit = false;                  // a sale credit, waiting for its sale
    int Credit = 0;
    bool bSaleNamed = false;               // GFGoodSell has named the good
    unsigned int SaleGood = 0;
    int SaleCount = 0;

    bool bShip = false;                    // a ship purchase (NnDealer::BuyShip) is open
    unsigned int ShipGood = 0;             // the package
    int StartCash = 0;
    std::map<unsigned int, long long> OldCounts;
    EquipDescList OldEquipment; // restored when the purchase does not complete
    bool bEquipmentSet = false; // ReqEquipment changed the loadout
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
// (NnLoadoutResaleValue counts each window's own items). So, with the new hull, the cash
// may be at most the cash before, less the least the hull costs, plus the old hull's price
// and the price of every item, old or from the package, that the new loadout leaves behind.
long long ShipCeiling(PlayerSlot const& Slot, FLedger const& Ledger)
{
    BaseData const* const Base = FindBaseData(Slot.current_base);
    GoodInfo const* const Package = GoodList::find_by_id(Ledger.ShipGood);
    GoodInfo const* const Hull =
        Package != nullptr ? GoodList::find_by_id(Package->HullGoodId) : nullptr;

    long long Ceiling = Ledger.StartCash;
    if (Hull != nullptr)
        Ceiling -= MinUnit(Base, Hull->Id);
    GoodInfo const* const OldHull = GoodList_get()->find_by_ship_arch(Ledger.OldHull);
    if (OldHull != nullptr)
        Ceiling += MaxUnit(Base, OldHull->Id);

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
    return Ceiling;
    }

    // A ship purchase that did not complete, or that the player could not pay for: the
    // loadout and cash it changed go back to what they were.
    void RollBackShip(PlayerSlot& Slot, FLedger& Ledger)
    {
        Ledger.bShip = false;
        if (Ledger.bEquipmentSet)
            PlayerData_LoadEquipment(Slot, Ledger.OldEquipment);
        if (Slot.cash != Ledger.StartCash)
            PlayerData_SetCash(Slot, Ledger.StartCash);
    }

    // An open ship purchase is closed: complete when the hull arrived (cash held to the
    // ceiling), otherwise undone.
    void FinishShip(PlayerSlot& Slot, FLedger& Ledger)
    {
        if (!Ledger.bShip)
            return;
        if (!Ledger.bHullSet)
        {
            RollBackShip(Slot, Ledger);
            return;
        }
        Ledger.bShip = false;
        long long const Ceiling = ShipCeiling(Slot, Ledger);
        if (Slot.cash > Ceiling)
            PlayerData_SetCash(Slot, ClampCash(Ceiling));
    }

void SettleAll(PlayerSlot& Slot, FLedger& Ledger)
{
    Ledger.bRefusedBuy = false;
    Ledger.PricedGood = 0;
    Ledger.PricedCount = 0;
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
    FLedger& Ledger = LedgerOf(SessionId);

    // A gun's ammo package comes after the gun is paid for, and its price is part of the
    // gun's package price (NnInventory::GetMarketPackagePrice), so it adds no cost here. No
    // payment follows it, so a refused one leaves the next cash change alone.
    bool const bAmmo = Ledger.FreeAmmoLeft > 0 && GoodId == Ledger.FreeAmmoGood;

    BaseData const* const Base = FindBaseData(BaseId);
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    bool bOk = Docked(Slot) && BaseId == Slot.current_base && Count >= 1 && Good != nullptr &&
               Sells(Base, GoodId);
    if (bOk && bAmmo)
    {
        if (Count > Ledger.FreeAmmoLeft)
            return false;
        Ledger.FreeAmmoLeft -= static_cast<int>(Count);
        Ledger.Allowance[Good->ArchId] += Count;
        return true;
    }
    if (bOk)
        SettleAll(Slot, Ledger);
    // The dealer refuses what the player cannot pay for, at the client's price, which is never
    // below MinUnit. (A ship's price is only known once the trade-in is: Ledger_MaySetShipArch.)
    if (bOk && Good->Category != GoodCategoryShip &&
        Count * MinUnit(Base, GoodId) > static_cast<long long>(Slot.cash))
        bOk = false;
    if (bOk && Good->Category == GoodCategoryShip && Count != 1)
        bOk = false;
    if (!bOk)
    {
        if (!bAmmo)
            Ledger.bRefusedBuy = true;
        return false;
    }

    if (Good->Category == GoodCategoryShip)
    {
        Ledger.bShip = true;
        Ledger.ShipGood = GoodId;
        Ledger.StartCash = Slot.cash;
        Ledger.OldCounts.clear();
        CountByArch(Slot.equipment, Ledger.OldCounts);
        Ledger.OldEquipment = Slot.equipment;
        Ledger.bEquipmentSet = false;
        Ledger.OldHull = Slot.ship_archetype;
        Ledger.bHullSet = false;
        return true;
    }

    Ledger.Allowance[Good->ArchId] += Count;
    Ledger.Balance -= Count * MinUnit(Base, GoodId);
    Ledger.PricedGood = GoodId;
    Ledger.PricedCount = Count;
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
    Ledger.bRefusedBuy = false;
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
    else if (Ledger.bShip)
        Ledger.bEquipmentSet = true;
    return bOk;
}

bool Ledger_MaySetShipArch(PlayerSlot& Slot, unsigned int SessionId, unsigned int ArchId)
{
    if (SinglePlayer())
        return true;
    // Only the hull of the package being bought (NnDealer::BuyShip: Hull->ShipArchId), and
    // only when the player can pay for the loadout it keeps: the cash ceiling of the trade
    // must not be negative (the client checks `credits >= cost` before it sends anything).
    FLedger& Ledger = LedgerOf(SessionId);
    GoodInfo const* const Package = Ledger.bShip ? GoodList::find_by_id(Ledger.ShipGood) : nullptr;
    GoodInfo const* const Hull =
        Package != nullptr ? GoodList::find_by_id(Package->HullGoodId) : nullptr;
    bool const bHull = Hull != nullptr && Hull->ShipArchId == ArchId;
    if (!bHull || ShipCeiling(Slot, Ledger) < 0)
    {
        if (Ledger.bShip)
            RollBackShip(Slot, Ledger);
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
        // The payment for a purchase the server refused: refused too, and the client is told
        // its real cash.
        if (Ledger.bRefusedBuy)
        {
            Ledger.bRefusedBuy = false;
            ResyncCash(Slot);
            return false;
        }
        Ledger.Balance -= Delta;   // a payment
        if (Ledger.PricedCount > 0)
        {
            // The client paid package price x count (BuyFromDealer); one credit of slack.
            Ledger.UnitPaid[Ledger.PricedGood] =
                -static_cast<long long>(Delta) / Ledger.PricedCount + 1;
            Ledger.PricedGood = 0;
            Ledger.PricedCount = 0;
        }
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
    // The client removes at least one (SellSelected is its only sender). remove_equipment_item
    // subtracts the count, so a negative one other than -1 (the whole stack) grows the stack.
    if (!SinglePlayer() && Count < 1)
    {
        FLedger& Ledger = LedgerOf(SessionId);
        DropCredit(Slot, Ledger);
        ResyncEquipment(Slot);
        return;
    }
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
    // A good bought this dock sells for no more than it was bought for: a dock's prices are
    // fixed. Otherwise the highest price the base's variance allows.
    long long Unit = MaxUnit(FindBaseData(Slot.current_base), Ledger.SaleGood);
    std::map<unsigned int, long long>::const_iterator Paid = Ledger.UnitPaid.find(Ledger.SaleGood);
    if (Paid != Ledger.UnitPaid.end() && Paid->second < Unit)
        Unit = Paid->second;
    long long const Bound = Sold * Unit;
    long long const Pay = std::min<long long>(Ledger.Credit, Bound);
    Ledger.bCredit = false;
    Ledger.Credit = 0;
    Ledger.bSaleNamed = false;
    PlayerData_SetCash(Slot, ClampCash(static_cast<long long>(Slot.cash) + Pay));
}
