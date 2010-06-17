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

enum LfgRolesNumber
{
    LFG_TANKS_COUNT                     = 1,
    LFG_HEALS_COUNT                     = 1,
    LFG_DPS_COUNT                       = 3,
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

enum LfgJoinResult
{
    LFG_JOIN_OK                    = 0,                     // Joined (no client msg)
    LFG_JOIN_FAILED                = 1,                     // RoleCheck Failed
    LFG_JOIN_GROUPFULL             = 2,                     // Your group is full
    LFG_JOIN_UNK3                  = 3,                     // No client reaction
    LFG_JOIN_INTERNAL_ERROR        = 4,                     // Internal LFG Error
    LFG_JOIN_NOT_MEET_REQS         = 5,                     // You do not meet the requirements for the chosen dungeons
    LFG_JOIN_PARTY_NOT_MEET_REQS   = 6,                     // One or more party members do not meet the requirements for the chosen dungeons
    LFG_JOIN_MIXED_RAID_DUNGEON    = 7,                     // You cannot mix dungeons, raids, and random when picking dungeons
    LFG_JOIN_MULTI_REALM           = 8,                     // The dungeon you chose does not support players from multiple realms
    LFG_JOIN_DISCONNECTED          = 9,                     // One or more party members are pending invites or disconnected
    LFG_JOIN_PARTY_INFO_FAILED     = 10,                    // Could not retrieve information about some party members
    LFG_JOIN_DUNGEON_INVALID       = 11,                    // One or more dungeons was not valid
    LFG_JOIN_DESERTER              = 12,                    // You can not queue for dungeons until your deserter debuff wears off
    LFG_JOIN_PARTY_DESERTER        = 13,                    // One or more party members has a deserter debuff
    LFG_JOIN_RANDOM_COOLDOWN       = 14,                    // You can not queue for random dungeons while on random dungeon cooldown
    LFG_JOIN_PARTY_RANDOM_COOLDOWN = 15,                    // One or more party members are on random dungeon cooldown
    LFG_JOIN_TOO_MUCH_MEMBERS      = 16,                    // You can not enter dungeons with more that 5 party members
    LFG_JOIN_USING_BG_SYSTEM       = 17,                    // You can not use the dungeon system while in BG or arenas
    LFG_JOIN_FAILED2               = 18,                    // RoleCheck Failed
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
//Spell IDs
#define LFG_DESERTER                           71041
#define LFG_RANDOM_COOLDOWN                    71328

enum LfgRandomDungeonEntries
{
    LFG_ALL_DUNGEONS       = 0,
    LFG_RANDOM_CLASSIC     = 258,
    LFG_RANDOM_BC_NORMAL   = 259,
    LFG_RANDOM_BC_HEROIC   = 260,
    LFG_RANDOM_LK_NORMAL   = 261,
    LFG_RANDOM_LK_HEROIC   = 262,
};

enum LfgType
{
    LFG_TYPE_DUNGEON = 1,                // world event count as dungeon
    LFG_TYPE_RAID    = 2,
    LFG_TYPE_QUEST   = 3,                // unused
    LFG_TYPE_ZONE    = 4,                // unused
    LFG_TYPE_HEROIC  = 5,
    LFG_TYPE_RANDOM  = 6,
};

// From LFGDungeonGroup.dbc
enum LfgGroupType
{
    LFG_GROUPTYPE_CLASSIC      = 1,
    LFG_GROUPTYPE_BC_NORMAL    = 2,
    LFG_GROUPTYPE_BC_HEROIC    = 3,
    LFG_GROUPTYPE_WTLK_NORMAL  = 4,
    LFG_GROUPTYPE_WTLK_HEROIC  = 5,
    LFG_GROUPTYPE_CLASSIC_RAID = 6,
    LFG_GROUPTYPE_BC_RAID      = 7,
    LFG_GROUPTYPE_WTLK_RAID_10 = 8,
    LFG_GROUPTYPE_WTLK_RAID_25 = 9,
    LFG_GROUPTYPE_WORLD_EVENT  = 11,
};

#define MAX_LFG_GROUPTYPE        12

//Theres some quest for rewards, but they have not anything different, so make custom flags
enum __LfgQuestFlags
{
    LFG_QUEST_NONE         = 0x00000000,
    LFG_QUEST_DAILY        = 0x00000001,    // Its only daily quest
};

struct LfgReward
{
    uint8 type;                      // enum LfgType
    uint8 GroupType;                 // reward type from LfgGroupType
    Quest *questInfo;                // rewards are quests
    uint32 flags;                    //__LfgQuestFlags

    bool isDaily() const { return (flags & LFG_QUEST_DAILY); }
};
typedef std::list<LfgReward*> LfgRewardList;
typedef std::set<LFGDungeonEntry* const> LfgDungeonList;

struct LfgLockStatus
{
   LFGDungeonEntry* const dungeonInfo;
   LfgLockStatusType lockType;
};
typedef std::list<LfgLockStatus*> LfgLocksList;
typedef std::map<uint64, LfgLocksList*> LfgLocksMap;

enum QueueFaction
{
    LFG_ALLIANCE                       = 0,
    LFG_HORDE                          = 1,
};
#define MAX_LFG_FACTION                  2

typedef std::set<Player*> PlayerList;
typedef std::set<LfgGroup*> GroupsList;

struct QueuedDungeonInfo
{
    LFGDungeonEntry* const dungeonInfo;

    PlayerList PlayerList;
    GroupsList GroupsList;  
};

typedef std::map<uint32, QueuedDungeonInfo*> QueuedDungeonsMap;

//Used not only inside dungeons, but also as queued group
class MANGOS_DLL_SPEC LfgGroup : public Group
{
    public:
        LfgGroup();
        ~LfgGroup();

        void SendLfgUpdateParty(uint8 updateType, uint32 dungeonEntry  = 0);
        void SendLfgPartyInfo(Player *plr);
        void SendLfgQueueStatus();
        LfgLocksMap *GetLocksList();
        
        //Override these methods
        bool AddMember(const uint64 &guid, const char* name);
        uint32 RemoveMember(const uint64 &guid, const uint8 &method);

        Player *GetTank() const { return m_tank; };
        Player *GetHeal() const { return m_heal; };
        PlayerList *GetDps() { return dps; };

        void SetTank(Player *tank) { m_tank = tank; }
        void SetHeal(Player *heal) { m_heal = heal; }

        void SetDungeonInfo(LFGDungeonEntry* const dungeonInfo) { m_dungeonInfo = dungeonInfo; }

        std::set<uint64> GetPremadePlayers() { return premadePlayers; }
    private:
        Player *m_tank;
        Player *m_heal;
        PlayerList *dps;
        LFGDungeonEntry* const m_dungeonInfo;
        std::set<uint64> premadePlayers;

};

class MANGOS_DLL_SPEC LfgMgr
{
    public:
        /* Construction */
        LfgMgr();
        ~LfgMgr();

        void Update(uint32 diff);

        void AddToQueue(Player *player, uint32 LfgGroupType);
        void RemoveFromQueue(Player *player);
        void UpdateQueues();

        void SendLfgPlayerInfo(Player *plr);
        void SendLfgUpdatePlayer(Player *plr, uint8 updateType);
        void BuildRewardBlock(WorldPacket &data, uint32 dungeon, Player *plr);

        void LoadDungeonRewards();
        LfgLocksList *GetDungeonsLock(Player *plr);
    private:
        LfgRewardList m_rewardsList;
        LfgReward *GetDungeonReward(uint32 dungeon, bool done, uint8 level);
        LfgDungeonList *GetRandomDungeons(Player *plr);

        QueuedDungeonsMap m_queuedDungeons[MAX_LFG_FACTION];
};

#define sLfgMgr MaNGOS::Singleton<LfgMgr>::Instance()
#endif
