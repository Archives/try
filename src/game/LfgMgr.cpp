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

    size_t packet_size = 2 + (extrainfo ? 1 : 0) * (5 + !dungeonEntry ? 4 : GetPlayer()->m_lookingForGroup.applyDungeons.size() * 4 + GetPlayer()->m_lookingForGroup.comment.length());
    WorldPacket data(SMSG_LFG_UPDATE_PARTY, packet_size);
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
    uint32 rsize = 0;        // size of random dungeons locks
    uint32 lsize = 0;        // size of normal dungeons locks

    WorldPacket data(SMSG_LFG_PLAYER_INFO, 1 + rsize * (4 + 1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4) + 4 + lsize * (4 + 4));
    if (rsize == 0)
    {
        data << uint8(0);
    }
    else
    {
        data << uint8(rsize);                  // Random Dungeon count
        //for (/* all random dungeons of player */)
        {
            data << uint32(0);                            // Entry of random dungeon
            BuildRewardBlock(data, 0, plr);               // 0 is entry of dungeon
        }
    }
    //for (/* every non-random lock OF THAT PLAYER!! */ )
    {
        data << uint32(0);                     // Dungeon entry + type
        data << uint32(0);                     // Lock status
    }
    plr->GetSession()->SendPacket(&data);
}

void LfgMgr::BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr)
{
    bool hasCompletedToday = false;
    LfgReward *reward = GetRandomDungeonReward(dungeon, hasCompletedToday, plr->getLevel());

    if (!reward)
        return;

    data << uint8(hasCompletedToday);
    if (data.GetOpcode() == SMSG_LFG_PLAYER_REWARD)
        data << uint32(reward->strangers);
    data << uint32(reward->baseMoney);
    data << uint32(reward->baseXP);
    data << uint32(reward->variableMoney);
    data << uint32(reward->variableXP);
    data << uint8(reward->itemId != 0);
    if (reward->itemId)
    {
        data << uint32(reward->itemId);
        data << uint32(reward->displayId);
        data << uint32(reward->stackCount);
    }
}

LfgReward* LfgMgr::GetRandomDungeonReward(uint32 dungeon, bool firstToday, uint8 level)
{
    uint8 index = 0;
    switch((dungeon & 0x00FFFFFF))                          // Get dungeon id from dungeon entry
    {
        case LFG_RANDOM_CLASSIC:
            if (level < 15)
                index = LFG_REWARD_LEVEL0;
            else if (level < 24)
                index = LFG_REWARD_LEVEL1;
            else if (level < 35)
                index = LFG_REWARD_LEVEL2;
            else if (level < 46)
                index = LFG_REWARD_LEVEL3;
            else if (level < 56)
                index = LFG_REWARD_LEVEL4;
            else
                index = LFG_REWARD_LEVEL5;
            break;
        case LFG_RANDOM_BC_NORMAL:
            index = LFG_REWARD_BC_NORMAL;
            break;
        case LFG_RANDOM_BC_HEROIC:
            index = LFG_REWARD_BC_HEROIC;
            break;
        case LFG_RANDOM_LK_NORMAL:
            index = level == 80 ? LFG_REWARD_LK_NORMAL80 : LFG_REWARD_LK_NORMAL;
            break;
        case LFG_RANDOM_LK_HEROIC:
            index = LFG_REWARD_LK_HEROIC;
            break;
        default:                                                // This should never happen!
            firstToday = false;
            index = LFG_REWARD_LEVEL0;
            sLog.outError("LFGMgr::GetRandomDungeonReward: Dungeon %u is not random dungeon!", dungeon);
            break;
    }

    for(LfgRewardList::iterator itr = m_rewardsList.begin(); itr != m_rewardsList.end(); , ++itr)
    {
        if((*itr)->type == index && (*itr)->daily == firstToday)
            return *itr;
    }
    return NULL;
}

/*
CREATE TABLE `mangostest`.`dungeon_rewards` (
`type` TINYINT( 3 ) UNSIGNED NOT NULL ,
`daily` TINYINT( 1 ) UNSIGNED NOT NULL DEFAULT '0',
`baseMoney` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`baseXP` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`variableMoney` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`variableXP` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`itemId` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`displayId` INT( 10 ) UNSIGNED NOT NULL DEFAULT '0',
`stackCount` TINYINT( 3 ) UNSIGNED NOT NULL DEFAULT '0',
`description` VARCHAR( 255 ) NOT NULL ,
INDEX ( `type` ) 
) ENGINE = InnoDB;
*/

void LfgMgr::LoadDungeonRewards()
{
    // In case of reload
    m_rewardsList.clear();

    uint32 count = 0;
    //                                                0     1      2          3       4              5           6       7          8        
    QueryResult *result = WorldDatabase.Query("SELECT type, daily, baseMoney, baseXP, variableMoney, variableXP, itemId, displayId, stackCount FROM dungeon_rewards");

    if( !result )
    {
        barGoLink bar( 1 );

        bar.step();

        sLog.outString();
        sLog.outString( ">> Loaded %u random dungeon rewards", count );
        return;
    }

    barGoLink bar( (int)result->GetRowCount() );

    do
    {
        Field *fields = result->Fetch();

        bar.step();

        LfgReward *reward = new LfgReward();
        reward->type                  = fields[0].GetUInt8();
        reward->daily                 = fields[1].GetBool();
        reward->baseMoney             = fields[2].GetInt32();
        reward->baseXP                = fields[3].GetInt32();
        reward->variableMoney         = fields[4].GetInt32();
        reward->variableXP            = fields[5].GetInt32();
        reward->itemId                = fields[6].GetInt32();
        reward->displayId             = fields[7].GetInt32();
        reward->stackCount            = fields[8].GetInt32();
        if(reward->type >= LFG_REWARD_DATA_SIZE)
        {
            sLog.outErrorDb("Entry listed in 'dungeon_rewards' has wrong type %u, skipping.", reward->type);
            delete reward;
            continue;
        }
        if(!sItemStore.LookupEntry(reward->itemId))
        {
            sLog.outErrorDb("Entry listed in 'dungeon_rewards' has wrong item entry %u, skipping.", reward->itemId);
            delete reward;
            continue;
        }

        m_rewardsList.push_back(reward);
    } while( result->NextRow() );

    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u random dungeon rewards.", count );
}