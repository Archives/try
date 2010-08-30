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

#ifndef __LFGGROUP_H
#define __LFGGROUP_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "Utilities/EventProcessor.h"
#include "DBCEnums.h"
#include "Group.h"
#include "LfgMgr.h"
#include "ace/Recursive_Thread_Mutex.h"

#define LFG_VOTES_NEEDED 3

typedef std::map<uint64, uint8> ProposalAnswersMap; // Guid and accept

struct VoteToKick
{
    VoteToKick() { Reset(); }

    void Reset()
    {
        isInProggres = false;
        votes.clear();
        victim = 0;
        beginTime = 0;
        reason = "";
    }

    bool PlayerVoted(uint64 guid) { return votes.find(guid) != votes.end(); }
    uint8 GetVote(uint64 guid) 
    {
        ProposalAnswersMap::iterator itr = votes.find(guid);
        if (itr != votes.end())
            return itr->second;
        else
            return 0;
    }
    uint32 GetVotesNum(bool agreeOnly)
    {
        if (!agreeOnly)
            return votes.size();
        else
        {
            uint32 votesCount = 0;
            for(ProposalAnswersMap::iterator itr = votes.begin(); itr != votes.end(); ++itr)
                if (itr->second)
                    ++votesCount;
            return votesCount;
        }
    }
    int32 GetTimeLeft() { return 60-(getMSTimeDiff(beginTime, getMSTime())/1000); }

    bool isInProggres;
    ProposalAnswersMap votes;
    uint64 victim;
    uint32 beginTime;
    std::string reason;
};

class MANGOS_DLL_SPEC LfgGroup : public Group
{
    public:
        LfgGroup(bool premade = false);
        ~LfgGroup();

        void SetGroupId(uint32 newid) { m_Id = newid; }
        uint32 GetKilledBosses() { return m_killedBosses; }
        bool LoadGroupFromDB(Field *fields);

        void SendLfgPartyInfo(Player *plr);
        void SendLfgQueueStatus();
        void SendGroupFormed();
        void SendProposalUpdate(uint8 state);
        void SendRoleCheckUpdate(uint8 state);
        LfgLocksMap *GetLocksList() const;
        
        //Override these methods
        bool AddMember(const uint64 &guid, const char* name);
        uint32 RemoveMember(const uint64 &guid, const uint8 &method);
        void SendUpdate();

        uint64 GetTank() const { return m_tank; };
        uint64 GetHeal() const { return m_heal; };
        PlayerList *GetDps() { return &dps; };
        ProposalAnswersMap *GetProposalAnswers() { return &m_answers; }
        ProposalAnswersMap *GetRoleAnswers() { return &m_rolesProposal; }
        void UpdateRoleCheck(uint32 diff = 0);
        PlayerList *GetPremadePlayers() { return &premadePlayers; }

        void SetTank(uint64 tank) { m_tank = tank; }
        void SetHeal(uint64 heal) { m_heal = heal; }
        void SetLeader(uint64 guid) { _setLeader(guid); }

        void SetDungeonInfo(LFGDungeonEntry const *dungeonInfo) { m_dungeonInfo = dungeonInfo; }
        LFGDungeonEntry const *GetDungeonInfo() { return m_dungeonInfo; }
        uint32 GetRandomEntry() const { return randomDungeonEntry; }

        bool RemoveOfflinePlayers();
        bool UpdateCheckTimer(uint32 time);
        void TeleportToDungeon();
        void TeleportPlayer(Player *plr, DungeonInfo *dungeonInfo, uint32 originalDungeonId = 0);
        bool HasCorrectLevel(uint8 level);
        bool IsInDungeon() const { return m_inDungeon; }
        void SetInstanceStatus(uint8 status) { m_instanceStatus = status; }
        uint8 GetInstanceStatus() const { return m_instanceStatus; }
        bool IsRandom() const { return m_isRandom; }
        uint8 GetPlayerRole(uint64 guid, bool withLeader = true, bool joinedAs = false) const;
        void KilledCreature(Creature *creature);
        void ResetGroup();
        void InitVoteKick(uint64 who, Player *initiator, std::string reason);
        void SendBootPlayer(Player *plr);
        VoteToKick *GetVoteToKick() { return &m_voteToKick; }
        bool UpdateVoteToKick(uint32 diff = 0);
        
    private:
        //ACE_Thread_Mutex m_queueLock;

        uint64 m_tank;
        uint64 m_heal;
        PlayerList dps;
        LFGDungeonEntry const *m_dungeonInfo;
        PlayerList premadePlayers;
        ProposalAnswersMap m_answers;
        ProposalAnswersMap m_rolesProposal;
        uint8 m_membersBeforeRoleCheck;

        uint32 m_killedBosses;
        int32 m_readycheckTimer;
        int32 m_voteKickTimer;
        uint8 m_baseLevel;
        uint8 m_instanceStatus;
        bool m_inDungeon;
        bool m_isRandom;
        uint32 randomDungeonEntry;
        VoteToKick m_voteToKick;
};

#endif