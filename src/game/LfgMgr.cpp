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
    premadePlayers.clear();
    m_answers.clear();
    m_tank = 0;
    m_heal = 0;
    m_killedBosses = 0;
    m_readycheckTimer = 0;
    m_baseLevel = 0;
}
LfgGroup::~LfgGroup()
{
    delete dps;
}

bool LfgGroup::AddMember(const uint64 &guid, const char* name)
{
    Player *player = sObjectMgr.GetPlayer(guid);
    if(GetMembersCount() == 0)
        m_baseLevel = player->getLevel();
    MemberSlot member;
    member.guid      = guid;
    member.name      = name;
    member.group     = 1;
    member.assistant = false;
    m_memberSlots.push_back(member);

    player->m_lookingForGroup.groups.insert(std::pair<uint32, LfgGroup*>(m_dungeonInfo->ID,this));
    //Add reference...this is horrible
    if(player->m_lookingForGroup.m_LfgGroup.find(m_dungeonInfo->ID) != player->m_lookingForGroup.m_LfgGroup.end())
    {
        std::map<uint32, GroupReference*>::iterator itr = player->m_lookingForGroup.m_LfgGroup.find(m_dungeonInfo->ID);
        itr->second->link(this, player);
        itr->second->setSubGroup(0);
    }
    else
    {
        GroupReference *reference = new GroupReference();
        reference->link(this, player);
        reference->setSubGroup(0);
        player->m_lookingForGroup.m_LfgGroup.insert(std::make_pair<uint32, GroupReference*>(m_dungeonInfo->ID, reference));
    }
}

uint32 LfgGroup::RemoveMember(const uint64 &guid, const uint8 &method)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot != m_memberSlots.end())
    {
        SubGroupCounterDecrease(slot->group);

        m_memberSlots.erase(slot);
    }
    if(Player *player = sObjectMgr.GetPlayer(guid))
    {
        player->m_lookingForGroup.groups.erase(m_dungeonInfo->ID);
        player->m_lookingForGroup.m_LfgGroup.find(m_dungeonInfo->ID)->second->unlink();
        delete player->m_lookingForGroup.m_LfgGroup.find(m_dungeonInfo->ID)->second;
        player->m_lookingForGroup.m_LfgGroup.erase(m_dungeonInfo->ID);
    }
    //Remove from any role
    if(m_tank == guid)
        m_tank = 0;
    else if(m_heal == guid)
        m_heal = 0;
    else if(dps->find(guid) != dps->end())
        dps->erase(guid);
}

void LfgGroup::RemoveOfflinePlayers()
{
    //cant do this with reference cuz guid is not in it
    //check tank
    Player *tank = sObjectMgr.GetPlayer(m_tank);
    if(!tank ||!tank->IsInWorld())
        RemoveMember(m_tank, 0);
    //check heal
    Player *heal = sObjectMgr.GetPlayer(m_heal);
    if(!heal || !heal->IsInWorld())
        RemoveMember(m_heal, 0);
    //check damage
    for(PlayerList::iterator itr = dps->begin(); itr != dps->end(); ++itr)
    {
        Player *damage = sObjectMgr.GetPlayer(*itr);
        if(!damage || !damage->IsInWorld())
            RemoveMember(*itr, 0);
    }
    if(GetMembersCount() == 0)
        sLfgMgr.AddGroupToDelete(this);
}
bool LfgGroup::UpdateCheckTimer(uint32 time)
{
    m_readycheckTimer += time;
    if(m_readycheckTimer >= LFG_TIMER_READY_CHECK)
        return false;

    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        if(!itr->getSource())
            return false;

    return true;
}
void LfgGroup::TeleportToDungeon()
{
    //If random, then select here
    if(m_dungeonInfo->type == LFG_TYPE_RANDOM)
    {
        LfgLocksMap *groupLocks = GetLocksList();
        LfgDungeonList options;
        LFGDungeonEntry const *currentRow;
        //Possible dungeons
        for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
        {
            currentRow = sLFGDungeonStore.LookupEntry(i);
            if(!currentRow)
                continue;
            if(currentRow->type != LFG_TYPE_RANDOM && currentRow->grouptype == m_dungeonInfo->grouptype)
                options.insert(currentRow);
        }
        //And now get only without locks
        for(LfgLocksMap::iterator itr = groupLocks->begin(); itr != groupLocks->end(); ++itr)
        {
            for(LfgLocksList::iterator itr2 = itr->second->begin(); itr2 != itr->second->end(); ++itr2)
            {
                //Already out
                if(options.find((*itr2)->dungeonInfo) == options.end())
                    continue;
                //TODO: correct instance save handling
                if((*itr2)->lockType != LFG_LOCKSTATUS_RAID_LOCKED)
                    options.erase((*itr2)->dungeonInfo);
            }
        }
        //This should not happen
        if(options.empty())
        {
            for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
            {
                sLfgMgr.SendLfgUpdatePlayer(itr->getSource(), LFG_UPDATETYPE_GROUP_DISBAND);
                itr->getSource()->GetSession()->SendAreaTriggerMessage("Cant select any aviable random dungeon for your group, try it again, sorry.");
                RemoveMember(itr->getSource()->GetGUID(), 0);
            }
            sLfgMgr.AddGroupToDelete(this);
        }
    }
     error_log("TELEPORT");
    if(InstanceTemplate const *instance = sObjectMgr.GetInstanceTemplate(m_dungeonInfo->map))
    {
        for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {

            itr->getSource()->GetSession()->SendAreaTriggerMessage("TODO: TELEPORT");
        }
    }
    
}
bool LfgGroup::HasCorrectLevel(uint8 level)
{
    //Non random
    if(!m_dungeonInfo->isRandom())
    {
        if(m_dungeonInfo->minlevel > level || level > m_dungeonInfo->maxlevel)
            return false;
        return true;
    }
    //And random
    switch(m_dungeonInfo->grouptype)
    {
        case LFG_GROUPTYPE_CLASSIC: 
        case LFG_GROUPTYPE_BC_NORMAL:
            if(m_baseLevel > level)
                return (m_baseLevel - level < 5);
            else
                return (level - m_baseLevel < 5);
        case LFG_GROUPTYPE_BC_HEROIC:
            if(level < 70 || level > 73)
                return false;
            else
                return true;
        case LFG_GROUPTYPE_WTLK_NORMAL:
            if(level > 68)
                return true;
            else
                return false;
        case LFG_GROUPTYPE_WTLK_HEROIC:
            if(level == 80)
                return true;
            else
                return false;
    }
    return true;
}

LfgLocksMap* LfgGroup::GetLocksList()
{
    LfgLocksMap *groupLocks;
    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *plr = itr->getSource();
        LfgLocksList *playerLocks = sLfgMgr.GetDungeonsLock(plr);
        if(!playerLocks->empty())
            groupLocks->insert(std::pair<uint64, LfgLocksList*>(plr->GetGUID(),playerLocks));
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
    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *plr = itr->getSource();
        if(!plr)
            continue;
        uint8 role = plr->m_lookingForGroup.roles;

        WorldPacket data(SMSG_LFG_QUEUE_STATUS, 31);
        data << uint32(m_dungeonInfo->ID);                              // Dungeon
        data << uint32(sLfgMgr.GetAvgWaitTime(m_dungeonInfo->ID, LFG_WAIT_TIME_AVG, role)); // Average Wait time
        data << uint32(sLfgMgr.GetAvgWaitTime(m_dungeonInfo->ID, LFG_WAIT_TIME, role));     // Wait Time
        data << uint32(sLfgMgr.GetAvgWaitTime(m_dungeonInfo->ID, LFG_WAIT_TIME_TANK, role));// Wait Tanks
        data << uint32(sLfgMgr.GetAvgWaitTime(m_dungeonInfo->ID, LFG_WAIT_TIME_HEAL, role));// Wait Healers
        data << uint32(sLfgMgr.GetAvgWaitTime(m_dungeonInfo->ID, LFG_WAIT_TIME_DPS, role)); // Wait Dps
        data << uint8(m_tank ? 0 : 1);                                  // Tanks needed
        data << uint8(m_heal ? 0 : 1);                                  // Healers needed
        data << uint8(LFG_DPS_COUNT - dps->size());                     // Dps needed
        data << uint32(time(NULL) - plr->m_lookingForGroup.joinTime);   // Player wait time in queue
        plr->GetSession()->SendPacket(&data);
    }
}
void LfgGroup::SendGroupFormed()
{
    //Finalize roles
    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        if(!itr->getSource()) // Player gone, will be removed on next update
            continue;
        
        error_log("player %s ma roli %u", itr->getSource()->GetName(), roles);
        itr->getSource()->m_lookingForGroup.roles = roles;
    }
    //SMSG_LFG_UPDATE_PLAYER -> SMSG_LFG_PROPOSAL_UPDATE
    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 10);
    data << uint8(LFG_UPDATETYPE_PROPOSAL_FOUND);
    data << uint8(true); //extra info
    data << uint8(false); //queued
    data << uint8(0); //unk
    data << uint8(0); //unk
    data << uint8(1); //count
    data << uint32(GetDungeonInfo()->Entry());
    data << uint8(0);
    BroadcastPacket(&data, false);

    SendProposalUpdate(LFG_PROPOSAL_WAITING);
}

void LfgGroup::SendProposalUpdate(uint8 state)
{
    for (GroupReference *plritr = GetFirstMember(); plritr != NULL; plritr = plritr->next())
    {
        if(!plritr->getSource() )
            continue;
        //Correct - 3.3.3a
        WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE);
        data << uint32(GetDungeonInfo()->Entry());
        data << uint8(state); 
        data << uint32(GetGroupId()); 
        data << uint32(GetKilledBosses());
        data << uint8(0); //silent
        data << uint8(GetMembersCount());
        for (GroupReference *plritr2 = GetFirstMember(); plritr2 != NULL; plritr2 = plritr2->next())
        {
            if(Player *plr = plritr2->getSource() && plritr2->getSource()->GetMap())
            {
                uint8 roles = 0;
                if(m_tank == plr->GetGUID())
                    roles |= TANK;
                else if(m_heal == plr->GetGUID())
                    roles |= HEALER;
                else if(dps->find(plr->GetGUID()) != dps->end())
                    roles |= DAMAGE;
                data << uint32(roles);
                data << uint8((plr == plritr->getSource()));  // if its you, this is true
                data << uint8(0); // InDungeon
                data << uint8(premadePlayers.find(plr->GetGUID()) != premadePlayers.end()); // Same group
                //If player agrees with dungeon, these two are 1
                if(m_answers.find(plr->GetGUID()) != m_answers.end())
                {
                    data << uint8(1);
                    data << uint8(m_answers.find(plr->GetGUID())->second);
                }
                else
                {
                    data << uint8(0);  //Answer
                    data << uint8(0);  //Accept
                }
            }
        }
        plritr->getSource()->GetSession()->SendPacket(&data);
    }
}

LfgMgr::LfgMgr()
{
    m_updateQueuesTimer = LFG_TIMER_UPDATE_QUEUES;
    m_updateProposalTimer = LFG_TIMER_UPDATE_PROPOSAL;
    m_deleteInvalidTimer = LFG_TIMER_DELETE_INVALID_GROUPS;
    m_groupids = 0;
}

LfgMgr::~LfgMgr()
{

}

void LfgMgr::Update(uint32 diff)
{
    //Update queues
    if(m_updateQueuesTimer <= diff)
        UpdateQueues(); // Timer will reset in UpdateQueues()
    else m_updateQueuesTimer -= diff;

    //Update formed groups
    if(m_updateProposalTimer <= diff)
    {
        UpdateFormedGroups(); 
        m_updateProposalTimer = LFG_TIMER_UPDATE_PROPOSAL;
    }
    else m_updateProposalTimer -= diff;

    //Delete invalid groups
    if(m_deleteInvalidTimer <= diff)
    {
        for(GroupsList::iterator itr = groupsForDelete.begin(); itr != groupsForDelete.end(); ++itr)
        {
            delete *itr;
            groupsForDelete.erase(itr);
        }
        m_deleteInvalidTimer = LFG_TIMER_DELETE_INVALID_GROUPS;
    }else m_deleteInvalidTimer -= diff;

}

void LfgMgr::AddToQueue(Player *player)
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
            
            if(m_queuedDungeons[side].find((*it)->ID) != m_queuedDungeons[side].end())
                m_queuedDungeons[side].find((*it)->ID)->second->players.insert(player->GetGUID());  //Insert player into queue, will be sorted on next queue update
            else  // None player is qeued into this dungeon
            {
                QueuedDungeonInfo *newDungeonQueue = new QueuedDungeonInfo();
                newDungeonQueue->dungeonInfo = *it;
                newDungeonQueue->players.insert(player->GetGUID());
                m_queuedDungeons[side].insert(std::pair<uint32, QueuedDungeonInfo*>((*it)->ID, newDungeonQueue));
                //fill some default data into wait times
                if(m_waitTimes[0].find((*it)->ID) == m_waitTimes[0].end())
                    for(int i = 0; i < LFG_WAIT_TIME_SLOT_MAX; ++i)
                        m_waitTimes[i].insert(std::make_pair<uint32, time_t>((*it)->ID, time_t(0)));
            }
        }
    }
    UpdateQueues();
}

void LfgMgr::RemoveFromQueue(Player *player)
{
     error_log("Remove from queue %s", player->GetName());
    if(Group *group = player->GetGroup())
    {
        //TODO
    }
    else
    {
        SendLfgUpdatePlayer(player, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
        uint8 side = (player->GetTeam() == ALLIANCE) ? LFG_ALLIANCE : LFG_HORDE;
        for (LfgDungeonList::const_iterator it = player->m_lookingForGroup.queuedDungeons.begin(); it != player->m_lookingForGroup.queuedDungeons.end(); ++it)
        {
            QueuedDungeonsMap::iterator itr = m_queuedDungeons[side].find((*it)->ID);
            if(itr == m_queuedDungeons[side].end())                 // THIS SHOULD NEVER HAPPEN
                continue;
            itr->second->players.erase(player->GetGUID());
            if(itr->second->groups.find(player->m_lookingForGroup.groups.find((*it)->ID)->second) != itr->second->groups.end())
            {
                GroupsList::iterator grpitr = itr->second->groups.find(player->m_lookingForGroup.groups.find((*it)->ID)->second);
                if((*grpitr)->IsMember(player->GetGUID()))
                {
                    (*grpitr)->RemoveMember(player->GetGUID(), 0);
                    error_log("REmove member %s", player->GetName());
                }

                if((*grpitr)->GetMembersCount() == 0)
                    itr->second->groups.erase(grpitr);
            }
            if(itr->second->groups.empty() && itr->second->groups.empty())
            {
                delete itr->second;
                m_queuedDungeons[side].erase(itr);
            }
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
            //Remove somehow unaviable players
            for(GroupsList::iterator grpitr1 = itr->second->groups.begin(); grpitr1 != itr->second->groups.end(); ++grpitr1)
                (*grpitr1)->RemoveOfflinePlayers();
            //First, try to merge groups
            for(GroupsList::iterator grpitr1 = itr->second->groups.begin(); grpitr1 != itr->second->groups.end(); ++grpitr1)
            {
                for(GroupsList::iterator grpitr2 = itr->second->groups.begin(); grpitr2 != itr->second->groups.end(); ++grpitr2)
                {
                    if((*grpitr1) == (*grpitr2) || !(*grpitr1) || (*grpitr2))
                        continue;
                    error_log("Try tom merge");
                    //Try to move tank
                    if(!(*grpitr1)->GetTank() && (*grpitr2)->GetTank() && (*grpitr2)->GetPremadePlayers().find((*grpitr2)->GetTank()) == (*grpitr2)->GetPremadePlayers().end())
                    {
                        Player *tank = sObjectMgr.GetPlayer((*grpitr2)->GetTank());
                        if(!(*grpitr1)->HasCorrectLevel(tank->getLevel()))
                            continue;
                        (*grpitr2)->RemoveMember((*grpitr2)->GetTank(), 0);
                        (*grpitr1)->AddMember(tank->GetGUID(), tank->GetName());
                        (*grpitr1)->SetTank(tank->GetGUID());
                    }
                    //Try to move heal
                    if(!(*grpitr1)->GetHeal() && (*grpitr2)->GetHeal() && (*grpitr2)->GetPremadePlayers().find((*grpitr2)->GetHeal()) == (*grpitr2)->GetPremadePlayers().end())
                    {
                        Player *heal = sObjectMgr.GetPlayer((*grpitr2)->GetHeal());
                        if(!(*grpitr1)->HasCorrectLevel(heal->getLevel()))
                            continue;
                        (*grpitr2)->RemoveMember((*grpitr2)->GetHeal(), 0);
                        (*grpitr1)->AddMember(heal->GetGUID(), heal->GetName());
                        (*grpitr1)->SetHeal(heal->GetGUID());
                    }
                    // ..and DPS
                    if((*grpitr1)->GetDps()->size() != LFG_DPS_COUNT && !(*grpitr2)->GetDps()->empty())
                    {
                        //move dps
                        for(PlayerList::iterator dps = (*grpitr2)->GetDps()->begin(); dps != (*grpitr2)->GetDps()->end(); ++dps)
                        {
                            if((*grpitr2)->GetPremadePlayers().find((*dps)) != (*grpitr2)->GetPremadePlayers().end())
                                continue;

                            Player *player = sObjectMgr.GetPlayer(*dps);
                            if(!(*grpitr1)->HasCorrectLevel(player->getLevel()))
                                continue;
                            (*grpitr2)->RemoveMember(*dps, 0);
                            (*grpitr1)->AddMember((*dps), player->GetName());
                            (*grpitr1)->GetDps()->insert(*dps);
                            if((*grpitr1)->GetDps()->size() == LFG_DPS_COUNT)
                                break;
                        }
                        //and delete them from second group
                        for(PlayerList::iterator dps = (*grpitr1)->GetDps()->begin(); dps != (*grpitr2)->GetDps()->end(); ++dps)
                        {
                            if((*grpitr2)->GetDps()->find(*dps) != (*grpitr2)->GetDps()->end())
                                (*grpitr2)->GetDps()->erase(*dps);
                        }
                    }
                    //Delete empty groups
                    if((*grpitr2)->GetMembersCount() == 0)
                    { 
                        delete *grpitr2;
                        itr->second->groups.erase(grpitr2);
                    }
                }
            }

            //Players in queue for that dungeon...
            for(PlayerList::iterator plritr = itr->second->players.begin(); plritr != itr->second->players.end(); ++plritr)
            {
                Player *player = sObjectMgr.GetPlayer(*plritr);
                bool getIntoGroup = false;
                //Try to put him into any incomplete group
                for(GroupsList::iterator grpitr = itr->second->groups.begin(); grpitr != itr->second->groups.end(); ++grpitr)
                {
                    //Check level, this is needed only for Classic and BC normal I think...
                    if(!(*grpitr)->HasCorrectLevel(player->getLevel()))
                        continue;
                    //Group needs tank and player is queued as tank
                    if((*grpitr)->GetTank() == 0 && (player->m_lookingForGroup.roles & TANK))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr), player->GetName());
                        (*grpitr)->SetTank((*plritr));
                         error_log("player %s de do grupy tank", player->GetName());
                    }
                    //Heal...
                    else if((*grpitr)->GetHeal() == 0 && (player->m_lookingForGroup.roles & HEALER))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr), player->GetName());
                        (*grpitr)->SetHeal((*plritr));
                         error_log("player %s de do grupy heal", player->GetName());
                    }
                    //DPS
                    else if((*grpitr)->GetDps()->size() != LFG_DPS_COUNT && (player->m_lookingForGroup.roles & DAMAGE))
                    {
                        getIntoGroup = true;
                        (*grpitr)->AddMember((*plritr), player->GetName());
                        (*grpitr)->GetDps()->insert((*plritr));
                         error_log("player %s de do grupy dps", player->GetName());

                    }
                    if(!getIntoGroup)
                        error_log("player %s nejde do grupy", player->GetName());
                }
                //Failed, so create new LfgGroup
                if(!getIntoGroup)
                {
                    error_log("player %s dela novou grupu", player->GetName());
                    LfgGroup *newGroup = new LfgGroup();
                    newGroup->SetDungeonInfo(itr->second->dungeonInfo);
                    newGroup->AddMember((*plritr), player->GetName());   
                    
                    newGroup->SetGroupId(GenerateLfgGroupId());

                    //Tank is main..
                    if(player->m_lookingForGroup.roles & TANK)
                         newGroup->SetTank((*plritr));
                    //Heal...
                    else if(player->m_lookingForGroup.roles & HEALER)
                        newGroup->SetHeal((*plritr));
                    //DPS
                    else if(player->m_lookingForGroup.roles & DAMAGE)
                        newGroup->GetDps()->insert((*plritr));
                    //Insert into queue
                    itr->second->groups.insert(newGroup);
                }
                //Player is in the group now
                itr->second->players.erase(plritr);
            }
            //Send update to everybody in queue and move complete groups to waiting state
            for(GroupsList::iterator grpitr = itr->second->groups.begin(); grpitr != itr->second->groups.end(); ++grpitr)
            {
                //Send Update
                (*grpitr)->SendLfgQueueStatus();

                //prepare complete groups
                if((*grpitr)->GetMembersCount() == 5)
                {
                    //Update wait times
                    time_t avgWaitTime = 0;
                    if(Player *tank = sObjectMgr.GetPlayer((*grpitr)->GetTank()))
                    {
                        time_t waitTimeTank = m_waitTimes[LFG_WAIT_TIME_TANK].find(itr->second->dungeonInfo->ID)->second;
                        time_t currentTankTime = time_t(NULL) - tank->m_lookingForGroup.joinTime;
                        time_t avgWaitTank = (waitTimeTank+currentTankTime)/2;
                        avgWaitTime += avgWaitTank;
                        m_waitTimes[LFG_WAIT_TIME_TANK].find(itr->second->dungeonInfo->ID)->second = avgWaitTank;           
                    }
                    if(Player *heal = sObjectMgr.GetPlayer((*grpitr)->GetHeal()))
                    {
                        time_t waitTimeHeal = m_waitTimes[LFG_WAIT_TIME_HEAL].find(itr->second->dungeonInfo->ID)->second;
                        time_t currentHealTime = time_t(NULL) - heal->m_lookingForGroup.joinTime;
                        time_t avgTimeHeal = (waitTimeHeal+currentHealTime)/2;
                        avgWaitTime += avgTimeHeal;
                        m_waitTimes[LFG_WAIT_TIME_HEAL].find(itr->second->dungeonInfo->ID)->second = avgTimeHeal;           
                    }
                    for(PlayerList::iterator plritr = (*grpitr)->GetDps()->begin(); plritr != (*grpitr)->GetDps()->end(); ++plritr)
                    {
                        if(Player *dps = sObjectMgr.GetPlayer(*plritr))
                        {
                            time_t waitTimeDps = m_waitTimes[LFG_WAIT_TIME_DPS].find(itr->second->dungeonInfo->ID)->second;
                            time_t currTime = time_t(NULL) - dps->m_lookingForGroup.joinTime;
                            time_t avgWaitDps = (waitTimeDps+currTime)/2;
                            avgWaitTime += avgWaitDps;
                            m_waitTimes[LFG_WAIT_TIME_DPS].find(itr->second->dungeonInfo->ID)->second = avgWaitDps;
                        }
                    }
                    m_waitTimes[LFG_WAIT_TIME].find(itr->second->dungeonInfo->ID)->second = time_t(avgWaitTime/5);
                    
                    //Send Info                   
                    (*grpitr)->SendGroupFormed();
                    
                    formedGroups[i].insert(*grpitr);
                    itr->second->groups.erase(grpitr);

                    //Delete empty dungeon queues
                    if(itr->second->groups.empty() && itr->second->players.empty())
                    {
                        delete itr->second;
                        m_queuedDungeons[i].erase(itr);
                    }
                } 
            }
        }
    }
    m_updateQueuesTimer = LFG_TIMER_UPDATE_QUEUES;
}
void LfgMgr::UpdateFormedGroups()
{
    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        for(GroupsList::iterator grpitr = formedGroups[i].begin(); grpitr != formedGroups[i].end(); ++grpitr)
        {
            //this return false if  time has passed or player offline
            if(!(*grpitr)->UpdateCheckTimer(LFG_TIMER_UPDATE_PROPOSAL))
            {
                error_log("PROPOSAL_FAILED");
                (*grpitr)->SendProposalUpdate(LFG_PROPOSAL_FAILED);
                //Move group to queue
                if(m_queuedDungeons[i].find((*grpitr)->GetDungeonInfo()->ID) != m_queuedDungeons[i].end())
                {
                    QueuedDungeonsMap::iterator itr = m_queuedDungeons[i].find((*grpitr)->GetDungeonInfo()->ID);
                    itr->second->groups.insert((*grpitr));
                }
                else
                {
                    QueuedDungeonInfo *newInfo = new QueuedDungeonInfo();
                    newInfo->dungeonInfo = (*grpitr)->GetDungeonInfo();
                    newInfo->groups.insert(*grpitr);
                    m_queuedDungeons[i].insert(std::pair<uint32, QueuedDungeonInfo*>(newInfo->dungeonInfo->ID, newInfo));
                }
                (*grpitr)->RemoveOfflinePlayers();
                //Send to players..
                for (GroupReference *plritr = (*grpitr)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                    SendLfgUpdatePlayer(plritr->getSource(), LFG_UPDATETYPE_PROPOSAL_FAILED);

                formedGroups[i].erase(grpitr);
                continue;
            }
            //all player responded
            if((*grpitr)->GetProposalAnswers()->size() == 5)
            {
                error_log("Vsici odpovedeli");
                uint32 type = LFG_PROPOSAL_SUCCESS;
                for(ProposalAnswersMap::iterator itr = (*grpitr)->GetProposalAnswers()->begin(); itr != (*grpitr)->GetProposalAnswers()->end(); ++itr)
                {
                    if(itr->second != 1)
                        type = LFG_PROPOSAL_FAILED;
                }
                (*grpitr)->SendProposalUpdate(type);
                //Failed, remove players which did not agree and move rest to queue
                if(type == LFG_PROPOSAL_FAILED)
                {

                    error_log("failed");
                    for (GroupReference *plritr = (*grpitr)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                    {
                        SendLfgUpdatePlayer(plritr->getSource(), LFG_UPDATETYPE_PROPOSAL_FAILED);
                        (*grpitr)->RemoveMember(plritr->first, 0);
                        AddToQueue(plritr->getSource());
                    }
                    delete *grpitr;
                    formedGroups[i].erase(grpitr);

                    // move group back to queue
                  /*  if(m_queuedDungeons[i].find((*grpitr)->GetDungeonInfo()->ID) != m_queuedDungeons[i].end())
                    {
                        QueuedDungeonsMap::iterator itr = m_queuedDungeons[i].find((*grpitr)->GetDungeonInfo()->ID);
                        itr->second->groups.insert((*grpitr));
                    }
                    else
                    {
                        QueuedDungeonInfo *newInfo = new QueuedDungeonInfo();
                        newInfo->dungeonInfo = (*grpitr)->GetDungeonInfo();
                        newInfo->groups.insert(*grpitr);
                        m_queuedDungeons[i].insert(std::pair<uint32, QueuedDungeonInfo*>(newInfo->dungeonInfo->ID, newInfo));
                        
                    } */
                    
                    error_log("Moved");
                }
                //We are good to go, sir
                else
                {
                    for (GroupReference *plritr = (*grpitr)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                    {
                        SendLfgUpdatePlayer(plritr->getSource(), LFG_UPDATETYPE_GROUP_FOUND);
                        
                        //I think this is useless when you are not in group, but its sent by retail serverse anyway...
                        SendLfgUpdateParty(plritr->getSource(), LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                        (*grpitr)->TeleportToDungeon();
                        inDungeonGroups[i].insert(*grpitr);
                        formedGroups[i].erase(*grpitr);
                    }
                }
            }
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
    delete locks;
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

    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 2 + (extrainfo ? 1 : 0) * (4 + plr->m_lookingForGroup.queuedDungeons.size() * 4 + plr->m_lookingForGroup.comment.length()));
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
        data << plr->m_lookingForGroup.comment;
    }
    plr->GetSession()->SendPacket(&data);
}

void LfgMgr::SendLfgUpdateParty(Player *plr, uint8 updateType)
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

    WorldPacket data(SMSG_LFG_UPDATE_PARTY, 2 + (extrainfo ? 1 : 0) * (8 + plr->m_lookingForGroup.queuedDungeons.size() * 4 + plr->m_lookingForGroup.comment.length()));
    data << uint8(updateType);                              // Lfg Update type
    data << uint8(extrainfo);                               // Extra info
    if (extrainfo)
    {
        data << uint8(join);                                // LFG Join
        data << uint8(queued);                              // Join the queue
        data << uint8(0);                                   // unk - Always 0
        data << uint8(0);                                   // unk - Always 0
        for (uint8 i = 0; i < 3; ++i)
            data << uint8(0);                               // unk - Always 0

        data << uint8(plr->m_lookingForGroup.queuedDungeons.size());

        for (LfgDungeonList::const_iterator it = plr->m_lookingForGroup.queuedDungeons.begin(); it != plr->m_lookingForGroup.queuedDungeons.end(); ++it)
            data << uint32((*it)->Entry());
        data << plr->m_lookingForGroup.comment;
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

        uint32 minlevel, maxlevel;
        //Take level from db where possible
        if(InstanceTemplate const *instance = sObjectMgr.GetInstanceTemplate(currentRow->map))
        {
            minlevel = instance->levelMin;
            maxlevel = instance->levelMax;
        }
        else
        {
            minlevel = currentRow->minlevel;
            maxlevel = currentRow->maxlevel;
        }
        type = LFG_LOCKSTATUS_OK;

        InstancePlayerBind *playerBind = plr->GetBoundInstance(currentRow->map, Difficulty(currentRow->heroic));

        if(currentRow->expansion > plr->GetSession()->Expansion())
            type = LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        else if(minlevel > plr->getLevel())
            type = LFG_LOCKSTATUS_TOO_LOW_LEVEL;
        else if(plr->getLevel() > maxlevel)
            type = LFG_LOCKSTATUS_TOO_HIGH_LEVEL;
        else if(playerBind && playerBind->perm)
            type = LFG_LOCKSTATUS_RAID_LOCKED;
        //others to be done

        if(type != LFG_LOCKSTATUS_OK)
        {
            LfgLockStatus *lockStatus = new LfgLockStatus();
            lockStatus->dungeonInfo = currentRow;
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
LfgGroup* LfgMgr::GetLfgGroupById(uint32 groupid)
{
    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        for(QueuedDungeonsMap::iterator itr = m_queuedDungeons[i].begin(); itr != m_queuedDungeons[i].end(); ++itr)
        {
            //queued groups
            for(GroupsList::iterator grpitr = itr->second->groups.begin(); grpitr != itr->second->groups.end(); ++grpitr)
                if((*grpitr)->GetGroupId() == groupid)
                    return *grpitr;
        }
        //formed groups
        for(GroupsList::iterator grpitr = formedGroups[i].begin(); grpitr != formedGroups[i].end(); ++grpitr)
            if((*grpitr)->GetGroupId() == groupid)
                return *grpitr;
        //InDungeon groups
        for(GroupsList::iterator grpitr = inDungeonGroups[i].begin(); grpitr != inDungeonGroups[i].end(); ++grpitr)
            if((*grpitr)->GetGroupId() == groupid)
                return *grpitr;
    }
    return NULL;
}

time_t LfgMgr::GetAvgWaitTime(uint32 dugeonId, uint8 slot, uint8 roles)
{
    switch(slot)
    {
        case LFG_WAIT_TIME:
        case LFG_WAIT_TIME_TANK:
        case LFG_WAIT_TIME_HEAL:
        case LFG_WAIT_TIME_DPS:
            return m_waitTimes[slot].find(dugeonId)->second;  // No check required, if this method is called, some data is already in array
        case LFG_WAIT_TIME_AVG:
            if(roles & TANK)
            {
                if(!(roles & HEALER) && !(roles & DAMAGE))
                    return m_waitTimes[LFG_WAIT_TIME_TANK].find(dugeonId)->second;
            }
            else if(roles & HEALER)
            {
                if(!(roles & DAMAGE))
                    return m_waitTimes[LFG_WAIT_TIME_HEAL].find(dugeonId)->second;
            }
            else if(roles & DAMAGE)
                return m_waitTimes[LFG_WAIT_TIME_DPS].find(dugeonId)->second;
            return m_waitTimes[LFG_WAIT_TIME].find(dugeonId)->second;
    }
}
void LfgMgr::AddGroupToDelete(LfgGroup *group)
{
    //Remove from any other list
    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        for(QueuedDungeonsMap::iterator itr = m_queuedDungeons[i].begin(); itr != m_queuedDungeons[i].end(); ++itr)
            itr->second->groups.erase(group);

        formedGroups[i].erase(group);
        inDungeonGroups[i].erase(group);
    }
    error_log("Add TO erase");
    //Add to erase list
    groupsForDelete.insert(group);
}

void LfgMgr::RemovePlayer(Player *player)
{
    if(!player->m_lookingForGroup.queuedDungeons.empty())
        RemoveFromQueue(player);

    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        for(GroupsList::iterator itr = formedGroups[i].begin(); itr != formedGroups[i].end(); ++itr)
        {
            if((*itr)->IsMember(player->GetGUID()))
                (*itr)->RemoveMember(player->GetGUID(), 0);

            if((*itr)->GetMembersCount() == 0)
                AddGroupToDelete(*itr);
        }
        for(GroupsList::iterator itr = inDungeonGroups[i].begin(); itr != inDungeonGroups[i].end(); ++itr)
        {
            if((*itr)->IsMember(player->GetGUID()))
                (*itr)->RemoveMember(player->GetGUID(), 0);

            if((*itr)->GetMembersCount() == 0)
                AddGroupToDelete(*itr);
        }
    }
}