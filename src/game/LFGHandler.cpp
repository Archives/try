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

#include "WorldSession.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"
#include "Player.h"
#include "WorldPacket.h"
#include "ObjectMgr.h"
#include "World.h"
#include "LfgMgr.h"
#include "DBCStores.h"

void WorldSession::HandleLfgJoinOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_LFG_JOIN");
    if(!sWorld.getConfig(CONFIG_BOOL_ALLOW_JOIN_LFG))
    {
        WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
        data << uint32(LFG_JOIN_INTERNAL_ERROR);                // Check Result
        data << uint32(0);                                      // Check Value
        SendPacket(&data);
        return;
    }

    uint32 error = LFG_JOIN_OK;
    uint32 roles;
    uint8 count, unk;
    std::string comment;

    recv_data >> roles;
    recv_data.read_skip<uint8>();                           // unk - always 0
    recv_data.read_skip<uint8>();                           // unk - always 0
    recv_data >> count;
    
    //Some first checks
    if(_player->InBattleGround() || _player->InBattleGroundQueue() || _player->InArena())
        error = LFG_JOIN_USING_BG_SYSTEM;
    else if(_player->HasAura(LFG_DESERTER))
        error = LFG_JOIN_PARTY_DESERTER;
    else if(_player->GetGroup() && _player->GetGroup()->GetLeaderGUID() != _player->GetGUID())
            error = LFG_JOIN_NOT_MEET_REQS;

    if(error != LFG_JOIN_OK)
    {
        WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
        data << uint32(error);                                  // Check Result
        data << uint32(0);                                      // Check Value
        SendPacket(&data);
        return;
    }
    // for every dungeon check also if theres some error
    for(uint8 i = 0; i < count; ++i)
    {
        uint32 dungeonEntry;
        recv_data >> dungeonEntry;
        LFGDungeonEntry const *dungeonInfo = sLFGDungeonStore.LookupEntry((dungeonEntry & 0x00FFFFFF));
        if(!dungeonInfo)
        {
            sLog.outError("WORLD: Player %u has attempted to join for non-exist dungeon from LFG", _player->GetGUID());
            error = LFG_JOIN_DUNGEON_INVALID;
        }
        //Raids are not implemented yet, and they are not so popular on offi, so get rid of them for now
        else if(dungeonInfo->type == LFG_TYPE_RAID)
            error = LFG_JOIN_INTERNAL_ERROR;
        else if(dungeonInfo->type == LFG_TYPE_RANDOM && _player->HasAura(LFG_RANDOM_COOLDOWN))
            error = LFG_JOIN_RANDOM_COOLDOWN;
        //Now the group
        else if(Group *group = _player->GetGroup())
        {
            if(group->isRaidGroup())
                error = LFG_JOIN_MIXED_RAID_DUNGEON;
            else
            {
                for (GroupReference *itr = group->GetFirstMember(); itr != NULL && error == LFG_JOIN_OK; itr = itr->next())
                {
                    if (Player *plr = itr->getSource())
                    {
                        if (plr->HasAura(LFG_DESERTER))
                            error = LFG_JOIN_PARTY_DESERTER;
                        else if(dungeonInfo->type == LFG_TYPE_RANDOM && plr->HasAura(LFG_RANDOM_COOLDOWN))
                            error = LFG_JOIN_PARTY_RANDOM_COOLDOWN;
                    }else
                        error = LFG_JOIN_DISCONNECTED;
                }
            }
        }
        if(error != LFG_JOIN_OK)
        {
            WorldPacket data(SMSG_LFG_JOIN_RESULT, 8);
            data << uint32(error);                                  // Check Result
            data << uint32(0);                                      // Check Value
            SendPacket(&data);
            return;
        }
        //Already queued for this dungeon, dunno how this can happen, but it happens
        if(_player->m_lookingForGroup.queuedDungeons.find(dungeonInfo) != _player->m_lookingForGroup.queuedDungeons.end())
        {
            error_log("Already queued to %u, continue", dungeonInfo->ID);
            continue;
        }
        _player->m_lookingForGroup.queuedDungeons.insert(dungeonInfo);
        error_log("JOIN TO %u", dungeonInfo->ID);
    } 
    recv_data >> unk; // looks like unk from LFGDungeons.dbc, so 0 = raid or zone, 3 = dungeon, 15 = world event. Possibly count of next data? anyway seems unused
    for (int8 i = 0 ; i < unk; ++i)
        recv_data.read_skip<uint8>();                       // unk, always 0?

    recv_data >> comment;

    _player->m_lookingForGroup.roles = uint8(roles);
    _player->m_lookingForGroup.comment = comment;
    _player->m_lookingForGroup.joinTime = getMSTime();

    sLfgMgr.AddToQueue(_player);
}

void WorldSession::HandleLfgLeaveOpcode(WorldPacket & /*recv_data*/)
{
    DEBUG_LOG("WORLD: Received CMSG_LFG_LEAVE");

    if(Group *group = _player->GetGroup())
        if(group->GetLeaderGUID() != _player->GetGUID())    // Only leader can leave
            return;

    sLfgMgr.RemoveFromQueue(_player);
    _player->m_lookingForGroup.roles = 0;
}

void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket &/*recv_data*/)
{
    DEBUG_LOG("WORLD: Received CMSG_LFD_PLAYER_LOCK_INFO_REQUEST");
    sLfgMgr.SendLfgPlayerInfo(_player);
}

void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket &/*recv_data*/)
{
    // TODO: Find out why its sometimes send even when player is not in group...
    DEBUG_LOG("WORLD: Received CMSG_LFD_PARTY_LOCK_INFO_REQUEST");
    if(!_player->GetGroup())
    {
        DEBUG_LOG("Recieved CMSG_LFD_PARTY_LOCK_INFO_REQUEST but player %u is not in Group!", _player->GetGUID()); 
        return;
    }
    ((LfgGroup*)_player->GetGroup())->SendLfgPartyInfo(_player);
}

void WorldSession::HandleLfgProposalResult(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_LFG_PROPOSAL_RESULT");
    uint32 groupid;
    uint8 accept;

    recv_data >> groupid;
    recv_data >> accept; 
    
    if(LfgGroup *group = (LfgGroup*)sObjectMgr.GetGroupById(groupid))
    {
        group->GetProposalAnswers()->insert(std::pair<uint64, uint8>(_player->GetGUID(), accept));       
        group->SendProposalUpdate(LFG_PROPOSAL_WAITING);
    }
    error_log("Odpoved %u", accept);
}

void WorldSession::HandleLfgTeleport(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_LFG_TELEPORT");
    uint8 teleportOut;
    recv_data >> teleportOut;

    if(teleportOut == 0)
        return;

    if(Group *group = _player->GetGroup())
    {
        if(group->isLfgGroup())
        {
            group->RemoveMember(_player->GetGUID(), 0);
            return;
        }
    }

    //This should not happen
    if (!_player->isAlive())
    {
        _player->ResurrectPlayer(1.0f);
        _player->SpawnCorpseBones();
    }
    WorldLocation teleLoc = _player->m_lookingForGroup.joinLoc;
    if(teleLoc.coord_x != 0 && teleLoc.coord_y != 0 && teleLoc.coord_z != 0)
    {
        _player->ScheduleDelayedOperation(DELAYED_LFG_MOUNT_RESTORE);
        _player->ScheduleDelayedOperation(DELAYED_LFG_TAXI_RESTORE);
        _player->RemoveAurasDueToSpell(LFG_BOOST);
        _player->TeleportTo(teleLoc);
    }
}

void WorldSession::HandleSetLfgCommentOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_SET_LFG_COMMENT");
    std::string comment;
    recv_data >> comment;
    _player->m_lookingForGroup.comment = comment;
}

void WorldSession::HandleLfgSetRoles(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received CMSG_LFG_SET_ROLES");
    uint8 roles;
    recv_data >> roles;
    _player->m_lookingForGroup.roles = roles;

    if(Group *group = _player->GetGroup())
    {
        if(group->isLfgGroup())
        {
            ((LfgGroup*)group)->GetRoleAnswers()->insert(std::make_pair<uint64, uint8>(_player->GetGUID(), roles));                
            ((LfgGroup*)group)->UpdateRoleCheck();
        }
    } 
}

