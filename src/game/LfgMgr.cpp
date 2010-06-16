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
#include "SharedDefines.h"
#include "Player.h"
#include "World.h"
#include "LfgMgr.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ProgressBar.h"

#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1( LfgMgr );


LfgGroup::LfgGroup(uint8 lfgType) : Group()
{
    m_lfgType = lfgType;
}
LfgGroup::~LfgGroup()
{
    
}


void LfgGroup::SendLfgUpdateParty(uint8 updateType, uint32 dungeonEntry)
{
    bool join = false;
    bool extrainfo = false;
    bool queued = false;

    switch(updateType)
    {
        case LFG_UPDATETYPE_JOIN_PROPOSAL:
            extrainfo = true;
            break;
        case LFG_UPDATETYPE_ADDED_TO_QUEUE:
            extrainfo = true;
            join = true;
            queued = true;
            break;
        case LFG_UPDATETYPE_CLEAR_LOCK_LIST:
            // join = true;  // TODO: Sometimes queued and extrainfo - Check ocurrences...
            queued = true;
            break;
        case LFG_UPDATETYPE_PROPOSAL_FOUND:
            extrainfo = true;
            join = true;
            break;
    }

    //size_t packet_size = 100;// 2 + (extrainfo ? 1 : 0) * (5 + !dungeonEntry ? 4 : GetPlayer()->m_lookingForGroup.applyDungeons.size() * 4 + GetPlayer()->m_lookingForGroup.comment.length());
    WorldPacket data(SMSG_LFG_UPDATE_PARTY);
    data << uint8(updateType);                              // Lfg Update type
    data << uint8(extrainfo);                               // Extra info
    if (extrainfo)
    {
        data << uint8(join);                                // LFG Join
        data << uint8(queued);                              // Join the queue
        data << uint8(0);                                   // unk - Always 0
        data << uint8(0);                                   // unk - Always 0
        if (dungeonEntry)
        {
            data << uint8(1);
            data << uint32(dungeonEntry);
        }
        else
        {
           // uint8 size = GetPlayer()->m_lookingForGroup.applyDungeons.size();
            data << uint8(0); //size

            //for (LfgDungeonSet::const_iterator it = GetPlayer()->m_lookingForGroup.applyDungeons.begin(); it != GetPlayer()->m_lookingForGroup.applyDungeons.end(); ++it)
              //  data << uint32(*it);
        }
        //data << GetPlayer()->m_lookingForGroup.comment;
    }
    BroadcastPacket(&data, true);
}

LfgMgr::LfgMgr()
{
    
}

LfgMgr::~LfgMgr()
{

}

void LfgMgr::Update(uint32 diff)
{
    
}

void LfgMgr::SendLfgPartyInfo(Player *plr)
{
    uint32 size = 0;
    //for (/* for every lock */)
      //  size += 8 + 4 + it->second->size() * (4 + 4);
    WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
    data << uint8(0); // number of locks...
    //for(/* for every lock */)
    {
        data << uint64(0);  // guid of player which has lock
        data << uint32(0);                        // Size of lock dungeons for that player
        //for (/* every lock OF THAT PLAYER!! */ )
        {
            data << uint32(0);                     // Dungeon entry + type
            data << uint32(0);                  // Lock status
        }
    }
    plr->GetSession()->SendPacket(&data);
}
void LfgMgr::SendLfgPlayerInfo(Player *plr)
{
    LfgDungeonList *random = GetRandomDungeons(plr);
    LfgLocksList *locks = GetDungeonsLock(plr);
    uint32 rsize = random->size();

    WorldPacket data(SMSG_LFG_PLAYER_INFO);
    if (rsize == 0)
        data << uint8(0);
    else
    {
        data << uint8(rsize);                                      // Random Dungeon count
        for (LfgDungeonList::iterator itr = random->begin(); itr != random->end(); ++itr)
        {
            data << uint32((*itr)->Entry());                       // Entry(ID and type) of random dungeon
            BuildRewardBlock(data, (*itr)->ID, plr);
        }
        random->clear();
        delete random;
    }
    data << uint32(locks->size());
    for (LfgLocksList::iterator itr = locks->begin(); itr != locks->end(); ++itr)
    {
        data << uint32((*itr)->dungeonInfo->Entry());              // Dungeon entry + type
        data << uint32((*itr)->lockType);                          // Lock status
    }
    plr->GetSession()->SendPacket(&data);
}

void LfgMgr::BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr)
{
    bool hasCompletedToday = true;
    LfgReward *reward = GetDungeonReward(dungeon, hasCompletedToday, plr->getLevel());

    if (!reward)
        return;

    data << uint8(hasCompletedToday);  // false = its first run this day, true = it isnt
    if (data.GetOpcode() == SMSG_LFG_PLAYER_REWARD)
        data << uint32(0);             // ???
    data << uint32(reward->questInfo->GetRewOrReqMoney());
    data << uint32((plr->getLevel() == sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)) ? 0 : reward->questInfo->XPValue( plr ));
    data << uint32(reward->questInfo->GetRewMoneyMaxLevel());                                      // some "variable" money?
    data << uint32((plr->getLevel() == sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)) ? 0 : reward->questInfo->XPValue( plr ));// some "variable" xp?
    
    ItemPrototype const *rewItem = sObjectMgr.GetItemPrototype(reward->questInfo->RewItemId[0]);   // Only first item is for dungeon finder
    if(!rewItem)
        data << uint8(0);   // have not reward item
    else
    {
        data << uint8(1);   // have reward item
        data << uint32(rewItem->ItemId);
        data << uint32(rewItem->DisplayInfoID);
        data << uint32(reward->questInfo->RewItemCount[0]);
    }
}

LfgReward* LfgMgr::GetDungeonReward(uint32 dungeon, bool firstToday, uint8 level)
{
    LFGDungeonEntry const *dungeonInfo = sLFGDungeonStore.LookupEntry(dungeon);
    if(!dungeonInfo)
        return NULL;

    for(LfgRewardList::iterator itr = m_rewardsList.begin(); itr != m_rewardsList.end(); ++itr)
    {
        if((*itr)->type == dungeonInfo->type && (*itr)->GroupType == dungeonInfo->grouptype &&
            (*itr)->isDaily() == firstToday)
        {
            Quest *rewQuest = (*itr)->questInfo;
            if(level >= (*itr)->questInfo->GetMinLevel() && (*itr)->questInfo->GetQuestLevel() <= level)  // ...mostly, needs some adjusting in db, blizz q level are without order
                return *itr;
        }
    }
    return NULL;
}

LfgDungeonList* LfgMgr::GetRandomDungeons(Player *plr)
{
    LfgDungeonList *dungeons = new LfgDungeonList();
    LFGDungeonEntry const *currentRow;
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        currentRow = sLFGDungeonStore.LookupEntry(i);
        if(currentRow && currentRow->type == LFG_TYPE_RANDOM &&
            currentRow->minlevel <= plr->getLevel() && currentRow->maxlevel >= plr->getLevel() &&
            currentRow->expansion <= plr->GetSession()->Expansion())
            dungeons->push_back(const_cast<LFGDungeonEntry*>(currentRow));
    }
    return dungeons;
}

LfgLocksList* LfgMgr::GetDungeonsLock(Player *plr)
{
    LfgLocksList* locks = new LfgLocksList();
    LFGDungeonEntry const *currentRow;
    LfgLockStatusType type;
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        currentRow = sLFGDungeonStore.LookupEntry(i);
        if(!currentRow)
            continue;
        type = LFG_LOCKSTATUS_OK;
        if(currentRow->expansion > plr->GetSession()->Expansion())
            type = LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        else if(currentRow->minlevel > plr->getLevel())
            type = LFG_LOCKSTATUS_TOO_LOW_LEVEL;
        else if(plr->getLevel() > currentRow->maxlevel)
            type = LFG_LOCKSTATUS_TOO_HIGH_LEVEL;
        //others to be done, I just need to test if LFG window has correct function right now...

        if(type != LFG_LOCKSTATUS_OK)
        {
            LfgLockStatus *lockStatus = new LfgLockStatus();
            lockStatus->dungeonInfo = const_cast<LFGDungeonEntry*>(currentRow);
            lockStatus->lockType = type;
            locks->push_back(lockStatus);
        } 
    }
    return locks;
}

/*
CREATE TABLE `quest_lfg_relation` (
`type` TINYINT( 3 ) UNSIGNED NOT NULL DEFAULT '0',
`groupType` TINYINT( 3 ) UNSIGNED NOT NULL DEFAULT '0',
`questEntry` INT( 11 ) UNSIGNED NOT NULL DEFAULT '0',
`flags` INT( 11 ) UNSIGNED NOT NULL DEFAULT '0',
INDEX ( `type` , `groupType` ) ,
UNIQUE (`questEntry`)
) ENGINE = InnoDB;
*/

void LfgMgr::LoadDungeonRewards()
{
    // In case of reload
    m_rewardsList.clear();

    uint32 count = 0;
    //                                                0     1          2           3       
    QueryResult *result = WorldDatabase.Query("SELECT type, groupType, questEntry, flags FROM quest_lfg_relation");

    if( !result )
    {
        barGoLink bar( 1 );

        bar.step();

        sLog.outString();
        sLog.outString( ">> Loaded %u LFG dungeon quest relations", count );
        return;
    }

    barGoLink bar( (int)result->GetRowCount() );

    do
    {
        Field *fields = result->Fetch();

        bar.step();
        
        LfgReward *reward = new LfgReward();
        reward->type                  = fields[0].GetUInt8();
        reward->GroupType             = fields[1].GetUInt8();
        reward->flags                 = fields[3].GetUInt32();

        if(Quest *rewardQuest = const_cast<Quest*>(sObjectMgr.GetQuestTemplate(fields[2].GetUInt32())))
            reward->questInfo = rewardQuest;
        else
        {
            sLog.outErrorDb("Entry listed in 'quest_lfg_relation' has non-exist quest %u, skipping.", fields[2].GetUInt32());
            delete reward;
            continue;
        }
        m_rewardsList.push_back(reward);
        ++count;
    } while( result->NextRow() );

    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u LFG dungeon quest relations.", count );
}