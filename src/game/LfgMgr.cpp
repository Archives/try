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


LfgGroup::LfgGroup() : Group()
{
    dps = new PlayerList();
    premadePlayers = new std::set<uint64>;

}
LfgGroup::~LfgGroup()
{
    delete dps;
    delete premadePlayers;
}

bool LfgGroup::AddMember(const uint64 &guid, const char* name)
{
    Player *player = sObjectMgr.GetPlayer(guid);

    MemberSlot member;
    member.guid      = guid;
    member.name      = name;
    member.group     = 1;
    member.assistant = false;
    m_memberSlots.push_back(member);

    player->m_lookingForGroup.group = this;
}

uint32 RemoveMember(const uint64 &guid, const uint8 &method)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot != m_memberSlots.end())
    {
        SubGroupCounterDecrease(slot->group);

        m_memberSlots.erase(slot);
    }
    if(Player *player = sObjectMgr.GetPlayer(guid))
        player->m_lookingForGroup.group = NULL;
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

LfgLocksMap* LfgGroup::GetLocksList()
{
    LfgLocksMap *groupLocks;
    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *plr = itr->getSource();
        LfgLocksList *playerLocks = sLfgMgr.GetDungeonsLock(plr);
        if(!playerLocks->empty())
            groupLocks[plr->GetGUID()] = playerLocks;
    }
    return groupLocks;
}
void LfgGroup::SendLfgPartyInfo(Player *plr)
{   
    LfgLocksMap *groupLocks = GetLocksList();

    uint32 size = 0;
    for (LfgLocksMap::const_iterator itr = groupLocks->begin(); itr != groupLocks->end(); ++itr)
        size += 8 + 4 + itr->second->size() * (4 + 4);

    WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
    data << uint8(groupLocks->size());                   // number of locks...
    for (LfgLocksMap::const_iterator itr = groupLocks->begin(); itr != groupLocks->end(); ++itr)
    {
        data << uint64(itr->first);                      // guid of player which has lock
        data << uint32(itr->second->size());             // Size of lock dungeons for that player
        for (LfgLocksList::iterator it = itr->second->begin(); it != itr->second->end(); ++it)
        {
            data << uint32((*it)->dungeonInfo->Entry()); // Dungeon entry + type
            data << uint32((*it)->lockType);             // Lock status
        }
    }
    plr->GetSession()->SendPacket(&data);
}

void LfgGroup::SendLfgQueueStatus()
{
    WorldPacket data(SMSG_LFG_QUEUE_STATUS);

    data << uint32(m_dungeonInfo->ID);                      // Dungeon
    data << uint32(0);                                      // Average Wait time
    data << uint32(0);                                      // Wait Time
    data << uint32(0);                                      // Wait Tanks
    data << uint32(0);                                      // Wait Healers
    data << uint32(0);                                      // Wait Dps
    data << uint8(m_tank ? 0 : 1);                          // Tanks needed
    data << uint8(m_heal ? 0 : 1);                          // Healers needed
    data << uint8(LFG_DPS_COUNT - dps->size());             // Dps needed
    data << uint32(0);                                      // Player wait time in queue

    BroadcastPacket(&data, false);    
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

void LfgMgr::AddToQueue(Player *player, uint32 LfgGroupType)
{
    //Already checked that group is fine
    if(Group *group = player->GetGroup())
    {
        //TODO
    }
    else
    {
        WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
        data << uint32(LFG_JOIN_OK);                                  
        data << uint32(0);
        player->GetSession()->SendPacket(&data);

        SendLfgUpdatePlayer(player, LFG_UPDATETYPE_JOIN_PROPOSAL);
        for (LfgDungeonList::const_iterator it = player->m_lookingForGroup.queuedDungeons.begin(); it != player->m_lookingForGroup.queuedDungeons.end(); ++it)
        {
            uint8 side = (player->GetTeam() == ALLIANCE) ? LFG_ALLIANCE : LFG_HORDE;
            
            if(QueuedDungeonsMap::iterator itr = m_queuedDungeons[side].find((*it)->ID) != m_queuedDungeons[side].end())
                itr->second->PlayerList.insert(player);  //Insert player into queue, will be sorted on next queue update
            else  // None player is qeued into this dungeon
            {
                QueuedDungeonInfo *newDungeonQueue = new QueuedDungeonInfo();
                newDungeonQueue->dungeonInfo = *it;
                newDungeonQueue->GroupsList = new GroupsList();
                newDungeonQueue->PlayerList = new PlayerList();
                newDungeonQueue->PlayerList.insert(player);
                m_queuedDungeons[side][(*it)->ID] = newDungeonQueue;
            }
        }
    }
    UpdateQueues();
}

void LfgMgr::RemoveFromQueue(Player *player)
{
    if(Group *group = player->GetGroup())
    {
        //TODO
    }
    else
    {
        SendLfgUpdatePlayer(LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
        player->m_lookingForGroup.roles = 0;
        uint8 side = (player->GetTeam() == ALLIANCE) ? LFG_ALLIANCE : LFG_HORDE;
        for (LfgDungeonList::const_iterator it = player->m_lookingForGroup.queuedDungeons.begin(); it != player->m_lookingForGroup.queuedDungeons.end(); ++it)
        {
            QueuedDungeonsMap::iterator itr = m_queuedDungeons[side].find((*it)->ID);
            if(itr == m_queuedDungeons[side].end())                 // THIS SHOULD NEVER HAPPEN
                continue;
            itr->second->PlayerList.erase(player);
            if(GroupsList::iterator grpitr = itr->second->GroupsList.find(player->m_lookingForGroup.group) != itr->second->GroupsList.end())
            {
                if((*grpitr)->IsMember(player->GetGUID()))
                    (*grpitr)->RemoveMember(player->GetGUID(), 0);

                if((*grpitr)->GetMembersCount() == 0)
                    itr->second->GroupsList.erase(grpitr);
            }
            if(itr->second->GroupsList.empty() && itr->second->PlayerList.empty())
                m_queuedDungeons[side].erase(itr);
        }
        player->m_lookingForGroup.queuedDungeons.clear();
    }
    UpdateQueues();
}

void LfgMgr::UpdateQueues()
{
    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        if(m_queuedDungeons[i].empty())
            continue;

        //dungeons...
        for(QueuedDungeonsMap::iterator itr = m_queuedDungeons[i].begin(); itr != m_queuedDungeons[i].end(); ++itr)
        {
            //First, try to merge groups
            for(GroupsList::iterator grpitr1 = itr->second->GroupsList.begin(); grpitr1 != itr->second->GroupsList.end(); ++grpitr1)
            {
                for(GroupsList::iterator grpitr2 = itr->second->GroupsList.begin(); grpitr2 != itr->second->GroupsList.end(); ++grpitr2)
                {
                    if((*grpitr1) == (*grpitr2) || !(*grpitr1) || (*grpitr2))
                        continue;
                    //Try to move tank
                    if(!(*grpitr1)->GetTank() && (*grpitr2)->GetTank() && (*grpitr2)->GetPremadePlayers().find((*grpitr2)->GetTank()->GetGUID()) == (*grpitr2)->GetPremadePlayers().end())
                    {
                        (*grpitr2)->RemoveMember((*grpitr2)->GetTank()->GetGUID(), 0);
                        (*grpitr1)->AddMember((*grpitr2)->GetTank()->GetGUID(), (*grpitr2)->GetTank()->GetName());
                        (*grpitr1)->SetTank((*grpitr2)->GetTank());
                        (*grpitr2)->SetTank(NULL);
                    }
                    //Try to move heal
                    if(!(*grpitr1)->GetHeal() && (*grpitr2)->GetHeal() && (*grpitr2)->GetPremadePlayers().find((*grpitr2)->GetHeal()->GetGUID()) == (*grpitr2)->GetPremadePlayers().end())
                    {
                        (*grpitr2)->RemoveMember((*grpitr2)->GetHeal()->GetGUID(), 0);
                        (*grpitr1)->AddMember((*grpitr2)->GetHeal()->GetGUID(), (*grpitr2)->GetHeal()->GetName());
                        (*grpitr1)->SetHeal((*grpitr2)->GetHeal());
                        (*grpitr2)->SetHeal(NULL);
                    }
                    // ..and DPS
                    if((*grpitr1)->GetDps()->size() != LFG_DPS_COUNT && !(*grpitr2)->GetDps()->empty())
                    {
                        //move dps
                        for(PlayerList::iterator dps = (*grpitr2)->GetDps()->begin(); dps != (*grpitr2)->GetDps()->end(); ++dps)
                        {
                            if((*grpitr2)->GetPremadePlayers().find((*dps)->GetGUID()) != (*grpitr2)->GetPremadePlayers().end())
                                continue;

                            (*grpitr2)->RemoveMember((*dps)->GetGUID(), 0);
                            (*grpitr1)->AddMember((*dps)->GetGUID(), (*dps)->GetName());
                            (*grpitr1)->GetDps()->insert(*dps);
                            if((*grpitr1)->GetDps()->size() == LFG_DPS_COUNT)
                                break;
                        }
                        //and delete them from second group
                        for(PlayerList::iterator dps = (*grpitr1)->GetDps()->begin(); dps != (*grpitr2)->GetDps()->end(); ++dps)
                        {
                            if(PlayerList::iterator deleteDps = (*grpitr2)->GetDps()->find(*dps) != (*grpitr2)->GetDps()->end())
                                (*grpitr2)->GetDps()->erase(deleteDps);
                        }
                    }
                    //Delete empty groups
                    if((*grpitr2)->GetMembersCount() == 0)
                    { 
                        delete *grpitr2;
                        itr->second->GroupsList.erase(grpitr2);
                    }
                }
            }

            //Players in queue for that dungeon...
            for(PlayerList::iterator plritr = itr->second->PlayerList.begin(); plritr != itr->second->PlayerList.end(); ++plritr)
            {
                bool getIntoGroup = false;
                //Try to put him into any incomplete group
                for(GroupsList::iterator grpitr = itr->second->GroupsList.begin(); grpitr != itr->second->GroupsList.end(); ++grpitr)
                {
                    //Group needs tank and player is queued as tank
                    if(!(*grpitr)->GetTank() && ((*plritr)->m_lookingForGroup.roles & TANK))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr)->GetGUID(), (*plritr)->GetName());
                        (*grpitr)->SetTank((*plritr));
                    }
                    //Heal...
                    else if(!(*grpitr)->GetHeal() && ((*plritr)->m_lookingForGroup.roles & HEAL))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr)->GetGUID(), (*plritr)->GetName());
                        (*grpitr)->SetHeal((*plritr));
                    }
                    //DPS
                    else if(!(*grpitr)->GetDps()->size() != LFG_DPS_COUNT && ((*plritr)->m_lookingForGroup.roles & DAMAGE))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr)->GetGUID(), (*plritr)->GetName());
                        (*grpitr)->GetDps()->insert((*plritr));

                    }
                }
                //Failed, so create new LfgGroup
                if(!getIntoGroup)
                {
                    LfgGroup *newGroup = new LfgGroup();
                    newGroup->AddMember((*plritr)->GetGUID(), (*plritr)->GetName());
                    newGroup->_setLeader((*plritr)->GetGUID());
                    newGroup->SetDungeonInfo(*itr);

                    //Tank is main..
                    if((*plritr)->m_lookingForGroup.roles & TANK)
                         newGroup->SetTank((*plritr));
                    //Heal...
                    else if((*plritr)->m_lookingForGroup.roles & HEAL)
                        newGroup->SetHeal((*plritr));
                    //DPS
                    else if(*plritr)->m_lookingForGroup.roles & DAMAGE)
                        newGroup->GetDps()->insert((*plritr));
                    //Insert into queue
                    itr->second->GroupsList.insert(newGroup);
                }
                //Player is in the group now
                itr->second->PlayerList.erase(plritr);
            }
            //Send update to everybody in queue
            for(GroupsList::iterator grpitr = itr->second->GroupsList.begin(); grpitr != itr->second->GroupsList.end(); ++grpitr)
               (*grpitr)->SendLfgQueueStatus();
        }
    }
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

void LfgMgr::SendLfgUpdatePlayer(Player *plr, uint8 updateType)
{
    bool queued = false;
    bool extrainfo = false;

    switch(updateType)
    {
        case LFG_UPDATETYPE_JOIN_PROPOSAL:
        case LFG_UPDATETYPE_ADDED_TO_QUEUE:
            queued = true;
            extrainfo = true;
            break;
        //case LFG_UPDATETYPE_CLEAR_LOCK_LIST: // TODO: Sometimes has extrainfo - Check ocurrences...
        case LFG_UPDATETYPE_PROPOSAL_FOUND:
            extrainfo = true;
            break;
    }

    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 2 + (extrainfo ? 1 : 0) * (4 + GetPlayer()->m_lookingForGroup.applyDungeons.size() * 4 + GetPlayer()->m_lookingForGroup.comment.length()));
    data << uint8(updateType);                              // Lfg Update type
    data << uint8(extrainfo);                               // Extra info
    if (extrainfo)
    {
        data << uint8(queued);                              // Join the queue
        data << uint8(0);                                   // unk - Always 0
        data << uint8(0);                                   // unk - Always 0

        data << uint8(plr->m_lookingForGroup.queuedDungeons.size());

        for (LfgDungeonList::const_iterator it = plr->m_lookingForGroup.queuedDungeons.begin(); it != plr->m_lookingForGroup.queuedDungeons.end(); ++it)
            data << uint32((*it)->Entry());
        data << GetPlayer()->m_lookingForGroup.comment;
    }
    plr->GetSession()->SendPacket(&data);
}

void LfgMgr::BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr)
{
    LfgReward *reward = GetDungeonReward(dungeon, plr->m_lookingForGroup.DoneDungeon(dungeon), plr->getLevel());

    if (!reward)
        return;

    data << uint8(plr->m_lookingForGroup.DoneDungeon(dungeon));  // false = its first run this day, true = it isnt
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

LfgReward* LfgMgr::GetDungeonReward(uint32 dungeon, bool done, uint8 level)
{
    LFGDungeonEntry const *dungeonInfo = sLFGDungeonStore.LookupEntry(dungeon);
    if(!dungeonInfo)
        return NULL;

    for(LfgRewardList::iterator itr = m_rewardsList.begin(); itr != m_rewardsList.end(); ++itr)
    {
        if((*itr)->type == dungeonInfo->type && (*itr)->GroupType == dungeonInfo->grouptype &&
            (*itr)->isDaily() != done)
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
            dungeons->insert(currentRow);
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
            lockStatus->dungeonInfo = currentRow;
            lockStatus->lockType = type;
            locks->insert(lockStatus);
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