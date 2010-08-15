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
#include "LfgGroup.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "ProgressBar.h"
#include "Unit.h"
#include "SpellAuras.h"

LfgGroup::LfgGroup(bool premade) : Group()
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
    m_groupType = premade ? GROUPTYPE_LFD_2 : GROUPTYPE_LFD;
    m_instanceStatus = INSTANCE_NOT_SAVED;
    m_inDungeon = false;
    m_isRandom = false;
    m_dungeonInfo = NULL;
    m_membersBeforeRoleCheck = 0;
    m_voteKickTimer = 0;
    randomDungeonEntry = 0;
}
LfgGroup::~LfgGroup()
{
    sObjectMgr.RemoveGroup(this);
    delete dps;
}

void LfgGroup::ResetGroup()
{
    m_answers.clear();
    m_rolesProposal.clear();
    m_readycheckTimer = 0;
    m_membersBeforeRoleCheck = 0;
    m_voteKickTimer = 0;
}

bool LfgGroup::LoadGroupFromDB(Field *fields)
{
    if(!Group::LoadGroupFromDB(fields))
        return false;
    
    m_tank = m_mainTank;
    m_heal = fields[1].GetUInt64();
    m_dungeonInfo = sLFGDungeonStore.LookupEntry(fields[19].GetUInt32());
    randomDungeonEntry = fields[20].GetUInt32();
    m_instanceStatus = fields[21].GetUInt8();
    m_inDungeon = true; 
    return true;
}
bool LfgGroup::AddMember(const uint64 &guid, const char* name)
{
    Player *player = sObjectMgr.GetPlayer(guid);
    if(!player)
        return false;
    
    if(GetMembersCount() == 0)
        m_baseLevel = player->getLevel();
    MemberSlot member;
    member.guid      = guid;
    member.name      = name;
    member.group     = 1;
    member.assistant = false;
    m_memberSlots.push_back(member);
   
    player->m_lookingForGroup.groups.insert(std::pair<uint32, LfgGroup*>(m_dungeonInfo->ID,this));
    return true;
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
        if(method == 1)
        {
            WorldPacket data(SMSG_GROUP_UNINVITE, 0);
            player->GetSession()->SendPacket( &data );
        }
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

uint8 LfgGroup::GetPlayerRole(uint64 guid, bool withLeader, bool joinedAs) const
{
    if(joinedAs)
    {
        if(Player *player = sObjectMgr.GetPlayer(guid))
        {
            if(withLeader)
                return player->m_lookingForGroup.roles;
            else
                return (player->m_lookingForGroup.roles & LEADER) ? player->m_lookingForGroup.roles-LEADER : player->m_lookingForGroup.roles;
        }
        return 0;
    }
    uint8 roles = (m_leaderGuid == guid && withLeader) ? LEADER : 0;
    if(m_tank == guid)
        roles |= TANK;
    else if(m_heal == guid)
        roles |= HEALER;
    else if(dps->find(guid) != dps->end())
        roles |= DAMAGE;
    return roles;        
}

bool LfgGroup::RemoveOfflinePlayers()  // Return true if group is empty after check
{
    //Hack wtf?
    LfgGroup *tato2 = this;
    LfgGroup **tato = &tato2;
    uint64 adress = uint64(tato);
    if(!this || adress == 0x1 ||  m_memberSlots.empty())

    if(m_memberSlots.empty())
    {
        sLfgMgr.AddGroupToDelete(this);
        return true;
    }
    member_citerator citr, next;
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); citr = next)
    {
        next = citr;
        ++next; 
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if((!plr || (!plr->GetSession() && !plr->IsBeingTeleported())) && premadePlayers.find(citr->guid) == premadePlayers.end())
        {
            uint64 guid = citr->guid;
            RemoveMember(guid, 0);
        }
    }
    //flush empty group
    if(GetMembersCount() == 0)
    {
        sLfgMgr.AddGroupToDelete(this);
        return true;
    }
    return false;
}

void LfgGroup::KilledCreature(Creature *creature)
{
    if(creature->GetCreatureInfo()->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND)
    {
		if(m_instanceStatus == INSTANCE_NOT_SAVED)
            m_instanceStatus = INSTANCE_SAVED;
        //There are mask values for bosses, this is not correct
        m_killedBosses += !m_killedBosses ? 1 : m_killedBosses*2;
    }
    if(creature->GetEntry() == sLfgMgr.GetDungeonInfo(m_dungeonInfo->ID)->lastBossId)
    {
        //Last boss
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
            LfgReward *reward = sLfgMgr.GetDungeonReward((randomDungeonEntry & 0x00FFFFFF), plr->m_lookingForGroup.DoneDungeon((randomDungeonEntry & 0x00FFFFFF), plr), plr->getLevel());
            if (!reward)
                continue;
            reward->questInfo->SetFlag(QUEST_FLAGS_AUTO_REWARDED);
            plr->CompleteQuest(reward->questInfo->GetQuestId());
        }
    }  

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
    if(m_inDungeon)
    {
        for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        {
            Player *plr = sObjectMgr.GetPlayer(citr->guid);
            if(!plr || !plr->GetSession())
                continue;

            if(plr->GetMapId() == m_dungeonInfo->map)
            {
                sLfgMgr.SendLfgUpdatePlayer(plr, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                sLfgMgr.SendLfgUpdateParty(plr, LFG_UPDATETYPE_REMOVED_FROM_QUEUE);
                continue;
            }
            DungeonInfo* dungeonInfo = sLfgMgr.GetDungeonInfo(m_dungeonInfo->ID);
            uint32 originalDungeonId = m_dungeonInfo->ID;
            if(m_isRandom)
                originalDungeonId = (randomDungeonEntry & 0x00FFFFFF);
            plr->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS, GetMembersCount()-1);
            TeleportPlayer(plr, dungeonInfo, originalDungeonId);
        }
        SendUpdate();
        return;
    }
    //If random, then select here
    uint32 originalDungeonId = m_dungeonInfo->ID;
    if(m_dungeonInfo->type == LFG_TYPE_RANDOM)
    {
        randomDungeonEntry = m_dungeonInfo->Entry();
        m_isRandom = true;
        LfgLocksMap *groupLocks = GetLocksList();
        std::vector<LFGDungeonEntry const*> options;
        options.clear();
        LFGDungeonEntry const *currentRow = NULL;
        //Possible dungeons
        for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
        {
            currentRow = sLFGDungeonStore.LookupEntry(i);
            if(!currentRow)
                continue;
            if(currentRow->type != LFG_TYPE_RANDOM && currentRow->grouptype == m_dungeonInfo->grouptype)
                options.push_back(currentRow);
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
                    DungeonInfo* dungeonInfo = sLfgMgr.GetDungeonInfo((*itr2)->dungeonInfo->ID);
                    if(dungeonInfo->locked || (*itr2)->lockType != LFG_LOCKSTATUS_RAID_LOCKED)
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
            for(member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
            {
                Player *plr = sObjectMgr.GetPlayer(itr->guid);
                if(!plr)
                    continue;
                sLfgMgr.SendLfgUpdatePlayer(plr, LFG_UPDATETYPE_GROUP_DISBAND);
                sLog.outError("LfgMgr: Cannot find any random dungeons for player %s", plr->GetName());
                plr->GetSession()->SendNotification("Cannot find any random dungeons for this group, you have to find new group. We are sorry");
                RemoveMember(plr->GetGUID(), 0);
            }
            sLfgMgr.AddGroupToDelete(this);
            return;
        }
        //Select dungeon, there should be also bind check
        uint32 tmp = urand(0, options.size()-1);
        m_dungeonInfo = options[tmp];
    }

    DungeonInfo* dungeonInfo = sLfgMgr.GetDungeonInfo(m_dungeonInfo->ID);
    //Set Leader
    m_leaderGuid = 0;   
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
            continue;
        if(m_groupType == GROUPTYPE_LFD_2 && plr->GetGroup())
        {
            m_leaderGuid = plr->GetGroup()->GetLeaderGUID();
            m_leaderName = plr->GetGroup()->GetLeaderName();
            break;
        }
        else if(plr->m_lookingForGroup.roles & LEADER)
        {
            m_leaderGuid = plr->GetGUID();
            m_leaderName = plr->GetName();
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
    CharacterDatabase.PExecute("DELETE FROM groups WHERE groupId ='%u'", m_Id);
    CharacterDatabase.PExecute("DELETE FROM group_member WHERE groupId ='%u'", m_Id);
    CharacterDatabase.PExecute("INSERT INTO groups (groupId,leaderGuid,mainTank,mainAssistant,lootMethod,looterGuid,lootThreshold,icon1,icon2,icon3,icon4,icon5,icon6,icon7,icon8,groupType,difficulty,raiddifficulty,healGuid,LfgId,LfgRandomEntry,LfgInstanceStatus) "
        "VALUES ('%u','%u','%u','%u','%u','%u','%u','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','" UI64FMTD "','%u','%u','%u','%u','%u','%u','%u')",
        m_Id, GUID_LOPART(m_leaderGuid), GUID_LOPART(m_tank), GUID_LOPART(m_mainAssistant), uint32(m_lootMethod),
        GUID_LOPART(m_looterGuid), uint32(m_lootThreshold), m_targetIcons[0], m_targetIcons[1], m_targetIcons[2], m_targetIcons[3], m_targetIcons[4], m_targetIcons[5], m_targetIcons[6], m_targetIcons[7], uint8(m_groupType), uint32(m_dungeonDifficulty), uint32(m_raidDifficulty), GUID_LOPART(m_heal), m_dungeonInfo->ID, randomDungeonEntry, m_instanceStatus);    
    //sort group members...
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
            continue;

        plr->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_LFD_TO_GROUP_WITH_PLAYERS, GetMembersCount()-1);   
        TeleportPlayer(plr, dungeonInfo, originalDungeonId);
    }
    m_inDungeon = true;
}

void LfgGroup::TeleportPlayer(Player *plr, DungeonInfo *dungeonInfo, uint32 originalDungeonId)
{
    if(m_inDungeon && premadePlayers.find(plr->GetGUID()) != premadePlayers.end())
    {
        for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player *player = itr->getSource();
            if(!player)
                continue;
            if(player->GetMapId() == GetDungeonInfo()->map)
            {
                WorldLocation loc;
                player->GetPosition(loc);
                plr->TeleportTo(loc);
                return;
            }
        }
    }
    else
    {
        for(GroupMap::iterator itr = plr->m_lookingForGroup.groups.begin(); itr != plr->m_lookingForGroup.groups.end(); ++itr)
        {
            if(itr->first != originalDungeonId)
            {
                itr->second->RemoveMember(plr->GetGUID(), 0);
                if(itr->second->GetMembersCount() == 0)
                    sLfgMgr.AddGroupToDelete(itr->second);
            }
        }
        plr->m_lookingForGroup.groups.clear();
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
        plr->UninviteFromGroup();
        if(Group *group = plr->GetGroup())
        {
            if(!group->isLfgGroup())
            {
                group->RemoveMember(plr->GetGUID(), 0);
                if(group->GetMembersCount() == 0)
                    group->Disband(true);
            }
        }
        plr->SetGroup(this, 1);
        plr->SetGroupInvite(NULL);
    }

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
        plr->GetMotionMaster()->Clear(false, true);
        plr->m_taxi.ClearTaxiDestinations();
    }
    CharacterDatabase.PExecute("DELETE FROM group_member WHERE memberGuid='%u'", GUID_LOPART(plr->GetGUID()));
    CharacterDatabase.PExecute("INSERT INTO group_member(groupId,memberGuid,assistant,subgroup,lfg_join_x,lfg_join_y,lfg_join_z,lfg_join_o,lfg_join_map,taxi_start,taxi_end,mount_spell) "
        "VALUES('%u','%u','%u','%u','%f','%f','%f','%f','%u','%u','%u','%u')",
        m_Id, GUID_LOPART(plr->GetGUID()), 0, 1, joinLoc.coord_x, joinLoc.coord_y, joinLoc.coord_z, joinLoc.orientation, joinLoc.mapid, taxi_start, taxi_end, mount_spell);

    //TELEPORT
    LfgDungeonList *queuedList = &plr->m_lookingForGroup.queuedDungeons;
    queuedList->clear();
    plr->m_lookingForGroup.roles = GetPlayerRole(plr->GetGUID());
    plr->ScheduleDelayedOperation(DELAYED_LFG_ENTER_DUNGEON);
    plr->ScheduleDelayedOperation(DELAYED_SAVE_PLAYER);
    if(!m_inDungeon)
    {
        plr->TeleportTo(dungeonInfo->start_map, dungeonInfo->start_x,
            dungeonInfo->start_y, dungeonInfo->start_z, dungeonInfo->start_o);
    }
    else
    {
        for (GroupReference *itr = GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player *player = itr->getSource();
            if(!player)
                continue;
            if(player->GetMapId() == GetDungeonInfo()->map)
            {
                WorldLocation loc;
                player->GetPosition(loc);
                plr->TeleportTo(loc);
                return;
            }
        }
    }
}

bool LfgGroup::HasCorrectLevel(uint8 level)
{
    //Non random
    if(!m_dungeonInfo->isRandom())
    {
        if(level >= m_dungeonInfo->minlevel && level <= m_dungeonInfo->maxlevel)
            return true;
        return false;
    }
    //And random
    switch(m_dungeonInfo->grouptype)
    {
        case LFG_GROUPTYPE_CLASSIC: 
        case LFG_GROUPTYPE_BC_NORMAL:
            if(m_baseLevel > level)
                return (m_baseLevel - level <= 5);
            else
                return (level - m_baseLevel <= 5);
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
        data << uint8(m_groupType);                         // group type (flags in 3.3)
        data << uint8(citr->group);                         // groupid
        data << uint8(GetFlags(*citr));                     // group flags
        data << uint8(GetPlayerRole(citr->guid));           // 2.0.x, isBattleGroundGroup? <--- Its flags or maybe more likely roles....?
        data << uint8(m_instanceStatus);                    // Instance status 0= not saved, 1= saved, 2 = completed
        data << uint32(m_dungeonInfo->Entry());             // dungeon entry
        data << uint64(0x1F54000004D3B000);                 // related to voice chat?
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
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
            continue;

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
    Player *plr;
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
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
    if(!premadePlayers.empty())
    {
        ResetGroup();
        for(PlayerList::iterator itr = premadePlayers.begin(); itr != premadePlayers.end(); ++itr)
            m_answers.insert(std::pair<uint64, uint8>(*itr, 1));
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
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
            continue;
        plr->GetSession()->SendPacket(&data);
    }

    SendProposalUpdate(LFG_PROPOSAL_WAITING);
}

void LfgGroup::SendProposalUpdate(uint8 state)
{
    Player *plr;
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession() || (m_inDungeon && plr->GetMapId() == GetDungeonInfo()->map))
            continue;
        //Correct - 3.3.3a
        WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE);
        data << uint32(GetDungeonInfo()->Entry());
        data << uint8(state); 
        data << uint32(GetId()); 
        data << uint32(GetKilledBosses());
        data << uint8(0); //silent
        uint8 membersCount = 0;
        for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        {
            Player *plr2 = sObjectMgr.GetPlayer(citr->guid);
            if(plr2 && plr2->GetSession())
                ++membersCount;
        }
        data << uint8(membersCount);
        for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        {
            if(Player *plr2 = sObjectMgr.GetPlayer(citr->guid))
            {
                //error_log("Player %s", plr2->GetName());
                uint8 roles = GetPlayerRole(plr2->GetGUID());
                //Something got wrong
                if(roles < 2)
                {
                    sLog.outError("LfgMgr: Wrong role for player %s in SMSG_LFG_PROPOSAL_UPDATE", plr2->GetName());
                    m_answers.insert(std::pair<uint64, uint8>(plr2->GetGUID(), 0));
                }

                data << uint32(roles);
                data << uint8(plr == plr2);  // if its you, this is true
                data << uint8(m_inDungeon); // InDungeon
                data << uint8(premadePlayers.find(plr2->GetGUID()) != premadePlayers.end()); // Same group
                //If player agrees with dungeon, these two are 1
                if(m_answers.find(plr2->GetGUID()) != m_answers.end())
                {
                    data << uint8(1);
                    data << uint8(m_answers.find(plr2->GetGUID())->second);
                }
                else
                {
                    data << uint8(0);  //Answer
                    data << uint8(0);  //Accept
                }
            }
        }
        plr->GetSession()->SendPacket(&data);
    }
}

void LfgGroup::UpdateRoleCheck(uint32 diff)
{
    if(diff != 0)
    {
        m_readycheckTimer += diff;
        if(m_readycheckTimer >= LFG_TIMER_READY_CHECK && m_membersBeforeRoleCheck != m_rolesProposal.size())
        {
            SendRoleCheckUpdate(LFG_ROLECHECK_MISSING_ROLE);
            for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
            {
                Player *player = sObjectMgr.GetPlayer(citr->guid);
                if(!player || !player->GetSession())
                    continue;
                sLfgMgr.SendLfgUpdateParty(player, LFG_UPDATETYPE_ROLECHECK_FAILED);
                if(player->GetGUID() == GetLeaderGUID())
                {
                    WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
                    data << uint32(LFG_JOIN_FAILED);                                  
                    data << uint32(0);
                    player->GetSession()->SendPacket(&data);
                }
            }
            return;
        }
    }

    bool offline = false;
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *player = sObjectMgr.GetPlayer(citr->guid);
        if(!player)
        {
            offline = true;
            break;
        }
    }
    if(GetMembersCount() != m_membersBeforeRoleCheck || offline)
    {
        SendRoleCheckUpdate(LFG_ROLECHECK_ABORTED);
        for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        {
            Player *player = sObjectMgr.GetPlayer(citr->guid);
            if(!player)
                continue;
            sLfgMgr.SendLfgUpdateParty(player, LFG_UPDATETYPE_ROLECHECK_ABORTED);
            if(player->GetGUID() == GetLeaderGUID())
            {
                WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
                data << uint32(LFG_JOIN_FAILED);                                  
                data << uint32(0);
                player->GetSession()->SendPacket(&data);
            }
        }
        sLfgMgr.AddCheckedGroup(this, false);
        return;
    }
    if(m_rolesProposal.empty())
        return;
    //Offline members checked at join
    //Check roles
    if(m_membersBeforeRoleCheck != m_rolesProposal.size())
        return;

    uint64 TankOnly = 0;
    uint64 HealOnly = 0;
    PlayerList *dpsOnly = new PlayerList();
    ProposalAnswersMap *others = new ProposalAnswersMap();
    uint8 error = 0;
    for(ProposalAnswersMap::iterator itr = m_rolesProposal.begin(); itr != m_rolesProposal.end(); ++itr)
    {
        uint8 role = (itr->second & LEADER) ? itr->second-1 : itr->second;
        if(role == 0)
        {
            error = LFG_ROLECHECK_NO_ROLE;
            break;
        }
        
        if((role == TANK && TankOnly != 0) || (role == HEALER && HealOnly != 0) || (role == DAMAGE && dpsOnly->size() == 3))
        {
            error = LFG_ROLECHECK_WRONG_ROLES;
            break;
        }
        else if(role == TANK)
            TankOnly = itr->first;
        else if(role == HEALER)
            HealOnly = itr->first;
        else if(role == DAMAGE)
            dpsOnly->insert(itr->first);
        else
            others->insert(std::make_pair<uint64, uint8>(itr->first, itr->second));
    }
    if(error == 0 && !others->empty())
    {
        for(ProposalAnswersMap::iterator itr = others->begin(); itr != others->end(); ++itr)
        {
            if((itr->second & TANK) && TankOnly == 0)
            {
                TankOnly = itr->first;
                others->erase(itr);
            }
            else if((itr->second & HEALER) && HealOnly == 0)
            {
                HealOnly = itr->first;
                others->erase(itr);
            }
            else if((itr->second & DAMAGE) && dpsOnly->size() != 3)
            {
                dpsOnly->insert(itr->first);
                others->erase(itr);
            }
        }
        if(!others->empty())
            error = LFG_ROLECHECK_WRONG_ROLES;
    }
    Player *leader = sObjectMgr.GetPlayer(GetLeaderGUID());
    if(error != 0 || !leader || !leader->GetSession())
    {
        SendRoleCheckUpdate(error);
        for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        {
            Player *player = sObjectMgr.GetPlayer(citr->guid);
            if(!player || !player->GetSession())
                continue;
      
            sLfgMgr.SendLfgUpdateParty(player, LFG_UPDATETYPE_ROLECHECK_FAILED);
            if(player->GetGUID() == GetLeaderGUID())
            {
                WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
                data << uint32(LFG_JOIN_FAILED);                                  
                data << uint32(0);
                player->GetSession()->SendPacket(&data);
            }
        }
        sLfgMgr.AddCheckedGroup(this, false);
        return;
    }
    //Move group to queue
    SendRoleCheckUpdate(LFG_ROLECHECK_FINISHED);

    LfgDungeonList *queued = &leader->m_lookingForGroup.queuedDungeons;

    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *player = sObjectMgr.GetPlayer(citr->guid);
        if(!player || !player->GetSession())
            continue;
        sLfgMgr.SendLfgUpdateParty(player,  LFG_UPDATETYPE_ADDED_TO_QUEUE);
        
        if(player->GetGUID() == GetLeaderGUID())
        {
            WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
            data << uint32(LFG_JOIN_OK);                                  
            data << uint32(0);
            player->GetSession()->SendPacket(&data);
        }
        else
            player->m_lookingForGroup.queuedDungeons = *queued;
        if(m_inDungeon)
            premadePlayers.insert(player->GetGUID());

    } 
    m_tank = TankOnly;
    m_heal = HealOnly;
    dps = dpsOnly;

    sLfgMgr.AddCheckedGroup(this, true);
}

void LfgGroup::SendRoleCheckUpdate(uint8 state)
{
    if(state == LFG_ROLECHECK_INITIALITING)
    {
        ResetGroup();
        if(m_inDungeon)
            premadePlayers.clear();
        m_membersBeforeRoleCheck = GetMembersCount();
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
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *member = sObjectMgr.GetPlayer(citr->guid);
        if(!member || !member->GetSession() || member->GetGUID() == GetLeaderGUID())
            continue;

        data << uint64(member->GetGUID());
        data << uint8(0);
        data << uint32(0);
        data << uint8(member->getLevel());
    }

    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *plr = sObjectMgr.GetPlayer(citr->guid);
        if(!plr || !plr->GetSession())
            continue;
        plr->GetSession()->SendPacket(&data);
    }
}

void LfgGroup::InitVoteKick(uint64 who, Player *initiator, std::string reason)
{
    //Checks first
    PartyResult error = ERR_PARTY_RESULT_OK;
    if(GetMembersCount() <= 3)
        error = ERR_PARTY_LFG_BOOT_TOO_FEW_PLAYERS;
    else if(m_instanceStatus == INSTANCE_COMPLETED)
        error = ERR_PARTY_LFG_BOOT_DUNGEON_COMPLETE;
    else if(m_voteToKick.isInProggres)
        error = ERR_PARTY_LFG_BOOT_IN_PROGRESS;

    initiator->GetSession()->SendPartyResult(PARTY_OP_LEAVE, "", error);
    if(error != ERR_PARTY_RESULT_OK)
        return;

    m_voteToKick.Reset();
    m_voteToKick.isInProggres = true;
    m_voteToKick.victim = who;
    m_voteToKick.beginTime = getMSTime();
    m_voteToKick.reason = reason;
    m_voteKickTimer = 0;

    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *member = sObjectMgr.GetPlayer(citr->guid);
        if(!member || !member->GetSession())
            continue;
        SendBootPlayer(member);
    }

    sLfgMgr.AddVoteKickGroup(this);   
}

// return true = remove from update list, false = continue
bool LfgGroup::UpdateVoteToKick(uint32 diff)
{
    if(!m_voteToKick.isInProggres)
        return true;

    if(diff)
    {
        if(m_voteToKick.GetTimeLeft() <= 0)
        {
            m_voteToKick.isInProggres = false;
            for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
            {
                Player *member = sObjectMgr.GetPlayer(citr->guid);
                if(!member || !member->GetSession())
                    continue;
                SendBootPlayer(member);
            }
            m_voteToKick.Reset();
            return true;
        }
        return false;
    }

    //Send Update
    for(member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player *member = sObjectMgr.GetPlayer(citr->guid);
        if(!member || !member->GetSession())
            continue;
        SendBootPlayer(member);
    }

    if(m_voteToKick.GetVotesNum(false) < 3)
        return false;
    else if(m_voteToKick.GetVotesNum(true) >= 3)
    {
        
        Player *victim = sObjectMgr.GetPlayer(m_voteToKick.victim);
        RemoveMember(m_voteToKick.victim, 1);
        if(victim && victim->GetSession())
        {
            WorldLocation teleLoc = victim->m_lookingForGroup.joinLoc;
            if(teleLoc.coord_x != 0 && teleLoc.coord_y != 0 && teleLoc.coord_z != 0)
            {
                victim->ScheduleDelayedOperation(DELAYED_LFG_MOUNT_RESTORE);
                victim->ScheduleDelayedOperation(DELAYED_LFG_TAXI_RESTORE);
                victim->RemoveAurasDueToSpell(LFG_BOOST);
                victim->TeleportTo(teleLoc);
            }
        }
        m_voteToKick.Reset();
        SendUpdate();
        return true;
    }
    return false;
}

void LfgGroup::SendBootPlayer(Player *plr)
{
    if(plr->GetGUID() == m_voteToKick.victim)
        return;

    WorldPacket data(SMSG_LFG_BOOT_PLAYER, 27 + m_voteToKick.reason.length());
    data << uint8(m_voteToKick.isInProggres);               // Vote in progress
    data << uint8(m_voteToKick.PlayerVoted(plr->GetGUID()));// Did Vote
    data << uint8(m_voteToKick.GetVote(plr->GetGUID()));    // Agree
    data << uint64(m_voteToKick.victim);                    // Victim GUID
    data << uint32(m_voteToKick.GetVotesNum(false));        // Total Votes
    data << uint32(m_voteToKick.GetVotesNum(true));         // Agree Count
    data << uint32(m_voteToKick.GetTimeLeft());             // Time Left
    data << uint32(LFG_VOTES_NEEDED);                       // Needed Votes
    data << m_voteToKick.reason.c_str();                    // Kick reason

    plr->GetSession()->SendPacket(&data);
}