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

#include "Player.h"
#include "BattleGround.h"
#include "BattleGroundDS.h"
#include "Language.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"

BattleGroundDS::BattleGroundDS()
{

    m_StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_1M;
    m_StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_30S;
    m_StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_15S;
    m_StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE;
    //we must set messageIds
    m_StartMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_ARENA_ONE_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_ARENA_THIRTY_SECONDS;
    m_StartMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_ARENA_FIFTEEN_SECONDS;
    m_StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_ARENA_HAS_BEGUN;
}

BattleGroundDS::~BattleGroundDS()
{

}

void BattleGroundDS::Update(uint32 diff)
{
    BattleGround::Update(diff);
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        if (m_uiKnock < diff && !Knocked)
            KnockOutOfTubes();
        else m_uiKnock -= diff;

        // Waterfall
        if (m_uiWaterfall < diff)
        {
            if (WaterfallActivated)
            {
                DespawnEvent(WATERFALL_EVENT, 0);
                WaterfallActivated = false;
            }
            else
                WaterfallSpawn();
            m_uiWaterfall = urand(30,45)*IN_MILLISECONDS;

        }else m_uiWaterfall -= diff;

    }
}

void BattleGroundDS::StartingEventCloseDoors()
{
}

void BattleGroundDS::StartingEventOpenDoors()
{
    OpenDoorEvent(BG_EVENT_DOOR);
    for(BattleGroundPlayerMap::const_iterator itr = m_Players.begin(); itr != m_Players.end(); ++itr)
        m_lPlrInTube.push_back(itr->first);
}

void BattleGroundDS::KnockOutOfTubes()
{
    DespawnEvent(DOORS_EVENT, 0);
    for(std::list<uint64>::const_iterator iter = m_lPlrInTube.begin(); iter != m_lPlrInTube.end(); ++iter)
    {
//    for(BattleGroundPlayerMap::const_iterator itr = m_Players.begin(); itr != m_Players.end(); ++itr)
//    {
        Player * plr = sObjectMgr.GetPlayer(*iter);
        if (plr->GetTeam() == ALLIANCE && plr->GetDistance2d(1214, 765) <= 50 && plr->GetPositionZ() > 10)
            plr->KnockWithAngle(6.05f, 35.0f, 7.0f);
        if (plr->GetTeam() == HORDE && plr->GetDistance2d(1369, 817) <= 50 && plr->GetPositionZ() > 10)
            plr->KnockWithAngle(3.03f, 35.0f, 7.0f);

        // Remove Demonic Circle
        if (plr->getClass() == CLASS_WARLOCK)
            if (GameObject* obj = plr->GetGameObject(48018))
                obj->Delete();
    }
    if (m_lPlrInTube.empty())
        Knocked = true;
}
void BattleGroundDS::WaterfallSpawn()
{
    SpawnEvent(WATERFALL_EVENT, 0, true);
    for(BattleGroundPlayerMap::const_iterator itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
    {
        Player * plr = sObjectMgr.GetPlayer(itr->first);
        float x, y, angle;
        x = 1291.02f;
        y = 790.42f;
        angle = plr->GetAngle(x,y)+M_PI_F;
        if (plr->GetDistance2d(x, y) <= 5)
            plr->KnockWithAngle(angle, 35.0f, 7.0f);
    }
    WaterfallActivated = true;
}
void BattleGroundDS::DespawnEvent(uint8 event1, uint8 event2)
{
    BGObjects::const_iterator itr2 = m_EventObjects[MAKE_PAIR32(event1, event2)].gameobjects.begin();
    for(; itr2 != m_EventObjects[MAKE_PAIR32(event1, event2)].gameobjects.end(); ++itr2)
        DespawnBGObject(*itr2);
}

void BattleGroundDS::DespawnBGObject(uint64 const& guid)
{
    Map* map = GetBgMap();

    GameObject *obj = map->GetGameObject(guid);
    if (obj)
        obj->Delete();
}

void BattleGroundDS::AddPlayer(Player *plr)
{
    BattleGround::AddPlayer(plr);
    //create score and add it to map, default values are set in constructor
    BattleGroundDSScore* sc = new BattleGroundDSScore;

    m_PlayerScores[plr->GetGUID()] = sc;

    UpdateArenaWorldState();
}

void BattleGroundDS::RemovePlayer(Player * /*plr*/, uint64 /*guid*/)
{
    if (GetStatus() == STATUS_WAIT_LEAVE)
        return;

    UpdateArenaWorldState();
    CheckArenaWinConditions();
}

void BattleGroundDS::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    if (!killer)
    {
        sLog.outError("BattleGroundDS: Killer player not found");
        return;
    }

    BattleGround::HandleKillPlayer(player,killer);

    UpdateArenaWorldState();
    CheckArenaWinConditions();
}

void BattleGroundDS::HandleAreaTrigger(Player *Source, uint32 Trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch(Trigger)
    {
        case 5347:
        case 5348:
            m_lPlrInTube.remove(Source->GetGUID());
        default:
            sLog.outError("WARNING: Unhandled AreaTrigger in Battleground: %u", Trigger);
            Source->GetSession()->SendAreaTriggerMessage("Warning: Unhandled AreaTrigger in Battleground: %u", Trigger);
            break;
    }
}
bool BattleGroundDS::HandlePlayerUnderMap(Player *player)
{
    player->TeleportTo(GetMapId(), 1299.046f, 784.825f, 9.338f, 2.422f, false);
    return true;
}

void BattleGroundDS::FillInitialWorldStates(WorldPacket &data, uint32& count)
{
    FillInitialWorldState(data, count, 0xe1a, 1);
    UpdateArenaWorldState();
}
void BattleGroundDS::Reset()
{
    //call parent's class reset
    BattleGround::Reset();
    m_lPlrInTube.clear();
    m_uiKnock = urand(10,15)*IN_MILLISECONDS;
    Knocked = true;
    WaterfallActivated = false;
}

bool BattleGroundDS::SetupBattleGround()
{
    return true;
}
