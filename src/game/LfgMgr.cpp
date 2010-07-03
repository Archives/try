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
#include "Unit.h"
#include "SpellAuras.h"

#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1( LfgMgr );

LfgGroup::LfgGroup() : Group()
{
    dps = new PlayerList();
    premadePlayers.clear();
    m_answers.clear();
    m_rolesProposal.clear();
    m_tank = 0;
    m_heal = 0;
    m_killedBosses = 0;
    m_readycheckTimer = 0;
    m_baseLevel = 0;
    m_groupType = GROUPTYPE_LFD;
    m_instanceStatus = INSTANCE_NOT_SAVED;
    m_inDungeon = false;
    m_isRandom = false;
    m_dungeonInfo = NULL;
    m_membersBeforeRoleCheck = 0;
    randomDungeonEntry = 0;
}
LfgGroup::~LfgGroup()
{
    sObjectMgr.RemoveGroup(this);
    delete dps;
}
bool LfgGroup::LoadGroupFromDB(Field *fields)
{
    if(!Group::LoadGroupFromDB(fields))
        return false;
    error_log("Load group from db");
    m_tank = m_mainTank;
    m_heal = fields[1].GetUInt64();
    m_dungeonInfo = sLFGDungeonStore.LookupEntry(fields[19].GetUInt32());
    m_instanceStatus = fields[20].GetUInt8();
    return true;
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
    CharacterDatabase.PExecute("DELETE FROM group_member WHERE memberGuid='%u'", GUID_LOPART(guid));
}

bool LfgGroup::RemoveOfflinePlayers()  // Return true if group is empty after check
{
    if(m_memberSlots.empty())
    {
        sLfgMgr.AddGroupToDelete(this);
        return true;
    }
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr ||!plr->GetSession())
            RemoveMember(citr->guid, 0);
    }
    //flush empty group
    if(GetMembersCount() == 0)
    {
        sLfgMgr.AddGroupToDelete(this);
        return true;
    }
    return false;
}

void LfgGroup::KilledCreature(char *name)
{
    error_log("Killed creature %s", name);
    if(m_dungeonBosses.find(name) == m_dungeonBosses.end())
        return;
    DungeonEncounterMap::iterator itr = m_dungeonBosses.find(name);
    m_killedBosses++;
    //Last boss
    if(sLfgMgr.GetDungeonInfo(m_dungeonInfo->ID)->lastBossId == itr->second->ID)
    {
        m_instanceStatus = INSTANCE_COMPLETED;
        //Reward here
        for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player *plr = itr->getSource();
            if(!plr)
                continue;
            WorldPacket data(SMSG_LFG_PLAYER_REWARD);
            data << uint32(randomDungeonEntry == 0 ? m_dungeonInfo->Entry() : randomDungeonEntry);
            data << uint32(m_dungeonInfo->Entry());
            sLfgMgr.BuildRewardBlock(data, (randomDungeonEntry & 0x00FFFFFF), plr);
            plr->GetSession()->SendPacket(&data);
            LfgReward *reward = sLfgMgr.GetDungeonReward((randomDungeonEntry & 0x00FFFFFF), plr->m_lookingForGroup.DoneDungeon((randomDungeonEntry & 0x00FFFFFF)), plr->getLevel());
            if (!reward)
                return;
            reward->questInfo->SetFlag(QUEST_FLAGS_AUTO_REWARDED);
            plr->CompleteQuest(reward->questInfo->GetQuestId());
        }
    }
    else
        m_instanceStatus = INSTANCE_SAVED;

    SendUpdate();
}

bool LfgGroup::UpdateCheckTimer(uint32 time)
{
    m_readycheckTimer += time;
    if(m_readycheckTimer >= LFG_TIMER_READY_CHECK || GetMembersCount() != 5)
        return false;
    return true;
}
void LfgGroup::TeleportToDungeon()
{
    //If random, then select here
    uint32 originalDungeonId = m_dungeonInfo->ID;
    if(m_dungeonInfo->type == LFG_TYPE_RANDOM)
    {
        randomDungeonEntry = m_dungeonInfo->Entry();
        m_isRandom = true;
        LfgLocksMap *groupLocks = GetLocksList();
        std::vector<LFGDungeonEntry const*> options;
        LFGDungeonEntry const *currentRow;
        //Possible dungeons
        for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
        {
            currentRow = sLFGDungeonStore.LookupEntry(i);
            if(!currentRow)
                continue;
            if(currentRow->type != LFG_TYPE_RANDOM && currentRow->grouptype == m_dungeonInfo->grouptype)
            {
                error_log("Adding random id %u", currentRow->ID);
                options.push_back(currentRow);
            }
        }
        //And now get only without locks
        for(LfgLocksMap::iterator itr = groupLocks->begin(); itr != groupLocks->end(); ++itr)
        {
            for(LfgLocksList::iterator itr2 = itr->second->begin(); itr2 != itr->second->end(); ++itr2)
            {
                for(std::vector<LFGDungeonEntry const*>::iterator itrDung = options.begin(); itrDung != options.end(); ++itrDung)
                {
                    if((*itrDung)->ID != (*itr2)->dungeonInfo->ID)
                        continue;
                    DugeonInfo* dungeonInfo = sLfgMgr.GetDungeonInfo((*itr2)->dungeonInfo->ID);
                    if((*itr2)->lockType != LFG_LOCKSTATUS_RAID_LOCKED && !dungeonInfo->locked)
                    {
                        options.erase(itrDung);
                        break;
                    }                   
                }
            }
        }
        //This should not happen
        if(options.empty())
        {
            for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
            {
                sLfgMgr.SendLfgUpdatePlayer(itr->getSource(), LFG_UPDATETYPE_GROUP_DISBAND);
                itr->getSource()->GetSession()->SendNotification("Cannot find any random dungeons for this group, you have to find new group. We are sorry");
                RemoveMember(itr->getSource()->GetGUID(), 0);
            }
            sLfgMgr.AddGroupToDelete(this);
            return;
        }
        //Select dungeon, there should be also bind check
        uint32 tmp = urand(0, options.size()-1);
        m_dungeonInfo = options[tmp];
    }
    //Fill dungeon bosses info
    m_dungeonBosses.clear();
    DungeonEncounterEntry const* currentRow;
    for (uint32 i = 0; i < sDungeonEncounterStore.GetNumRows(); ++i)
    {
        currentRow = sDungeonEncounterStore.LookupEntry(i);
        if(!currentRow)
            continue;
        error_log("Add boss %s", currentRow->Name[0]);
        if(currentRow->Map == m_dungeonInfo->map && m_dungeonInfo->isHeroic() == currentRow->IsHeroic())
        {
            error_log("Add boss %s 2", currentRow->Name[0]);
            m_dungeonBosses.insert(std::make_pair<char*, DungeonEncounterEntry const*>(currentRow->Name[0], currentRow));
        }
    }

    error_log("TELEPORT");
    DugeonInfo* dungeonInfo = sLfgMgr.GetDungeonInfo(m_dungeonInfo->ID);
    if(m_groupType == GROUPTYPE_LFD)
    {
        //Set Leader
        m_leaderGuid = 0;
        for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            if(itr->getSource()->m_lookingForGroup.roles & LEADER)
            {
                m_leaderGuid = itr->getSource()->GetGUID();
                m_leaderName = itr->getSource()->GetName();
                break;
            }
        }
        if(m_leaderGuid == 0)
        {
            m_leaderGuid = GetFirstMember()->getSource()->GetGUID();
            m_leaderName = GetFirstMember()->getSource()->GetName();
        }
        m_lootMethod = GROUP_LOOT;
        m_lootThreshold = ITEM_QUALITY_UNCOMMON;
        m_looterGuid = m_leaderGuid;
        m_dungeonDifficulty = m_dungeonInfo->isHeroic() ? DUNGEON_DIFFICULTY_HEROIC : DUNGEON_DIFFICULTY_NORMAL;
        m_raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
        //Save to DB
        /*
        ALTER TABLE `groups` ADD `healGuid` INT( 11 ) UNSIGNED NOT NULL DEFAULT '0',
            ADD `LfgId` INT( 11 ) UNSIGNED NOT NULL DEFAULT '0',
            ADD `LfgInstanceStatus` TINYINT( 3 ) UNSIGNED NOT NULL;

        ALTER TABLE `group_member` ADD `lfg_join_x` FLOAT NOT NULL DEFAULT '0',
            ADD `lfg_join_y` FLOAT NOT NULL DEFAULT '0',
            ADD `lfg_join_z` FLOAT NOT NULL DEFAULT '0',
            ADD `lfg_join_o` FLOAT NOT NULL DEFAULT '0',
            ADD `lfg_join_map` INT( 11 ) NOT NULL DEFAULT '0',
            ADD `taxi_start` INT( 11 ) NOT NULL DEFAULT '0',
            ADD `taxi_end` INT( 11 ) NOT NULL DEFAULT '0',
            ADD `mount_spell` INT( 11 ) NOT NULL DEFAULT '0';
        */
        CharacterDatabase.PExecute("DELETE FROM groups WHERE groupId ='%u'", m_Id);
        CharacterDatabase.PExecute("DELETE FROM group_member WHERE groupId ='%u'", m_Id);
        CharacterDatabase.PExecute("INSERT INTO groups (groupId,leaderGuid,mainTank,mainAssistant,lootMethod,looterGuid,lootThreshold,icon1,icon2,icon3,icon4,icon5,icon6,icon7,icon8,groupType,difficulty,raiddifficulty,healGuid,LfgId,LfgInstanceStatus) "
            "VALUES ('%u','%u','%u','%u','%u','%u','%u','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','%u','%u','%u','%u','%u','%u')",
            m_Id, GUID_LOPART(m_leaderGuid), GUID_LOPART(m_tank), GUID_LOPART(m_mainAssistant), uint32(m_lootMethod),
            GUID_LOPART(m_looterGuid), uint32(m_lootThreshold), m_targetIcons[0], m_targetIcons[1], m_targetIcons[2], m_targetIcons[3], m_targetIcons[4], m_targetIcons[5], m_targetIcons[6], m_targetIcons[7], uint8(m_groupType), uint32(m_dungeonDifficulty), uint32(m_raidDifficulty), GUID_LOPART(m_heal), m_dungeonInfo->ID, m_instanceStatus);    
    }
    //sort group members...
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        error_log("Do grupy");
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
    
        plr->m_lookingForGroup.groups.erase(originalDungeonId);
        plr->m_lookingForGroup.m_LfgGroup.find(originalDungeonId)->second->unlink();
        delete plr->m_lookingForGroup.m_LfgGroup.find(originalDungeonId)->second;
        plr->m_lookingForGroup.m_LfgGroup.erase(originalDungeonId);

        plr->UnbindInstance(dungeonInfo->start_map, m_dungeonInfo->isHeroic() ? DUNGEON_DIFFICULTY_HEROIC : DUNGEON_DIFFICULTY_NORMAL);
        plr->ResetInstances(INSTANCE_RESET_GROUP_JOIN,false);
        plr->ResetInstances(INSTANCE_RESET_GROUP_JOIN,true);

        if (plr->getLevel() >= LEVELREQUIREMENT_HEROIC)
        {
            if (plr->GetDungeonDifficulty() != GetDungeonDifficulty())
                plr->SetDungeonDifficulty(GetDungeonDifficulty());
            if (plr->GetRaidDifficulty() != GetRaidDifficulty())
                plr->SetRaidDifficulty(GetRaidDifficulty());
        }
        
        plr->SetGroup(this, 1);
        plr->SetGroupInvite(NULL);

        uint32 taxi_start = 0;
        uint32 taxi_end = 0;
        uint32 mount_spell = 0;
        WorldLocation joinLoc;
        if (!plr->m_taxi.empty())
        {
           taxi_start = plr->m_taxi.GetTaxiSource();
           taxi_end = plr->m_taxi.GetTaxiDestination();
           joinLoc = WorldLocation(plr->GetMapId(), plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetOrientation());
        }
        else
        {
            // Mount spell id storing
            if (plr->IsMounted())
            {
                Unit::AuraList const& auras = plr->GetAurasByType(SPELL_AURA_MOUNTED);
                if (!auras.empty())
                    mount_spell = (*auras.begin())->GetId();
            }
            //Nearest graveyard if in dungeon
            if(plr->GetMap()->IsDungeon())
            {
                if (const WorldSafeLocsEntry* entry = sObjectMgr.GetClosestGraveYard(plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetMapId(), plr->GetTeam()))
                    joinLoc = WorldLocation(entry->map_id, entry->x, entry->y, entry->z, 0.0f);
                else
                    joinLoc = WorldLocation(plr->GetMapId(), plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetOrientation());
            }
            else
                joinLoc = WorldLocation(plr->GetMapId(), plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetOrientation());
        }
        //Set info to player
        plr->m_lookingForGroup.joinLoc = joinLoc;
        plr->m_lookingForGroup.taxi_start = taxi_start;
        plr->m_lookingForGroup.taxi_end = taxi_end;
        plr->m_lookingForGroup.mount_spell = mount_spell;

        // resurrect the player
        if (!plr->isAlive())
        {
            plr->ResurrectPlayer(1.0f);
            plr->SpawnCorpseBones();
        }
        // stop taxi flight at port
        if (plr->isInFlight())
        {
            plr->GetMotionMaster()->MovementExpired(false);
            plr->m_taxi.ClearTaxiDestinations();
        }
        CharacterDatabase.PExecute("INSERT INTO group_member(groupId,memberGuid,assistant,subgroup,lfg_join_x,lfg_join_y,lfg_join_z,lfg_join_o,lfg_join_map,taxi_start,taxi_end,mount_spell) "
            "VALUES('%u','%u','%u','%u','%f','%f','%f','%f','%u','%u','%u','%u')",
            m_Id, GUID_LOPART(plr->GetGUID()), 0, 1, joinLoc.coord_x, joinLoc.coord_y, joinLoc.coord_z, joinLoc.orientation, joinLoc.mapid, taxi_start, taxi_end, mount_spell);
    }
    //Teleport
    for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player *plr = itr->getSource();

        error_log("port");
        plr->m_lookingForGroup.queuedDungeons.clear();

        plr->m_lookingForGroup.roles = GetPlayerRole(plr->GetGUID());

        plr->ScheduleDelayedOperation(DELAYED_LFG_ENTER_DUNGEON);

        plr->TeleportTo(dungeonInfo->start_map, dungeonInfo->start_x,
            dungeonInfo->start_y, dungeonInfo->start_z, dungeonInfo->start_o);
    }
    m_inDungeon = true;
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
void LfgGroup::SendUpdate()
{
    Player *player;

    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        player = sObjectMgr.GetPlayer(citr->guid);
        if(!player || !player->GetSession() || player->GetGroup() != this )
            continue;
                                                            // guess size
        WorldPacket data(SMSG_GROUP_LIST, (1+1+1+1+8+4+GetMembersCount()*20));
        data << uint8(GROUPTYPE_LFD);                       // group type (flags in 3.3)
        data << uint8(citr->group);                         // groupid
        data << uint8(GetFlags(*citr));                     // group flags
        data << uint8(0);                                   // 2.0.x, isBattleGroundGroup?
        data << uint8(m_instanceStatus);                    // Instance status 0= not saved, 1= saved, 2 = completed
        data << uint32(m_dungeonInfo->Entry());             // dungeon entry
        data << uint64(0x50000000FFFFFFFELL);               // related to voice chat?
        data << uint32(0);                                  // 3.3, this value increments every time SMSG_GROUP_LIST is sent
        data << uint32(GetMembersCount()-1);
        for(member_citerator citr2 = m_memberSlots.begin(); citr2 != m_memberSlots.end(); ++citr2)
        {
            if(citr->guid == citr2->guid)
                continue;
            Player* member = sObjectMgr.GetPlayer(citr2->guid);
            uint8 onlineState = (member) ? MEMBER_STATUS_ONLINE : MEMBER_STATUS_OFFLINE;
            onlineState = onlineState | ((isBGGroup()) ? MEMBER_STATUS_PVP : 0);

            data << citr2->name;
            data << uint64(citr2->guid);
            data << uint8(onlineState);                     // online-state
            data << uint8(citr2->group);                    // groupid
            data << uint8(GetFlags(*citr2));                // group flags
            data << uint8(GetPlayerRole(citr2->guid));      // 3.3, role?
        }

        data << uint64(m_leaderGuid);                       // leader guid
        if(GetMembersCount()-1)
        {
            data << uint8(m_lootMethod);                    // loot method
            data << uint64(m_looterGuid);                   // looter guid
            data << uint8(m_lootThreshold);                 // loot threshold
            data << uint8(m_dungeonDifficulty);             // Dungeon Difficulty
            data << uint8(m_raidDifficulty);                // Raid Difficulty
            data << uint8(0);                               // 3.3, dynamic difficulty?
        }
        player->GetSession()->SendPacket( &data );
    }
}
LfgLocksMap* LfgGroup::GetLocksList() const
{
    LfgLocksMap *groupLocks = new LfgLocksMap();
    for (GroupReference *itr = ((Group*)this)->GetFirstMember(); itr != NULL; itr = itr->next())
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
    delete groupLocks;
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
        data << uint32(getMSTimeDiff(plr->m_lookingForGroup.joinTime, getMSTime())/1000);   // Player wait time in queue
        plr->GetSession()->SendPacket(&data);
    }
}
void LfgGroup::SendGroupFormed()
{
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
        data << uint32(GetId()); 
        data << uint32(GetKilledBosses());
        data << uint8(0); //silent
        data << uint8(GetMembersCount());
        for (GroupReference *plritr2 = GetFirstMember(); plritr2 != NULL; plritr2 = plritr2->next())
        {
            if(Player *plr = plritr2->getSource())
            {
                uint8 roles = GetPlayerRole(plr->GetGUID());
                //Something got wrong
                if(roles < 2)
                {
                    sLog.outError("LfgMgr: Wrong role for player in SMSG_LFG_PROPOSAL_UPDATE");
                    m_answers.insert(std::pair<uint64, uint8>(plr->GetGUID(), 0));
                }

                data << uint32(roles);
                data << uint8((plr == plritr->getSource()));  // if its you, this is true
                data << uint8(m_inDungeon); // InDungeon
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

void LfgGroup::SendRoleCheckUpdate()
{
    uint8 state = LFG_ROLECHECK_INITIALITING;
    if(m_membersBeforeRoleCheck == 0)
        m_membersBeforeRoleCheck = GetMembersCount();
    //Someone left the group
    else if(m_membersBeforeRoleCheck > GetMembersCount())
        state = LFG_ROLECHECK_ABORTED;

    if(m_rolesProposal.size() == m_membersBeforeRoleCheck)
    {
        state = LFG_ROLECHECK_FINISHED;
        uint8 roles[3] = {0,0,0};
        for(ProposalAnswersMap::iterator itr = m_rolesProposal.begin(); itr != m_rolesProposal.end(); ++itr)
        {
         /*   if(itr->second & TANK)
                roles[0]++;
            if(itr->second & HEALER)
                roles[1]++;
            if(itr->second & DAMAGE)
                roles[2]++;

            //No role?
            if(possibleRoles == 0)
            {
                state = LFG_ROLECHECK_NO_ROLE;
                break;
            }
            if(possibleRoles == 1) */
        }
    }

    Player *leader = sObjectMgr.GetPlayer(GetLeaderGUID());
    if(!leader)
        return;
    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 6 + leader->m_lookingForGroup.queuedDungeons.size() * 4 + 1 + GetMembersCount() * 14);
    data << uint32(state);
    data << uint8(state == LFG_ROLECHECK_INITIALITING); // begining
    data << uint8(leader->m_lookingForGroup.queuedDungeons.size());
    for (LfgDungeonList::const_iterator it = leader->m_lookingForGroup.queuedDungeons.begin(); it != leader->m_lookingForGroup.queuedDungeons.end(); ++it)
        data << uint32((*it)->Entry());
    data << uint8(GetMembersCount());
    //leader first
    data << uint64(GetLeaderGUID());
    if(m_rolesProposal.find(GetLeaderGUID()) != m_rolesProposal.end())
    {
        ProposalAnswersMap::iterator itr = m_rolesProposal.find(GetLeaderGUID());
        data << uint8(1); //ready
        data << uint32(itr->second); //roles 
    }
    else
    {
        data << uint8(0); //ready
        data << uint32(0); //roles
    }
    data << uint8(leader->getLevel());
    for (GroupReference *itr2 = GetFirstMember(); itr2 != NULL; itr2 = itr2->next())
    {
        Player *member = itr2->getSource();
        if(member->GetGUID() == GetLeaderGUID())
            continue;
        data << uint64(member->GetGUID());
        data << uint8(0);
        data << uint32(0);
        data << uint8(member->getLevel());
    }
}

LfgMgr::LfgMgr()
{
    m_updateQueuesBaseTime = sWorld.getConfig(CONFIG_UINT32_LFG_QUEUE_UPDATETIME);
    m_updateQueuesTimer = m_updateQueuesBaseTime;
    m_updateProposalTimer = LFG_TIMER_UPDATE_PROPOSAL;
    m_deleteInvalidTimer = LFG_TIMER_DELETE_INVALID_GROUPS;
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
        /*
        if(!group->isLfgGroup())
        {
        
        }
        else
        {
            LfgGroup *lfgGroup = (LfgGroup*)group;
            lfgGroup->SendRoleCheckUpdate(LFG_ROLECHECK_INITIALITING);

        }








        for (GroupReference *itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player *plr = itr->getSource();
            SendLfgUpdateParty(plr, LFG_UPDATETYPE_JOIN_PROPOSAL);
            WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 6 + player->m_lookingForGroup.queuedDungeons.size * 4 + 1 + group->GetMembersCount() * 14);
            data << uint32(LFG_ROLECHECK_INITIALITING);
            data << uint8(1); // begining
            data << uint8(player->m_lookingForGroup.queuedDungeons.size);
            for (LfgDungeonList::const_iterator it = player->m_lookingForGroup.queuedDungeons.begin(); it != player->m_lookingForGroup.queuedDungeons.end(); ++it)
                data << uint32((*it)->Entry());
            data << uint8(group->GetMembersCount());
            //leader first
            Player *leader = sObjectMgr.GetPlayer(group->GetLeaderGUID());
            data << uint64(group->GetLeaderGUID());
            data << uint8(0); //ready
            data << uint32(0); //roles
            data << uint8(leader->getLevel());
            for (GroupReference *itr2 = group->GetFirstMember(); itr2 != NULL; itr2 = itr2->next())
            {
                Player *member = itr2->getSource();
                if(member->GetGUID() == group->GetLeaderGUID())
                    continue;
                data << uint64(member->GetGUID());
                data << uint8(0);
                data << uint32(0);
                data << uint8(leader->getLevel());
            }
        } */
        WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
        data << uint32(LFG_JOIN_INTERNAL_ERROR);                                  
        data << uint32(0);
        player->GetSession()->SendPacket(&data);
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
            error_log("queed %u", (*it)->ID);
            
            if(m_queuedDungeons[side].find((*it)->ID) != m_queuedDungeons[side].end())
            {
                error_log("Uz existuje, pridavam do hracu jen %u", (*it)->ID);
                m_queuedDungeons[side].find((*it)->ID)->second->players.insert(player->GetGUID());  //Insert player into queue, will be sorted on next queue update
            }else  // None player is qeued into this dungeon
            {
                error_log("Delam novou %u", (*it)->ID);
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
    if(sWorld.getConfig(CONFIG_BOOL_LFG_IMMIDIATE_QUEUE_UPDATE))
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
            error_log("Tu, ID: %u", (*it)->ID);
            QueuedDungeonsMap::iterator itr = m_queuedDungeons[side].find((*it)->ID);
            if(itr == m_queuedDungeons[side].end())                 // THIS SHOULD NEVER HAPPEN
                continue;
            error_log("Tu2, ID: %u, size tu %u, size player %u", (*it)->ID, itr->second->groups.size(), player->m_lookingForGroup.groups.size());
            itr->second->players.erase(player->GetGUID());
            if(itr->second->groups.find(player->m_lookingForGroup.groups.find((*it)->ID)->second) != itr->second->groups.end())
            {
                 error_log(" uvnitr");
                GroupsList::iterator grpitr = itr->second->groups.find(player->m_lookingForGroup.groups.find((*it)->ID)->second);
                if((*grpitr)->IsMember(player->GetGUID()))
                {
                    (*grpitr)->RemoveMember(player->GetGUID(), 0);
                    error_log("REmove member %s", player->GetName());
                }

                if((*grpitr)->GetMembersCount() == 0)
                    itr->second->groups.erase(grpitr);
            }
            if(itr->second->groups.empty() && itr->second->players.empty())
            {
                delete itr->second;
                m_queuedDungeons[side].erase(itr);
            }
        }
        player->m_lookingForGroup.queuedDungeons.clear();
    }
    if(sWorld.getConfig(CONFIG_BOOL_LFG_IMMIDIATE_QUEUE_UPDATE))
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
            error_log("Dung %u", itr->second->dungeonInfo->ID);
            //Remove somehow unaviable players
            for(GroupsList::iterator grpitr1 = itr->second->groups.begin(); grpitr1 != itr->second->groups.end(); ++grpitr1)
                (*grpitr1)->RemoveOfflinePlayers();
            //First, try to merge groups
            for(GroupsList::iterator grpitr1 = itr->second->groups.begin(); grpitr1 != itr->second->groups.end(); ++grpitr1)
            {
                //We can expect that there will be less tanks and healers than dps
              /*  for (GroupReference *plritr = (*grpitr1)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                {
                    Player *plr = plritr->getSource();
                    uint8 rolesCount = 0;
                    uint8 playerRoles = plr->m_lookingForGroup.roles;
                    if(playerRoles & LEADER)
                        playerRoles--;
                    playerRoles -= (*grpitr1)->GetPlayerRole(plr->GetGUID(), false);

                    if(playerRoles == 0)
                        continue;
                    //Try to move from dps to tank
                    if((playerRoles & TANK) && (*grpitr1)->GetTank() == 0 && plr->GetGUID() != (*grpitr1)->GetHeal())
                    {
                        (*grpitr1)->GetDps()->erase(plr->GetGUID());
                        (*grpitr1)->SetTank(plr->GetGUID());
                    }
                    //Try to move from dps to heal
                    if((playerRoles & HEALER) && (*grpitr1)->GetHeal() == 0 && plr->GetGUID() != (*grpitr1)->GetTank())
                    {
                        (*grpitr1)->GetDps()->erase(plr->GetGUID());
                        (*grpitr1)->SetHeal(plr->GetGUID());
                    }

                } */
                for(GroupsList::iterator grpitr2 = itr->second->groups.begin(); grpitr2 != itr->second->groups.end(); ++grpitr2)
                {
                    if((*grpitr1) == (*grpitr2) || !(*grpitr1) || (*grpitr2))
                        continue;
                     error_log("Try tom merge");
                    for (GroupReference *plritr = (*grpitr2)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                    {
                        Player *plr = plritr->getSource();
                        uint8 rolesCount = 0;
                        uint8 playerRoles = plr->m_lookingForGroup.roles;

                        if((*grpitr2)->GetMembersCount() > (*grpitr1)->GetMembersCount() || !(*grpitr1)->HasCorrectLevel(plr->getLevel()))
                            continue;

                        if((playerRoles & TANK) && (*grpitr1)->GetTank() == 0 && plr->GetGUID() != (*grpitr2)->GetHeal())
                        {
                            (*grpitr2)->RemoveMember(plr->GetGUID(), 0);
                            (*grpitr1)->AddMember(plr->GetGUID(), plr->GetName());
                            (*grpitr1)->SetTank(plr->GetGUID());
                        }
                        if((playerRoles & HEALER) && (*grpitr1)->GetHeal() == 0 && plr->GetGUID() != (*grpitr2)->GetTank())
                        {
                            (*grpitr2)->RemoveMember(plr->GetGUID(), 0);
                            (*grpitr1)->AddMember(plr->GetGUID(), plr->GetName());
                            (*grpitr1)->SetHeal(plr->GetGUID());
                        }

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
                error_log("player %s", player->GetName());
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
                    //Found group, so break
                    if(getIntoGroup)
                        break;
                }
                //Failed, so create new LfgGroup
                if(!getIntoGroup)
                {
                    error_log("player %s dela novou grupu", player->GetName());
                    LfgGroup *newGroup = new LfgGroup();
                    newGroup->SetDungeonInfo(itr->second->dungeonInfo);
                    newGroup->AddMember((*plritr), player->GetName());   
                    
                    newGroup->SetGroupId(sObjectMgr.GenerateGroupId());
                    sObjectMgr.AddGroup(newGroup);

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
                    uint32 avgWaitTime = 0;
                    if(Player *tank = sObjectMgr.GetPlayer((*grpitr)->GetTank()))
                    {
                        uint32 waitTimeTank = m_waitTimes[LFG_WAIT_TIME_TANK].find(itr->second->dungeonInfo->ID)->second;
                        uint32 currentTankTime = getMSTimeDiff(tank->m_lookingForGroup.joinTime, getMSTime());
                        uint32 avgWaitTank = (waitTimeTank+currentTankTime)/2;
                        avgWaitTime += avgWaitTank;
                        m_waitTimes[LFG_WAIT_TIME_TANK].find(itr->second->dungeonInfo->ID)->second = avgWaitTank;           
                    }
                    if(Player *heal = sObjectMgr.GetPlayer((*grpitr)->GetHeal()))
                    {
                        uint32 waitTimeHeal = m_waitTimes[LFG_WAIT_TIME_HEAL].find(itr->second->dungeonInfo->ID)->second;
                        uint32 currentHealTime = getMSTimeDiff(heal->m_lookingForGroup.joinTime, getMSTime());
                        uint32 avgTimeHeal = (waitTimeHeal+currentHealTime)/2;
                        avgWaitTime += avgTimeHeal;
                        m_waitTimes[LFG_WAIT_TIME_HEAL].find(itr->second->dungeonInfo->ID)->second = avgTimeHeal;           
                    }
                    for(PlayerList::iterator plritr = (*grpitr)->GetDps()->begin(); plritr != (*grpitr)->GetDps()->end(); ++plritr)
                    {
                        if(Player *dps = sObjectMgr.GetPlayer(*plritr))
                        {
                            uint32 waitTimeDps = m_waitTimes[LFG_WAIT_TIME_DPS].find(itr->second->dungeonInfo->ID)->second;
                            uint32 currTime = getMSTimeDiff(dps->m_lookingForGroup.joinTime, getMSTime());
                            uint32 avgWaitDps = (waitTimeDps+currTime)/2;
                            avgWaitTime += avgWaitDps;
                            m_waitTimes[LFG_WAIT_TIME_DPS].find(itr->second->dungeonInfo->ID)->second = avgWaitDps;
                        }
                    }
                    m_waitTimes[LFG_WAIT_TIME].find(itr->second->dungeonInfo->ID)->second = (avgWaitTime/5);
                    
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
    m_updateQueuesTimer = m_updateQueuesBaseTime;
}
void LfgMgr::UpdateFormedGroups()
{
    for(int i = 0; i < MAX_LFG_FACTION; ++i)
    {
        for(GroupsList::iterator grpitr = formedGroups[i].begin(); grpitr != formedGroups[i].end(); ++grpitr)
        {
            if((*grpitr)->RemoveOfflinePlayers())
                continue;
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
                        (*grpitr)->RemoveMember(plritr->getSource()->GetGUID(), 0);
                        AddToQueue(plritr->getSource());
                    }
                    delete *grpitr;
                    formedGroups[i].erase(grpitr);
                    error_log("Moved");
                }
                //We are good to go, sir
                else
                {
                    for (GroupReference *plritr = (*grpitr)->GetFirstMember(); plritr != NULL; plritr = plritr->next())
                    {
                        SendLfgUpdatePlayer(plritr->getSource(), LFG_UPDATETYPE_GROUP_FOUND);
                        SendLfgUpdatePlayer(plritr->getSource(), LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                        
                        //I think this is useless when you are not in group, but its sent by retail serverse anyway...
                        SendLfgUpdateParty(plritr->getSource(), LFG_UPDATETYPE_REMOVED_FROM_QUEUE);       
                    }
                    (*grpitr)->TeleportToDungeon();
                    formedGroups[i].erase(*grpitr);
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
    data << uint32(0);                                      // some "variable" money?
    data << uint32(0);                                      // some "variable" xp?
    
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
            minlevel = instance->levelMin == 0 ? currentRow->minlevel : instance->levelMin;
            maxlevel = instance->levelMax == 0 ? currentRow->maxlevel : instance->levelMax;
        }
        else
        {
            minlevel = currentRow->minlevel;
            maxlevel = currentRow->maxlevel;
        }
        type = LFG_LOCKSTATUS_OK;
        DungeonInfoMap::iterator itr = m_dungeonInfoMap.find(currentRow->ID);
        InstancePlayerBind *playerBind = plr->GetBoundInstance(currentRow->map, Difficulty(currentRow->heroic));

        if(currentRow->expansion > plr->GetSession()->Expansion())
            type = LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION;
        else if(minlevel > plr->getLevel())
            type = LFG_LOCKSTATUS_TOO_LOW_LEVEL;
        else if(plr->getLevel() > maxlevel)
            type = LFG_LOCKSTATUS_TOO_HIGH_LEVEL;
        else if((playerBind && playerBind->perm) || (itr != m_dungeonInfoMap.end() && itr->second->locked) || itr == m_dungeonInfoMap.end())
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
/*
CREATE TABLE IF NOT EXISTS `lfg_dungeon_info` (
  `ID` mediumint(8) NOT NULL DEFAULT '0' COMMENT 'ID from LfgDugeons.dbc',
  `name` text,
  `lastBossId` int(11) NOT NULL DEFAULT '0' COMMENT 'ID from DungeonEncounters.dbc',
  `start_map` mediumint(8) NOT NULL DEFAULT '0',
  `start_x` float NOT NULL DEFAULT '0',
  `start_y` float NOT NULL DEFAULT '0',
  `start_z` float NOT NULL DEFAULT '0',
  `start_o` int(11) NOT NULL,
  `locked` tinyint(3) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
*/
void LfgMgr::LoadDungeonsInfo()
{
    // In case of reload
    m_dungeonInfoMap.clear();

    //Fill locked for dungeons without info in db
    LFGDungeonEntry const *currentRow;
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        currentRow = sLFGDungeonStore.LookupEntry(i);
        if(!currentRow)
            continue;
        DugeonInfo *info = new DugeonInfo();
        info->ID = currentRow->ID;
        info->locked = true;
        m_dungeonInfoMap.insert(std::make_pair<uint32, DugeonInfo*>(info->ID, info));
    }
    uint32 count = 0;
    //                                                0   1     2           3          4        5        6        7        8
    QueryResult *result = WorldDatabase.Query("SELECT ID, name, lastBossId, start_map, start_x, start_y, start_z, start_o, locked  FROM lfg_dungeon_info");

    if( !result )
    {
        barGoLink bar( 1 );
        bar.step();
        sLog.outString();
        sLog.outString( ">> Loaded %u LFG dungeon info entries.", count );
        return;
    }
    barGoLink bar( (int)result->GetRowCount() );
    do
    {
        Field *fields = result->Fetch();

        bar.step();
        
        DugeonInfo *info = new DugeonInfo();
        info->ID                      = fields[0].GetUInt32();
        info->name                    = fields[1].GetCppString();
        info->lastBossId              = fields[2].GetUInt32();
        info->start_map               = fields[3].GetUInt32();
        info->start_x                 = fields[4].GetFloat();
        info->start_y                 = fields[5].GetFloat();
        info->start_z                 = fields[6].GetFloat();
        info->start_o                 = fields[7].GetFloat();
        info->locked                  = fields[8].GetBool();
       
        if(!sLFGDungeonStore.LookupEntry(info->ID))
        {
            sLog.outErrorDb("Entry listed in 'lfg_dungeon_info' has non-exist LfgDungeon.dbc id %u, skipping.", info->ID);
            delete info;
            continue;
        }
        if(!sDungeonEncounterStore.LookupEntry(info->lastBossId) && info->lastBossId != 0)
        {
            sLog.outErrorDb("Entry listed in 'lfg_dungeon_info' has non-exist DungeonEncounter.dbc id %u, skipping.", info->lastBossId);
            delete info;
            continue;   
        }
        m_dungeonInfoMap.find(info->ID)->second = info;
        ++count;
    } while( result->NextRow() );

    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u LFG dungeon info entries.", count );
}
uint32 LfgMgr::GetAvgWaitTime(uint32 dugeonId, uint8 slot, uint8 roles)
{
    switch(slot)
    {
        case LFG_WAIT_TIME:
        case LFG_WAIT_TIME_TANK:
        case LFG_WAIT_TIME_HEAL:
        case LFG_WAIT_TIME_DPS:
            return (m_waitTimes[slot].find(dugeonId)->second / 1000);  // No check required, if this method is called, some data is already in array
        case LFG_WAIT_TIME_AVG:
            if(roles & TANK)
            {
                if(!(roles & HEALER) && !(roles & DAMAGE))
                    return (m_waitTimes[LFG_WAIT_TIME_TANK].find(dugeonId)->second / 1000);
            }
            else if(roles & HEALER)
            {
                if(!(roles & DAMAGE))
                    return (m_waitTimes[LFG_WAIT_TIME_HEAL].find(dugeonId)->second / 1000);
            }
            else if(roles & DAMAGE)
                return (m_waitTimes[LFG_WAIT_TIME_DPS].find(dugeonId)->second / 1000);
            return (m_waitTimes[LFG_WAIT_TIME].find(dugeonId)->second / 1000);
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
    }
}