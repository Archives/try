/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Database/SQLStorage.h"
#include "DBCStores.h"
#include "ProgressBar.h"

#include "AccountMgr.h"
#include "AuctionHouseMgr.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Mail.h"

#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1( AuctionHouseMgr );

AuctionHouseMgr::AuctionHouseMgr()
{
}

AuctionHouseMgr::~AuctionHouseMgr()
{
    for(ItemMap::const_iterator itr = mAitems.begin(); itr != mAitems.end(); ++itr)
        delete itr->second;
}

AuctionHouseObject * AuctionHouseMgr::GetAuctionsMap( uint32 factionTemplateId )
{
    if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
        return &mNeutralAuctions;

    // team have linked auction houses
    FactionTemplateEntry const* u_entry = sFactionTemplateStore.LookupEntry(factionTemplateId);
    if (!u_entry)
        return &mNeutralAuctions;
    else if (u_entry->ourMask & FACTION_MASK_ALLIANCE)
        return &mAllianceAuctions;
    else if (u_entry->ourMask & FACTION_MASK_HORDE)
        return &mHordeAuctions;
    else
        return &mNeutralAuctions;
}

uint32 AuctionHouseMgr::GetAuctionDeposit(AuctionHouseEntry const* entry, uint32 time, Item *pItem)
{
    float deposit = float(pItem->GetProto()->SellPrice * pItem->GetCount() * (time / MIN_AUCTION_TIME ));

    deposit = deposit * entry->depositPercent * 3.0f / 100.0f;

    float min_deposit = float(sWorld.getConfig(CONFIG_UINT32_AUCTION_DEPOSIT_MIN));

    if (deposit < min_deposit)
        deposit = min_deposit;

    return uint32(deposit * sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_DEPOSIT));
}

//does not clear ram
void AuctionHouseMgr::SendAuctionWonMail( AuctionEntry *auction )
{
    Item *pItem = GetAItem(auction->item_guidlow);
    if (!pItem)
        return;

    uint64 bidder_guid = MAKE_NEW_GUID(auction->bidder, 0, HIGHGUID_PLAYER);
    Player *bidder = sObjectMgr.GetPlayer(bidder_guid);

    uint32 bidder_accId = 0;

    // data for gm.log
    if ( sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE) )
    {
        uint32 bidder_security = 0;
        std::string bidder_name;
        if (bidder)
        {
            bidder_accId = bidder->GetSession()->GetAccountId();
            bidder_security = bidder->GetSession()->GetSecurity();
            bidder_name = bidder->GetName();
        }
        else
        {
            bidder_accId = sObjectMgr.GetPlayerAccountIdByGUID(bidder_guid);
            bidder_security = sAccountMgr.GetSecurity(bidder_accId);

            if (bidder_security > SEC_PLAYER )               // not do redundant DB requests
            {
                if (!sObjectMgr.GetPlayerNameByGUID(bidder_guid,bidder_name))
                    bidder_name = sObjectMgr.GetMangosStringForDBCLocale(LANG_UNKNOWN);
            }
        }

        if ( bidder_security > SEC_PLAYER )
        {
            std::string owner_name;
            if (!sObjectMgr.GetPlayerNameByGUID(auction->owner,owner_name))
                owner_name = sObjectMgr.GetMangosStringForDBCLocale(LANG_UNKNOWN);

            uint32 owner_accid = sObjectMgr.GetPlayerAccountIdByGUID(auction->owner);

            sLog.outCommand(bidder_accId,"GM %s (Account: %u) won item in auction: %s (Entry: %u Count: %u) and pay money: %u. Original owner %s (Account: %u)",
                bidder_name.c_str(),bidder_accId,pItem->GetProto()->Name1,pItem->GetEntry(),pItem->GetCount(),auction->bid,owner_name.c_str(),owner_accid);
        }
    }
    else if (!bidder)
        bidder_accId = sObjectMgr.GetPlayerAccountIdByGUID(bidder_guid);

    // receiver exist
    if (bidder || bidder_accId)
    {
        std::ostringstream msgAuctionWonSubject;
        msgAuctionWonSubject << auction->item_template << ":0:" << AUCTION_WON;

        std::ostringstream msgAuctionWonBody;
        msgAuctionWonBody.width(16);
        msgAuctionWonBody << std::right << std::hex << auction->owner;
        msgAuctionWonBody << std::dec << ":" << auction->bid << ":" << auction->buyout;
        DEBUG_LOG( "AuctionWon body string : %s", msgAuctionWonBody.str().c_str() );

        // set owner to bidder (to prevent delete item with sender char deleting)
        // owner in `data` will set at mail receive and item extracting
        CharacterDatabase.PExecute("UPDATE item_instance SET owner_guid = '%u' WHERE guid='%u'",auction->bidder,pItem->GetGUIDLow());
        CharacterDatabase.CommitTransaction();

        if (bidder)
        {
            bidder->GetSession()->SendAuctionBidderNotification( auction->GetHouseId(), auction->Id, bidder_guid, 0, 0, auction->item_template);
            // FIXME: for offline player need also
            bidder->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WON_AUCTIONS, 1);
        }
        else
            RemoveAItem(pItem->GetGUIDLow());               // we have to remove the item, before we delete it !!

        // will delete item or place to receiver mail list
        MailDraft(msgAuctionWonSubject.str(), msgAuctionWonBody.str())
            .AddItem(pItem)
            .SendMailTo(MailReceiver(bidder,auction->bidder), auction, MAIL_CHECK_MASK_COPIED);
    }
    // receiver not exist
    else
    {
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid='%u'", pItem->GetGUIDLow());
        RemoveAItem(pItem->GetGUIDLow());                   // we have to remove the item, before we delete it !!
        delete pItem;
    }
}

void AuctionHouseMgr::SendAuctionSalePendingMail( AuctionEntry * auction )
{
    uint64 owner_guid = MAKE_NEW_GUID(auction->owner, 0, HIGHGUID_PLAYER);
    Player *owner = sObjectMgr.GetPlayer(owner_guid);

    // owner exist (online or offline)
    if (owner || sObjectMgr.GetPlayerAccountIdByGUID(owner_guid))
    {
        std::ostringstream msgAuctionSalePendingSubject;
        msgAuctionSalePendingSubject << auction->item_template << ":0:" << AUCTION_SALE_PENDING;

        std::ostringstream msgAuctionSalePendingBody;
        uint32 auctionCut = auction->GetAuctionCut();

        time_t distrTime = time(NULL) + HOUR;

        msgAuctionSalePendingBody.width(16);
        msgAuctionSalePendingBody << std::right << std::hex << auction->bidder;
        msgAuctionSalePendingBody << std::dec << ":" << auction->bid << ":" << auction->buyout;
        msgAuctionSalePendingBody << ":" << auction->deposit << ":" << auctionCut << ":0:";
        msgAuctionSalePendingBody << secsToTimeBitFields(distrTime);

        DEBUG_LOG("AuctionSalePending body string : %s", msgAuctionSalePendingBody.str().c_str());

        MailDraft(msgAuctionSalePendingSubject.str(), msgAuctionSalePendingBody.str())
            .SendMailTo(MailReceiver(owner,auction->owner), auction, MAIL_CHECK_MASK_COPIED);
    }
}

//call this method to send mail to auction owner, when auction is successful, it does not clear ram
void AuctionHouseMgr::SendAuctionSuccessfulMail( AuctionEntry * auction )
{
    uint64 owner_guid = MAKE_NEW_GUID(auction->owner, 0, HIGHGUID_PLAYER);
    Player *owner = sObjectMgr.GetPlayer(owner_guid);

    uint32 owner_accId = 0;
    if (!owner)
        owner_accId = sObjectMgr.GetPlayerAccountIdByGUID(owner_guid);

    // owner exist
    if (owner || owner_accId)
    {
        std::ostringstream msgAuctionSuccessfulSubject;
        msgAuctionSuccessfulSubject << auction->item_template << ":0:" << AUCTION_SUCCESSFUL;

        std::ostringstream auctionSuccessfulBody;
        uint32 auctionCut = auction->GetAuctionCut();

        auctionSuccessfulBody.width(16);
        auctionSuccessfulBody << std::right << std::hex << auction->bidder;
        auctionSuccessfulBody << std::dec << ":" << auction->bid << ":" << auction->buyout;
        auctionSuccessfulBody << ":" << auction->deposit << ":" << auctionCut;

        DEBUG_LOG("AuctionSuccessful body string : %s", auctionSuccessfulBody.str().c_str());

        uint32 profit = auction->bid + auction->deposit - auctionCut;

        if (owner)
        {
            //FIXME: what do if owner offline
            owner->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GOLD_EARNED_BY_AUCTIONS, profit);
            owner->GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_SOLD, auction->bid);
            //send auction owner notification, bidder must be current!
            owner->GetSession()->SendAuctionOwnerNotification( auction );
        }

        MailDraft(msgAuctionSuccessfulSubject.str(), auctionSuccessfulBody.str())
            .AddMoney(profit)
            .SendMailTo(MailReceiver(owner,auction->owner), auction, MAIL_CHECK_MASK_COPIED, HOUR);
    }
}

//does not clear ram
void AuctionHouseMgr::SendAuctionExpiredMail( AuctionEntry * auction )
{                                                           //return an item in auction to its owner by mail
    Item *pItem = GetAItem(auction->item_guidlow);
    if (!pItem)
    {
        sLog.outError("Auction item (GUID: %u) not found, and lost.",auction->item_guidlow);
        return;
    }

    uint64 owner_guid = MAKE_NEW_GUID(auction->owner, 0, HIGHGUID_PLAYER);
    Player *owner = sObjectMgr.GetPlayer(owner_guid);

    uint32 owner_accId = 0;
    if (!owner)
        owner_accId = sObjectMgr.GetPlayerAccountIdByGUID(owner_guid);

    // owner exist
    if (owner || owner_accId)
    {
        std::ostringstream subject;
        subject << auction->item_template << ":0:" << AUCTION_EXPIRED << ":0:0";

        if ( owner )
            owner->GetSession()->SendAuctionOwnerNotification( auction );
        else
            RemoveAItem(pItem->GetGUIDLow());               // we have to remove the item, before we delete it !!

        // will delete item or place to receiver mail list
        MailDraft(subject.str(), "")                        // TODO: fix body
            .AddItem(pItem)
            .SendMailTo(MailReceiver(owner,auction->owner), auction, MAIL_CHECK_MASK_COPIED);
    }
    // owner not found
    else
    {
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid='%u'",pItem->GetGUIDLow());
        RemoveAItem(pItem->GetGUIDLow());                   // we have to remove the item, before we delete it !!
        delete pItem;
    }
}

void AuctionHouseMgr::LoadAuctionItems()
{
    // data needs to be at first place for Item::LoadFromDB 0   1    2        3
    QueryResult *result = CharacterDatabase.Query( "SELECT data,text,itemguid,item_template FROM auctionhouse JOIN item_instance ON itemguid = guid" );

    if ( !result )
    {
        barGoLink bar(1);
        bar.step();
        sLog.outString();
        sLog.outString(">> Loaded 0 auction items");
        return;
    }

    barGoLink bar( (int)result->GetRowCount() );

    uint32 count = 0;

    Field *fields;
    do
    {
        bar.step();

        fields = result->Fetch();
        uint32 item_guid        = fields[2].GetUInt32();
        uint32 item_template    = fields[3].GetUInt32();

        ItemPrototype const *proto = ObjectMgr::GetItemPrototype(item_template);

        if (!proto)
        {
            sLog.outError( "AuctionHouseMgr::LoadAuctionItems: Unknown item (GUID: %u id: #%u) in auction, skipped.", item_guid,item_template);
            continue;
        }

        Item *item = NewItemOrBag(proto);

        if (!item->LoadFromDB(item_guid,0, result))
        {
            delete item;
            continue;
        }
        AddAItem(item);

        ++count;
    }
    while( result->NextRow() );
    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u auction items", count );
}

void AuctionHouseMgr::LoadAuctions()
{
    QueryResult *result = CharacterDatabase.Query("SELECT COUNT(*) FROM auctionhouse");
    if ( !result )
    {
        barGoLink bar(1);
        bar.step();
        sLog.outString();
        sLog.outString(">> Loaded 0 auctions. DB table `auctionhouse` is empty.");
        return;
    }

    Field *fields = result->Fetch();
    uint32 AuctionCount=fields[0].GetUInt32();
    delete result;

    if (!AuctionCount)
    {
        barGoLink bar(1);
        bar.step();
        sLog.outString();
        sLog.outString(">> Loaded 0 auctions. DB table `auctionhouse` is empty.");
        return;
    }

    result = CharacterDatabase.Query( "SELECT id,auctioneerguid,itemguid,item_template,itemowner,buyoutprice,time,buyguid,lastbid,startbid,deposit FROM auctionhouse" );
    if ( !result )
    {
        barGoLink bar(1);
        bar.step();
        sLog.outString();
        sLog.outString(">> Loaded 0 auctions. DB table `auctionhouse` is empty.");
        return;
    }

    barGoLink bar( AuctionCount );

    AuctionEntry *aItem;

    do
    {
        fields = result->Fetch();

        bar.step();

        aItem = new AuctionEntry;
        aItem->Id = fields[0].GetUInt32();
        aItem->auctioneer = fields[1].GetUInt32();
        aItem->item_guidlow = fields[2].GetUInt32();
        aItem->item_template = fields[3].GetUInt32();
        aItem->owner = fields[4].GetUInt32();
        aItem->buyout = fields[5].GetUInt32();
        aItem->expire_time = fields[6].GetUInt32();
        aItem->bidder = fields[7].GetUInt32();
        aItem->bid = fields[8].GetUInt32();
        aItem->startbid = fields[9].GetUInt32();
        aItem->deposit = fields[10].GetUInt32();

        CreatureData const* auctioneerData = sObjectMgr.GetCreatureData(aItem->auctioneer);
        if (!auctioneerData)
        {
            aItem->DeleteFromDB();
            sLog.outError("Auction %u has not a existing auctioneer (GUID : %u)", aItem->Id, aItem->auctioneer);
            delete aItem;
            continue;
        }

        CreatureInfo const* auctioneerInfo = ObjectMgr::GetCreatureTemplate(auctioneerData->id);
        if (!auctioneerInfo)
        {
            aItem->DeleteFromDB();
            sLog.outError("Auction %u has not a existing auctioneer (GUID : %u Entry: %u)", aItem->Id, aItem->auctioneer,auctioneerData->id);
            delete aItem;
            continue;
        }

        aItem->auctionHouseEntry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneerInfo->faction_A);
        if (!aItem->auctionHouseEntry)
        {
            aItem->DeleteFromDB();
            sLog.outError("Auction %u has auctioneer (GUID : %u Entry: %u) with wrong faction %u",
                aItem->Id, aItem->auctioneer,auctioneerData->id,auctioneerInfo->faction_A);
            delete aItem;
            continue;
        }

        // check if sold item exists for guid
        // and item_template in fact (GetAItem will fail if problematic in result check in AuctionHouseMgr::LoadAuctionItems)
        if ( !GetAItem( aItem->item_guidlow ) )
        {
            aItem->DeleteFromDB();
            sLog.outError("Auction %u has not a existing item : %u", aItem->Id, aItem->item_guidlow);
            delete aItem;
            continue;
        }

        GetAuctionsMap( auctioneerInfo->faction_A )->AddAuction(aItem);

    } while (result->NextRow());
    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u auctions", AuctionCount );
}

void AuctionHouseMgr::AddAItem( Item* it )
{
    ASSERT( it );
    ASSERT( mAitems.find(it->GetGUIDLow()) == mAitems.end());
    mAitems[it->GetGUIDLow()] = it;
}

bool AuctionHouseMgr::RemoveAItem( uint32 id )
{
    ItemMap::iterator i = mAitems.find(id);
    if (i == mAitems.end())
    {
        return false;
    }
    mAitems.erase(i);
    return true;
}

void AuctionHouseMgr::Update()
{
    mHordeAuctions.Update();
    mAllianceAuctions.Update();
    mNeutralAuctions.Update();
}

AuctionHouseEntry const* AuctionHouseMgr::GetAuctionHouseEntry(uint32 factionTemplateId)
{
    uint32 houseid = 1;                                     // dwarf auction house (used for normal cut/etc percents)

    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        //FIXME: found way for proper auctionhouse selection by another way
        // AuctionHo use.dbc have faction field with _player_ factions associated with auction house races.
        // but no easy way convert creature faction to player race faction for specific city
        switch(factionTemplateId)
        {
            case   12: houseid = 1; break;                  // human
            case   29: houseid = 6; break;                  // orc, and generic for horde
            case   55: houseid = 2; break;                  // dwarf, and generic for alliance
            case   68: houseid = 4; break;                  // undead
            case   80: houseid = 3; break;                  // n-elf
            case  104: houseid = 5; break;                  // trolls
            case  120: houseid = 7; break;                  // booty bay, neutral
            case  474: houseid = 7; break;                  // gadgetzan, neutral
            case  855: houseid = 7; break;                  // everlook, neutral
            case 1604: houseid = 6; break;                  // b-elfs,
            default:                                        // for unknown case
            {
                FactionTemplateEntry const* u_entry = sFactionTemplateStore.LookupEntry(factionTemplateId);
                if (!u_entry)
                    houseid = 7;                            // goblin auction house
                else if (u_entry->ourMask & FACTION_MASK_ALLIANCE)
                    houseid = 1;                            // human auction house
                else if (u_entry->ourMask & FACTION_MASK_HORDE)
                    houseid = 6;                            // orc auction house
                else
                    houseid = 7;                            // goblin auction house
                break;
            }
        }
    }

    return sAuctionHouseStore.LookupEntry(houseid);
}

void AuctionHouseObject::Update()
{
    time_t curTime = sWorld.GetGameTime();
    ///- Handle expired auctions
    AuctionEntryMap::iterator next;
    for (AuctionEntryMap::iterator itr = AuctionsMap.begin(); itr != AuctionsMap.end();itr = next)
    {
        next = itr;
        ++next;
        if (curTime > (itr->second->expire_time))
        {
            ///- Either cancel the auction if there was no bidder
            if (itr->second->bidder == 0)
            {
                sAuctionMgr.SendAuctionExpiredMail( itr->second );
            }
            ///- Or perform the transaction
            else
            {
                //we should send an "item sold" message if the seller is online
                //we send the item to the winner
                //we send the money to the seller
                sAuctionMgr.SendAuctionSuccessfulMail( itr->second );
                sAuctionMgr.SendAuctionWonMail( itr->second );
            }

            ///- In any case clear the auction
            itr->second->DeleteFromDB();
            sAuctionMgr.RemoveAItem(itr->second->item_guidlow);
            delete itr->second;
            RemoveAuction(itr->first);
        }
    }
}

void AuctionHouseObject::BuildListBidderItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount)
{
    for (AuctionEntryMap::const_iterator itr = AuctionsMap.begin();itr != AuctionsMap.end();++itr)
    {
        AuctionEntry *Aentry = itr->second;
        if ( Aentry && Aentry->bidder == player->GetGUIDLow() )
        {
            if (itr->second->BuildAuctionInfo(data))
                ++count;
            ++totalcount;
        }
    }
}

void AuctionHouseObject::BuildListOwnerItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount)
{
    for (AuctionEntryMap::const_iterator itr = AuctionsMap.begin();itr != AuctionsMap.end();++itr)
    {
        AuctionEntry *Aentry = itr->second;
        if ( Aentry && Aentry->owner == player->GetGUIDLow() )
        {
            if (Aentry->BuildAuctionInfo(data))
                ++count;
            ++totalcount;
        }
    }
}

void AuctionHouseObject::BuildListAuctionItems(WorldPacket& data, Player* player,
    std::wstring const& wsearchedname, uint32 listfrom, uint32 levelmin, uint32 levelmax, uint32 usable,
    uint32 inventoryType, uint32 itemClass, uint32 itemSubClass, uint32 quality,
    uint32& count, uint32& totalcount)
{
    int loc_idx = player->GetSession()->GetSessionDbLocaleIndex();

    for (AuctionEntryMap::const_iterator itr = AuctionsMap.begin();itr != AuctionsMap.end();++itr)
    {
        AuctionEntry *Aentry = itr->second;
        Item *item = sAuctionMgr.GetAItem(Aentry->item_guidlow);
        if (!item)
            continue;

        ItemPrototype const *proto = item->GetProto();

        if (itemClass != 0xffffffff && proto->Class != itemClass)
            continue;

        if (itemSubClass != 0xffffffff && proto->SubClass != itemSubClass)
            continue;

        if (inventoryType != 0xffffffff && proto->InventoryType != inventoryType)
            continue;

        if (quality != 0xffffffff && proto->Quality != quality)
            continue;

        if (levelmin != 0x00 && (proto->RequiredLevel < levelmin || (levelmax != 0x00 && proto->RequiredLevel > levelmax)))
            continue;

        if (usable != 0x00 && player->CanUseItem( item ) != EQUIP_ERR_OK)
            continue;

        std::string name = proto->Name1;
        if (name.empty())
            continue;

        // local name
        if ( loc_idx >= 0 )
        {
            ItemLocale const *il = sObjectMgr.GetItemLocale(proto->ItemId);
            if (il)
            {
                if (il->Name.size() > size_t(loc_idx) && !il->Name[loc_idx].empty())
                    name = il->Name[loc_idx];
            }
        }

        if (!wsearchedname.empty() && !Utf8FitTo(name, wsearchedname) )
            continue;

        if (count < 50 && totalcount >= listfrom)
        {
            ++count;
            Aentry->BuildAuctionInfo(data);
        }
        ++totalcount;
    }
}

//this function inserts to WorldPacket auction's data
bool AuctionEntry::BuildAuctionInfo(WorldPacket & data) const
{
    Item *pItem = sAuctionMgr.GetAItem(item_guidlow);
    if (!pItem)
    {
        sLog.outError("auction to item, that doesn't exist !!!!");
        return false;
    }
    data << uint32(Id);
    data << uint32(pItem->GetEntry());

    for (uint8 i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
    {
        data << uint32(pItem->GetEnchantmentId(EnchantmentSlot(i)));
        data << uint32(pItem->GetEnchantmentDuration(EnchantmentSlot(i)));
        data << uint32(pItem->GetEnchantmentCharges(EnchantmentSlot(i)));
    }

    data << uint32(pItem->GetItemRandomPropertyId());       //random item property id
    data << uint32(pItem->GetItemSuffixFactor());           //SuffixFactor
    data << uint32(pItem->GetCount());                      //item->count
    data << uint32(pItem->GetSpellCharges());               //item->charge FFFFFFF
    data << uint32(0);                                      //Unknown
    data << uint64(owner);                                  //Auction->owner
    data << uint32(startbid);                               //Auction->startbid (not sure if useful)
    data << uint32(bid ? GetAuctionOutBid() : 0);
    //minimal outbid
    data << uint32(buyout);                                 //auction->buyout
    data << uint32((expire_time-time(NULL))*IN_MILLISECONDS);//time left
    data << uint64(bidder) ;                                //auction->bidder current
    data << uint32(bid);                                    //current bid
    return true;
}

uint32 AuctionEntry::GetAuctionCut() const
{
    return uint32(auctionHouseEntry->cutPercent * bid * sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_CUT) / 100.0f);
}

/// the sum of outbid is (1% from current bid)*5, if bid is very small, it is 1c
uint32 AuctionEntry::GetAuctionOutBid() const
{
    uint32 outbid = (bid / 100) * 5;
    if (!outbid)
        outbid = 1;
    return outbid;
}

void AuctionEntry::DeleteFromDB() const
{
    //No SQL injection (Id is integer)
    CharacterDatabase.PExecute("DELETE FROM auctionhouse WHERE id = '%u'",Id);
}

void AuctionEntry::SaveToDB() const
{
    //No SQL injection (no strings)
    CharacterDatabase.PExecute("INSERT INTO auctionhouse (id,auctioneerguid,itemguid,item_template,itemowner,buyoutprice,time,buyguid,lastbid,startbid,deposit) "
        "VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '" UI64FMTD "', '%u', '%u', '%u', '%u')",
        Id, auctioneer, item_guidlow, item_template, owner, buyout, (uint64)expire_time, bidder, bid, startbid, deposit);
}
