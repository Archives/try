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

#ifndef __LFGMGR_H
#define __LFGMGR_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "Utilities/EventProcessor.h"
#include "DBCEnums.h"
#include "Group.h"
#include "ace/Recursive_Thread_Mutex.h"

enum LfgRoles
{
    NONE   = 0x00,
    LEADER = 0x01,
    TANK   = 0x02,
    HEALER = 0x04,
    DAMAGE = 0x08,
};

enum LfgUpdateType
{
    LFG_UPDATETYPE_LEADER               = 1,
    LFG_UPDATETYPE_ROLECHECK_ABORTED    = 4,
    LFG_UPDATETYPE_JOIN_PROPOSAL        = 5,
    LFG_UPDATETYPE_ROLECHECK_FAILED     = 6,
    LFG_UPDATETYPE_REMOVED_FROM_QUEUE   = 7,
    LFG_UPDATETYPE_PROPOSAL_FAILED      = 8,
    LFG_UPDATETYPE_PROPOSAL_DECLINED    = 9,
    LFG_UPDATETYPE_GROUP_FOUND          = 10,
    LFG_UPDATETYPE_ADDED_TO_QUEUE       = 12,
    LFG_UPDATETYPE_PROPOSAL_FOUND       = 13,
    LFG_UPDATETYPE_CLEAR_LOCK_LIST      = 14,
    LFG_UPDATETYPE_GROUP_MEMBER_OFFLINE = 15,
    LFG_UPDATETYPE_GROUP_DISBAND        = 16,
};

enum LfgType
{
    LFG_TYPE_DUNGEON = 1,
    LFG_TYPE_RAID    = 2,
    LFG_TYPE_QUEST   = 3,
    LFG_TYPE_ZONE    = 4,
    LFG_TYPE_HEROIC  = 5,
    LFG_TYPE_RANDOM  = 6,
};

enum LfgLockStatusType
{
    LFG_LOCKSTATUS_OK                        = 0,           // Internal use only
    LFG_LOCKSTATUS_INSUFFICIENT_EXPANSION    = 1,
    LFG_LOCKSTATUS_TOO_LOW_LEVEL             = 2,
    LFG_LOCKSTATUS_TOO_HIGH_LEVEL            = 3,
    LFG_LOCKSTATUS_TOO_LOW_GEAR_SCORE        = 4,
    LFG_LOCKSTATUS_TOO_HIGH_GEAR_SCORE       = 5,
    LFG_LOCKSTATUS_RAID_LOCKED               = 6,
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_LOW_LEVEL  = 1001,
    LFG_LOCKSTATUS_ATTUNEMENT_TOO_HIGH_LEVEL = 1002,
    LFG_LOCKSTATUS_QUEST_NOT_COMPLETED       = 1022,
    LFG_LOCKSTATUS_MISSING_ITEM              = 1025,
    LFG_LOCKSTATUS_NOT_IN_SEASON             = 1031,
};

enum LfgRandomDungeonEntries
{
    LFG_ALL_DUNGEONS       = 0,
    LFG_RANDOM_CLASSIC     = 258,
    LFG_RANDOM_BC_NORMAL   = 259,
    LFG_RANDOM_BC_HEROIC   = 260,
    LFG_RANDOM_LK_NORMAL   = 261,
    LFG_RANDOM_LK_HEROIC   = 262,
};

enum LfgRewardEnums
{
    LFG_REWARD_LEVEL0      = 10,
    LFG_REWARD_LEVEL1      = 0,
    LFG_REWARD_LEVEL2      = 1,
    LFG_REWARD_LEVEL3      = 2,
    LFG_REWARD_LEVEL4      = 3,
    LFG_REWARD_LEVEL5      = 4,
    LFG_REWARD_BC_NORMAL   = 5,
    LFG_REWARD_BC_HEROIC   = 6,
    LFG_REWARD_LK_NORMAL   = 7,
    LFG_REWARD_LK_NORMAL80 = 7,
    LFG_REWARD_LK_HEROIC   = 8,
    LFG_REWARD_DATA_SIZE   = 10,
};

struct LfgReward
{
    uint8 type;                 // reward type from LfgRewardEnums
    bool firstToday;
    uint32 baseMoney;
    uint32 baseXP;
    uint32 variableMoney;
    uint32 variableXP;
    uint32 itemId;
    uint32 displayId;
    uint32 stackCount;
};

class MANGOS_DLL_SPEC LfgGroup : public Group
{
    public:
        LfgGroup();
        ~LfgGroup();

        void SendLfgUpdateParty(uint8 updateType, uint32 dungeonEntry  = 0);  
}
class LfgMgr
{
    public:
        typedef std::list<LfgReward*> LfgRewardList;
        /* Construction */
        LfgMgr();
        ~LfgMgr();
        void Update(uint32 diff);

        void SendLfgPlayerInfo(Player *plr);
        void SendLfgPartyInfo(Player *plr);

        void BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr);

    private:
        LfgRewardList m_rewardsList;
        LfgReward *GetRandomDungeonReward(uint32 dungeon, bool firstToday, uint8 level);
};

#define sLfgMgr MaNGOS::Singleton<LfgMgr>::Instance()
#endif
