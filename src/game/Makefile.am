# Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

## Process this file with automake to produce Makefile.in

## Sub-directories to parse

## CPP flags for includes, defines, etc.
AM_CPPFLAGS = $(MANGOS_INCLUDES) -I$(top_builddir)/src/shared -I$(srcdir) -I$(srcdir)/../../dep/tbb/include -I$(srcdir)/../../dep/include -I$(srcdir)/../framework -I$(srcdir)/../shared -I$(srcdir)/../shared/vmap -I$(srcdir)/../realmd -DSYSCONFDIR=\"$(sysconfdir)/\"
 
## Build MaNGOS game library as convenience library.
#  All libraries will be convenience libraries. Might be changed to shared
#  later.
noinst_LIBRARIES = libmangosgame.a

#  libmangossgame library will later be reused by ...
libmangosgame_a_SOURCES = \
	AccountMgr.cpp \
	AccountMgr.h \
	AchievementMgr.h \
	AchievementMgr.cpp \
	AggressorAI.cpp \
	AggressorAI.h \
	ArenaTeam.cpp \
	ArenaTeam.h \
	ArenaTeamHandler.cpp \
	AuctionHouseHandler.cpp \
	AuctionHouseMgr.cpp \
	AuctionHouseMgr.h \
	Bag.cpp \
	Bag.h \
	BattleGround.cpp \
	BattleGroundAA.cpp \
	BattleGroundAB.cpp \
	BattleGroundRB.cpp \
	BattleGroundAV.cpp \
	BattleGroundBE.cpp \
	BattleGroundDS.cpp \
	BattleGroundEY.cpp \
	BattleGroundIC.cpp \
	BattleGroundNA.cpp \
	BattleGroundRL.cpp \
	BattleGroundRV.cpp \
	BattleGroundSA.cpp \
	BattleGroundWS.cpp \
	BattleGround.h \
	BattleGroundAA.h \
	BattleGroundAB.h \
	BattleGroundRB.h \
	BattleGroundAV.h \
	BattleGroundBE.h \
	BattleGroundDS.h \
	BattleGroundEY.h \
	BattleGroundIC.h \
	BattleGroundNA.h \
	BattleGroundRL.h \
	BattleGroundRV.h \
	BattleGroundSA.h \
	BattleGroundWS.h \
	BattleGroundHandler.cpp \
	BattleGroundMgr.cpp \
	BattleGroundMgr.h \
	Calendar.cpp \
	Calendar.h \
	CalendarHandler.cpp \
	Camera.cpp \
	Camera.h \
	Cell.h \
	CellImpl.h \
	Channel.cpp \
	Channel.h \
	ChannelHandler.cpp \
	ChannelMgr.cpp \
	ChannelMgr.h \
	CharacterDatabaseCleaner.cpp \
	CharacterDatabaseCleaner.h \
	CharacterHandler.cpp \
	Chat.cpp \
	Chat.h \
	ChatHandler.cpp \
	CombatHandler.cpp \
	ConfusedMovementGenerator.cpp \
	ConfusedMovementGenerator.h \
	Corpse.cpp \
	Corpse.h \
	CreatureAI.cpp \
	CreatureAI.h \
	CreatureAIImpl.h \
	CreatureAIRegistry.cpp \
	CreatureAIRegistry.h \
	CreatureAISelector.cpp \
	CreatureAISelector.h \
	CreatureEventAI.cpp \
	CreatureEventAI.h \
	CreatureEventAIMgr.cpp \
	CreatureEventAIMgr.h \
	Creature.cpp \
	Creature.h \
	DBCEnums.h \
	DBCfmt.h \
	DBCStores.cpp \
	DBCStores.h \
	DBCStructure.h	\
	debugcmds.cpp \
	DestinationHolder.cpp \
	DestinationHolder.h \
	DestinationHolderImp.h \
	DuelHandler.cpp \
	DynamicObject.cpp \
	DynamicObject.h \
	FleeingMovementGenerator.cpp \
	FleeingMovementGenerator.h \
	Formulas.h \
	GameEventMgr.cpp \
	GameEventMgr.h \
	GameObject.cpp \
	GameObject.h \
	GMTicketHandler.cpp \
	GMTicketMgr.cpp \
	GMTicketMgr.h \
	GossipDef.cpp \
	GossipDef.h \
	GridDefines.h \
	GridMap.cpp \
	GridMap.h \
	GridNotifiers.cpp \
	GridNotifiers.h \
	GridNotifiersImpl.h \
	GridStates.cpp \
	GridStates.h \
	Group.cpp \
	Group.h \
	GroupHandler.cpp \
	GuardAI.cpp \
	GuardAI.h \
	Guild.cpp \
	Guild.h \
	GuildHandler.cpp \
	HomeMovementGenerator.cpp \
	HomeMovementGenerator.h \
	HostileRefManager.cpp \
	HostileRefManager.h \
	IdleMovementGenerator.cpp \
	IdleMovementGenerator.h \
	InstanceData.cpp \
	InstanceData.h \
	InstanceSaveMgr.cpp \
	InstanceSaveMgr.h \
	Item.cpp \
	Item.h \
	ItemEnchantmentMgr.cpp \
	ItemEnchantmentMgr.h \
	ItemHandler.cpp \
	ItemPrototype.h \
	Language.h \
	Level0.cpp \
	Level1.cpp \
	Level2.cpp \
	Level3.cpp \
	LfgGroup.cpp \
	LfgGroup.h \
	LFGHandler.cpp \
	LfgMgr.cpp \
	LfgMgr.h \
	LootHandler.cpp \
	LootMgr.cpp \
	LootMgr.h \
	Mail.cpp \
	Mail.h \
	Map.cpp \
	Map.h \
	MapInstanced.cpp \
	MapInstanced.h \
	MapManager.cpp \
	MapManager.h \
	MapReference.h \
	MapRefManager.h \
	MiscHandler.cpp \
	MotionMaster.cpp \
	MotionMaster.h \
	MovementGenerator.cpp \
	MovementGenerator.h \
	MovementGeneratorImpl.h \
	MovementHandler.cpp \
	NPCHandler.cpp \
	NPCHandler.h \
	NullCreatureAI.cpp \
	NullCreatureAI.h \
	ObjectAccessor.cpp \
	ObjectAccessor.h \
	Object.cpp \
	ObjectGuid.cpp \
	ObjectGuid.h \
	ObjectGridLoader.cpp \
	ObjectGridLoader.h \
	Object.h \
	ObjectMgr.cpp \
	ObjectMgr.h \
	ObjectPosSelector.cpp \
	ObjectPosSelector.h \
	Opcodes.cpp \
	Opcodes.h \
	Path.h \
	PetAI.cpp \
	PetAI.h \
	Pet.cpp \
	Pet.h \
	PetHandler.cpp \
	PetitionsHandler.cpp \
	Player.cpp \
	Player.h \
	PlayerDump.cpp \
	PlayerDump.h \
	PointMovementGenerator.cpp \
	PointMovementGenerator.h \
	PoolManager.cpp \
	PoolManager.h \
	QueryHandler.cpp \
	QuestDef.cpp \
	QuestDef.h \
	QuestHandler.cpp \
	RandomMovementGenerator.cpp \
	RandomMovementGenerator.h \
	ReactorAI.cpp \
	ReactorAI.h \
	ReputationMgr.cpp \
	ReputationMgr.h \
	ScriptCalls.cpp \
	ScriptCalls.h \
	SharedDefines.h \
	SkillHandler.cpp \
	SpellAuraDefines.h \
	SpellAuras.cpp \
	SpellAuras.h \
	Spell.cpp \
	SpellDBCHacks.cpp \
	SpellEffects.cpp \
	Spell.h \
	SkillDiscovery.cpp \
	SkillDiscovery.h \
	SkillExtraItems.cpp \
	SkillExtraItems.h \
	SpellHandler.cpp \
	SocialMgr.cpp \
	SocialMgr.h \
	SpellMgr.cpp \
	SpellMgr.h \
	StatSystem.cpp \
	TargetedMovementGenerator.cpp \
	TargetedMovementGenerator.h \
	TaxiHandler.cpp \
	TemporarySummon.cpp \
	TemporarySummon.h \
	TotemAI.cpp \
	TotemAI.h \
	Totem.cpp \
	Totem.h \
	TradeHandler.cpp \
	Transports.cpp \
	Transports.h \
	ThreatManager.cpp \
	ThreatManager.h \
	Traveller.h \
	Unit.cpp \
	Unit.h \
	UnitEvents.h \
	UpdateData.cpp \
	UpdateData.h \
	UpdateFields.h \
	UpdateMask.h \
	Vehicle.cpp \
	Vehicle.h \
	VoiceChatHandler.cpp \
	WaypointManager.cpp \
	WaypointManager.h \
	WaypointMovementGenerator.cpp \
	WaypointMovementGenerator.h \
	Weather.cpp \
	Weather.h \
	World.cpp \
	World.h \
	WorldSession.cpp \
	WorldSession.h \
	WorldSocket.cpp \
	WorldSocket.h \
	WorldSocketMgr.cpp \
	WorldSocketMgr.h \
	FollowerReference.cpp \
	FollowerReference.h \
	FollowerRefManager.h \
	GroupReference.cpp \
	GroupReference.h \
	GroupRefManager.h

## Additional files to include when running 'make dist'
#  Precompiled Headers for WIN
EXTRA_DIST = \
	pchdef.cpp \
	pchdef.h
