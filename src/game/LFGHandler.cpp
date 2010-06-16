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


void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket &/*recv_data*/)
{
    DEBUG_LOG("CMSG_LFD_PLAYER_LOCK_INFO_REQUEST");
    sLfgMgr.SendLfgPlayerInfo(_player);
}

void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket &/*recv_data*/)
{
    DEBUG_LOG("CMSG_LFD_PARTY_LOCK_INFO_REQUEST");
    if(!_player->m_lookingForGroup.group)
    {
        error_log("Recieved CMSG_LFD_PARTY_LOCK_INFO_REQUEST but player %u is not in LfgGroup!", _player->GetGUID()); 
        return;
    }
    _player->m_lookingForGroup.group->SendLfgPartyInfo(_player);
}
