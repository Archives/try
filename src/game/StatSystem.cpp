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

#include "Unit.h"
#include "Player.h"
#include "Pet.h"
#include "Creature.h"
#include "SharedDefines.h"
#include "SpellAuras.h"

/*#######################################
########                         ########
########   PLAYERS STAT SYSTEM   ########
########                         ########
#######################################*/

bool Player::UpdateStats(Stats stat)
{
    if (stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);

    SetStat(stat, int32(value));

    if (stat == STAT_STRENGTH || stat == STAT_STAMINA || stat == STAT_INTELLECT)
    {
        Pet *pet = GetPet();
        if (pet)
            pet->UpdateStats(stat);
    }

    switch(stat)
    {
        case STAT_STRENGTH:
            UpdateShieldBlockValue();
            break;
        case STAT_AGILITY:
            UpdateArmor();
            UpdateAllCritPercentages();
            UpdateDodgePercentage();
            break;
        case STAT_STAMINA:   UpdateMaxHealth(); break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            UpdateAllSpellCritChances();
            UpdateArmor();                                  //SPELL_AURA_MOD_RESISTANCE_OF_INTELLECT_PERCENT, only armor currently
            break;

        case STAT_SPIRIT:
            break;

        default:
            break;
    }
    // Need update (exist AP from stat auras)
    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);

    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();

    // Update ratings in exist SPELL_AURA_MOD_RATING_FROM_STAT and only depends from stat
    uint32 mask = 0;
    AuraList const& modRatingFromStat = GetAurasByType(SPELL_AURA_MOD_RATING_FROM_STAT);
    for(AuraList::const_iterator i = modRatingFromStat.begin();i != modRatingFromStat.end(); ++i)
        if (Stats((*i)->GetMiscBValue()) == stat)
            mask |= (*i)->GetMiscValue();
    if (mask)
    {
        for (uint32 rating = 0; rating < MAX_COMBAT_RATING; ++rating)
            if (mask & (1 << rating))
                ApplyRatingMod(CombatRating(rating), 0, true);
    }
    return true;
}

void Player::ApplySpellPowerBonus(int32 amount, bool apply)
{
    m_baseSpellPower+=apply?amount:-amount;

    // For speed just update for client
    ApplyModUInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, amount, apply);
    for(int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        ApplyModUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS+i, amount, apply);

    Pet *pet = GetPet();
    if (pet)
        pet->UpdateAttackPowerAndDamage();
}

void Player::UpdateSpellDamageAndHealingBonus()
{
    // Magic damage modifiers implemented in Unit::SpellDamageBonusDone
    // This information for client side use only
    // Get healing bonus for all schools
    SetStatInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL));
    // Get damage bonus for all schools
    for(int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        SetStatInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS+i, SpellBaseDamageBonusDone(SpellSchoolMask(1 << i)));

    Pet *pet = GetPet();
    if (pet)
        pet->UpdateAttackPowerAndDamage();
}

bool Player::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        float value = GetTotalStatValue(Stats(i));
        SetStat(Stats(i), (int32)value);
    }

    UpdateArmor();
    // calls UpdateAttackPowerAndDamage() in UpdateArmor for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
    UpdateAttackPowerAndDamage(true);
    UpdateMaxHealth();

    for(int i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    UpdateAllRatings();
    UpdateAllCritPercentages();
    UpdateAllSpellCritChances();
    UpdateDefenseBonusesMod();
    UpdateShieldBlockValue();
    UpdateArmorPenetration();
    UpdateSpellPenetration();
    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();
    UpdateExpertise(BASE_ATTACK);
    UpdateExpertise(OFF_ATTACK);
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Player::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));

        Pet *pet = GetPet();
        if (pet)
            pet->UpdateResistances(school);
    }
    else
        UpdateArmor();
}

void Player::UpdateSpellPenetration()
{
    float value = 0;
    // Spell Penetration is given in positive for proper showing in client we need to do -value
    value -= GetTotalAuraModValue(UNIT_MOD_SPELL_PENETRATION);
    // does not really matter which school we use, it just must be a magic one
    value +=(float)GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_TARGET_RESISTANCE, SPELL_SCHOOL_MASK_SHADOW);

    SetSpellPenetration(int32(value));
}

void Player::UpdateArmor()
{
    float value = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    value  = GetModifierValue(unitMod, BASE_VALUE);         // base armor (from items)
    value *= GetModifierValue(unitMod, BASE_PCT);           // armor percent from items
    value += GetStat(STAT_AGILITY) * 2.0f;                  // armor bonus from stats
    value += GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    AuraList const& mResbyIntellect = GetAurasByType(SPELL_AURA_MOD_RESISTANCE_OF_STAT_PERCENT);
    for(AuraList::const_iterator i = mResbyIntellect.begin();i != mResbyIntellect.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        if (mod->m_miscvalue & SPELL_SCHOOL_MASK_NORMAL)
            value += int32(GetStat(Stats((*i)->GetMiscBValue())) * mod->m_amount / 100.0f);
    }

    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));

    Pet *pet = GetPet();
    if (pet)
        pet->UpdateArmor();

    UpdateAttackPowerAndDamage();                           // armor dependent auras update for SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR
}

float Player::GetHealthBonusFromStamina()
{
    float stamina = GetStat(STAT_STAMINA);

    float baseStam = stamina < 20 ? stamina : 20;
    float moreStam = stamina - baseStam;

    return baseStam + (moreStam*10.0f);
}

float Player::GetManaBonusFromIntellect()
{
    float intellect = GetStat(STAT_INTELLECT);

    float baseInt = intellect < 20 ? intellect : 20;
    float moreInt = intellect - baseInt;

    return baseInt + (moreInt*15.0f);
}

void Player::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;

    float value = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) + GetHealthBonusFromStamina();
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Player::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    uint32 create_power = GetCreatePowers(power);

    // ignore classes without mana
    float bonusPower = (power == POWER_MANA && create_power > 0) ? GetManaBonusFromIntellect() : 0;

    float value = GetModifierValue(unitMod, BASE_VALUE) + create_power;
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) +  bonusPower;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Player::ApplyFeralAPBonus(int32 amount, bool apply)
{
    m_baseFeralAP+= apply ? amount:-amount;
    UpdateAttackPowerAndDamage();
}

void Player::UpdateAttackPowerAndDamage(bool ranged )
{
    float val2 = 0.0f;
    float level = float(getLevel());

    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod = UNIT_FIELD_ATTACK_POWER_MODS;
    uint16 index_mult = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        index_mod = UNIT_FIELD_RANGED_ATTACK_POWER_MODS;
        index_mult = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;

        switch(getClass())
        {
            case CLASS_HUNTER: val2 = level * 2.0f + GetStat(STAT_AGILITY) - 10.0f;    break;
            case CLASS_ROGUE:  val2 = level        + GetStat(STAT_AGILITY) - 10.0f;    break;
            case CLASS_WARRIOR:val2 = level        + GetStat(STAT_AGILITY) - 10.0f;    break;
            case CLASS_DRUID:
                switch(m_form)
                {
                    case FORM_CAT:
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = 0.0f; break;
                    default:
                        val2 = GetStat(STAT_AGILITY) - 10.0f; break;
                }
                break;
            default: val2 = GetStat(STAT_AGILITY) - 10.0f; break;
        }
    }
    else
    {
        switch(getClass())
        {
            case CLASS_WARRIOR:      val2 = level*3.0f + GetStat(STAT_STRENGTH)*2.0f                    - 20.0f; break;
            case CLASS_PALADIN:      val2 = level*3.0f + GetStat(STAT_STRENGTH)*2.0f                    - 20.0f; break;
            case CLASS_DEATH_KNIGHT: val2 = level*3.0f + GetStat(STAT_STRENGTH)*2.0f                    - 20.0f; break;
            case CLASS_ROGUE:        val2 = level*2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f; break;
            case CLASS_HUNTER:       val2 = level*2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f; break;
            case CLASS_SHAMAN:       val2 = level*2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f; break;
            case CLASS_DRUID:
            {
                //Check if Predatory Strikes is skilled
                float mLevelBonus = 0.0f;
                float mBonusWeaponAtt = 0.0f;
                switch(m_form)
                {
                    case FORM_CAT:
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                    case FORM_MOONKIN:
                    {
                        Unit::AuraList const& mDummy = GetAurasByType(SPELL_AURA_DUMMY);
                        for(Unit::AuraList::const_iterator itr = mDummy.begin(); itr != mDummy.end(); ++itr)
                        {
                            if ((*itr)->GetSpellProto()->SpellIconID != 1563)
                                continue;

                            // Predatory Strikes (effect 0)
                            if ((*itr)->GetEffIndex() == EFFECT_INDEX_0 && IsInFeralForm())
                                mLevelBonus = getLevel() * (*itr)->GetModifier()->m_amount / 100.0f;
                            // Predatory Strikes (effect 1)
                            else if ((*itr)->GetEffIndex() == EFFECT_INDEX_1)
                                mBonusWeaponAtt = (*itr)->GetModifier()->m_amount * m_baseFeralAP / 100.0f;

                            if (mLevelBonus != 0.0f && mBonusWeaponAtt != 0.0f)
                                break;
                        }
                        break;
                    }
                    default: break;
                }

                switch(m_form)
                {
                    case FORM_CAT:
                        val2 = GetStat(STAT_STRENGTH)*2.0f + GetStat(STAT_AGILITY) - 20.0f + mLevelBonus + m_baseFeralAP + mBonusWeaponAtt; break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = GetStat(STAT_STRENGTH)*2.0f - 20.0f + mLevelBonus + m_baseFeralAP + mBonusWeaponAtt; break;
                    case FORM_MOONKIN:
                        val2 = GetStat(STAT_STRENGTH)*2.0f - 20.0f + m_baseFeralAP + mBonusWeaponAtt; break;
                    default:
                        val2 = GetStat(STAT_STRENGTH)*2.0f - 20.0f; break;
                }
                break;
            }
            case CLASS_MAGE:    val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
            case CLASS_PRIEST:  val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
            case CLASS_WARLOCK: val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
        }
    }

    SetModifierValue(unitMod, BASE_VALUE, val2);

    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    if ( ranged )
    {
        if ((getClassMask() & CLASSMASK_WAND_USERS)==0)
        {
            AuraList const& mRAPbyStat = GetAurasByType(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT);
            for(AuraList::const_iterator i = mRAPbyStat.begin();i != mRAPbyStat.end(); ++i)
                attPowerMod += int32(GetStat(Stats((*i)->GetModifier()->m_miscvalue)) * (*i)->GetModifier()->m_amount / 100.0f);
        }
    }
    else
    {
        AuraList const& mAPbyStat = GetAurasByType(SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT);
        for(AuraList::const_iterator i = mAPbyStat.begin();i != mAPbyStat.end(); ++i)
            attPowerMod += int32(GetStat(Stats((*i)->GetModifier()->m_miscvalue)) * (*i)->GetModifier()->m_amount / 100.0f);

        AuraList const& mAPbyArmor = GetAurasByType(SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR);
        for(AuraList::const_iterator iter = mAPbyArmor.begin(); iter != mAPbyArmor.end(); ++iter)
            // always: ((*i)->GetModifier()->m_miscvalue == 1 == SPELL_SCHOOL_MASK_NORMAL)
            attPowerMod += int32(GetArmor() / (*iter)->GetModifier()->m_amount);
    }

    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, (uint32)base_attPower);            //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(index_mod, (uint32)attPowerMod);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetFloatValue(index_mult, attPowerMultiplier);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    //automatically update weapon damage after attack power modification
    if (ranged)
    {
        UpdateDamagePhysical(RANGED_ATTACK);
    }
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        if (CanDualWield() && haveOffhandWeapon())           //allow update offhand damage only if player knows DualWield Spec and has equipped offhand weapon
            UpdateDamagePhysical(OFF_ATTACK);
    }

    Pet *pet = GetPet();                                //update pet's AP
    if (pet)
        pet->UpdateAttackPowerAndDamage();
}

void Player::UpdateShieldBlockValue()
{
    SetUInt32Value(PLAYER_SHIELD_BLOCK, GetShieldBlockValue());
}

void Player::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, float& min_damage, float& max_damage)
{
    UnitMods unitMod;
    UnitMods attPower;

    switch(attType)
    {
        case BASE_ATTACK:
        default:
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            attPower = UNIT_MOD_ATTACK_POWER;
            break;
        case OFF_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            attPower = UNIT_MOD_ATTACK_POWER;
            break;
        case RANGED_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            attPower = UNIT_MOD_ATTACK_POWER_RANGED;
            break;
    }

    float att_speed = GetAPMultiplier(attType,normalized);

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType)/ 14.0f * att_speed;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (IsInFeralForm())                                    //check if player is druid and in cat or bear forms
    {
        uint32 lvl = getLevel();
        if ( lvl > 60 ) lvl = 60;

        weapon_mindamage = lvl*0.85f*att_speed;
        weapon_maxdamage = lvl*1.25f*att_speed;
    }
    else if (!IsUseEquipedWeapon(attType))                   //check if player not in form but still can't use weapon (broken/etc)
    {
        weapon_mindamage = BASE_MINDAMAGE;
        weapon_maxdamage = BASE_MAXDAMAGE;
    }
    else if (attType == RANGED_ATTACK)                       //add ammo DPS to ranged damage
    {
        weapon_mindamage += GetAmmoDPS() * att_speed;
        weapon_maxdamage += GetAmmoDPS() * att_speed;
    }

    min_damage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    max_damage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;
}

void Player::UpdateDamagePhysical(WeaponAttackType attType)
{
    float mindamage;
    float maxdamage;

    CalculateMinMaxDamage(attType,false,mindamage,maxdamage);

    switch(attType)
    {
        case BASE_ATTACK:
        default:
            SetStatFloatValue(UNIT_FIELD_MINDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXDAMAGE,maxdamage);
            break;
        case OFF_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE,maxdamage);
            break;
        case RANGED_ATTACK:
            SetStatFloatValue(UNIT_FIELD_MINRANGEDDAMAGE,mindamage);
            SetStatFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE,maxdamage);
            break;
    }
}

void Player::UpdateDefenseBonusesMod()
{
    UpdateBlockPercentage();
    UpdateParryPercentage();
    UpdateDodgePercentage();
}

void Player::UpdateBlockPercentage()
{
    // No block
    float value = 0.0f;
    if (CanBlock())
    {
        // Base value
        value = 5.0f;
        // Modify value from defense skill
        value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        // Increase from SPELL_AURA_MOD_BLOCK_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
        // Increase from rating
        value += GetRatingBonusValue(CR_BLOCK);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_BLOCK_PERCENTAGE, value);
}

void Player::UpdateCritPercentage(WeaponAttackType attType)
{
    BaseModGroup modGroup;
    uint16 index;
    CombatRating cr;

    switch(attType)
    {
        case OFF_ATTACK:
            modGroup = OFFHAND_CRIT_PERCENTAGE;
            index = PLAYER_OFFHAND_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
        case RANGED_ATTACK:
            modGroup = RANGED_CRIT_PERCENTAGE;
            index = PLAYER_RANGED_CRIT_PERCENTAGE;
            cr = CR_CRIT_RANGED;
            break;
        case BASE_ATTACK:
        default:
            modGroup = CRIT_PERCENTAGE;
            index = PLAYER_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
    }

    float value = GetTotalPercentageModValue(modGroup) + GetRatingBonusValue(cr);
    // Modify crit from weapon skill and maximized defense skill of same level victim difference
    value += (int32(GetWeaponSkillValue(attType)) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(index, value);
}

void Player::UpdateAllCritPercentages()
{
    float value = GetMeleeCritFromAgility();

    SetBaseModValue(CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(OFFHAND_CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(RANGED_CRIT_PERCENTAGE, PCT_MOD, value);

    UpdateCritPercentage(BASE_ATTACK);
    UpdateCritPercentage(OFF_ATTACK);
    UpdateCritPercentage(RANGED_ATTACK);
}

void Player::UpdateParryPercentage()
{
    // No parry
    float value = 0.0f;
    if (CanParry())
    {
        // Base parry
        value  = 5.0f;
        // Modify value from defense skill
        value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        // Parry from SPELL_AURA_MOD_PARRY_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        // Parry from rating
        value += GetRatingBonusValue(CR_PARRY);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_PARRY_PERCENTAGE, value);
}

void Player::UpdateDodgePercentage()
{
    // Dodge from agility
    float value = GetBaseDodge();
    value += GetDodgeFromAgility();
    // Modify value from defense skill
    value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    // Dodge from SPELL_AURA_MOD_DODGE_PERCENT aura
    value += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
    // Dodge from rating
    value += GetRatingBonusValue(CR_DODGE);
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(PLAYER_DODGE_PERCENTAGE, value);
}

void Player::UpdateSpellCritChance(uint32 school)
{
    // For normal school set zero crit chance
    if (school == SPELL_SCHOOL_NORMAL)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1, 0.0f);
        return;
    }
    // For others recalculate it from:
    float crit = 0.0f;
    // Crit from Intellect
    crit += GetSpellCritFromIntellect();
    // Increase crit from SPELL_AURA_MOD_SPELL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    // Increase crit from SPELL_AURA_MOD_ALL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_ALL_CRIT_CHANCE);
    // Increase crit by school from SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
    crit += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, 1<<school);
    // Increase crit from spell crit ratings
    crit += GetRatingBonusValue(CR_CRIT_SPELL);

    // Store crit value
    SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school, crit);
}

void Player::UpdateMeleeHitChances()
{
    m_modMeleeHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_HIT_CHANCE);
    m_modMeleeHitChance+=  GetRatingBonusValue(CR_HIT_MELEE);
}

void Player::UpdateRangedHitChances()
{
    m_modRangedHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_HIT_CHANCE);
    m_modRangedHitChance+= GetRatingBonusValue(CR_HIT_RANGED);
}

void Player::UpdateSpellHitChances()
{
    m_modSpellHitChance = GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_HIT_CHANCE);
    m_modSpellHitChance+= GetRatingBonusValue(CR_HIT_SPELL);
}

void Player::UpdateAllSpellCritChances()
{
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateSpellCritChance(i);
}

void Player::UpdateExpertise(WeaponAttackType attack)
{
    if (attack==RANGED_ATTACK)
        return;

    int32 expertise = int32(GetRatingBonusValue(CR_EXPERTISE));

    Item *weapon = GetWeaponForAttack(attack);

    AuraList const& expAuras = GetAurasByType(SPELL_AURA_MOD_EXPERTISE);
    for(AuraList::const_iterator itr = expAuras.begin(); itr != expAuras.end(); ++itr)
    {
        // item neutral spell
        if ((*itr)->GetSpellProto()->EquippedItemClass == -1)
            expertise += (*itr)->GetModifier()->m_amount;
        // item dependent spell
        else if (weapon && weapon->IsFitToSpellRequirements((*itr)->GetSpellProto()))
            expertise += (*itr)->GetModifier()->m_amount;
    }

    if (expertise < 0)
        expertise = 0;

    switch(attack)
    {
        case BASE_ATTACK: SetUInt32Value(PLAYER_EXPERTISE, expertise);         break;
        case OFF_ATTACK:  SetUInt32Value(PLAYER_OFFHAND_EXPERTISE, expertise); break;
        default: break;
    }
}

void Player::UpdateArmorPenetration()
{
    m_armorPenetrationPct = GetRatingBonusValue(CR_ARMOR_PENETRATION);

    AuraList const& armorAuras = GetAurasByType(SPELL_AURA_MOD_TARGET_ARMOR_PCT);
    for(AuraList::const_iterator itr = armorAuras.begin(); itr != armorAuras.end(); ++itr)
    {
        // affects all weapons
        if ((*itr)->GetSpellProto()->EquippedItemClass == -1)
        {
            m_armorPenetrationPct += (*itr)->GetModifier()->m_amount;
            continue;
        }

        // dependent on weapon class
        for(uint8 i = 0; i < MAX_ATTACK; ++i)
        {
            Item *weapon = GetWeaponForAttack(WeaponAttackType(i));
            if (weapon && weapon->IsFitToSpellRequirements((*itr)->GetSpellProto()))
            {
                m_armorPenetrationPct += (*itr)->GetModifier()->m_amount;
                break;
            }
        }
    }
}

void Player::ApplyManaRegenBonus(int32 amount, bool apply)
{
    m_baseManaRegen+= apply ? amount : -amount;
    UpdateManaRegen();
}

void Player::UpdateManaRegen()
{
    float Intellect = GetStat(STAT_INTELLECT);
    // Mana regen from spirit and intellect
    float power_regen = sqrt(Intellect) * OCTRegenMPPerSpirit();
    // Apply PCT bonus from SPELL_AURA_MOD_POWER_REGEN_PERCENT aura on spirit base regen
    power_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);

    // Mana regen from SPELL_AURA_MOD_POWER_REGEN aura
    float power_regen_mp5 = (GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA) + m_baseManaRegen) / 5.0f;

    // Get bonus from SPELL_AURA_MOD_MANA_REGEN_FROM_STAT aura
    AuraList const& regenAura = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_FROM_STAT);
    for(AuraList::const_iterator i = regenAura.begin();i != regenAura.end(); ++i)
    {
        Modifier* mod = (*i)->GetModifier();
        power_regen_mp5 += GetStat(Stats(mod->m_miscvalue)) * mod->m_amount / 500.0f;
    }

    // Set regen rate in cast state apply only on spirit based regen
    int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
    if (modManaRegenInterrupt > 100)
        modManaRegenInterrupt = 100;
    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER, power_regen_mp5 + power_regen * modManaRegenInterrupt / 100.0f);

    SetStatFloatValue(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER, power_regen_mp5 + power_regen);
}

void Player::_ApplyAllStatBonuses()
{
    SetCanModifyStats(false);

    _ApplyAllAuraMods();
    _ApplyAllItemMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

void Player::_RemoveAllStatBonuses()
{
    SetCanModifyStats(false);

    _RemoveAllItemMods();
    _RemoveAllAuraMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/*#######################################
########                         ########
########    MOBS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Creature::UpdateStats(Stats /*stat*/)
{
    return true;
}

bool Creature::UpdateAllStats()
{
    UpdateMaxHealth();
    UpdateAttackPowerAndDamage();

    for(int i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    for(int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Creature::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Creature::UpdateArmor()
{
    float value = GetTotalAuraModValue(UNIT_MOD_ARMOR);
    SetArmor(int32(value));
}

void Creature::UpdateMaxHealth()
{
    float value = GetTotalAuraModValue(UNIT_MOD_HEALTH);
    SetMaxHealth((uint32)value);
}

void Creature::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float value  = GetTotalAuraModValue(unitMod);
    SetMaxPower(power, uint32(value));
}

void Creature::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod = UNIT_FIELD_ATTACK_POWER_MODS;
    uint16 index_mult = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        index_mod = UNIT_FIELD_RANGED_ATTACK_POWER_MODS;
        index_mult = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;
    }

    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, (uint32)base_attPower);            //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(index_mod, (uint32)attPowerMod);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetFloatValue(index_mult, attPowerMultiplier);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    if (ranged)
        return;
    //automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);
}

void Creature::UpdateDamagePhysical(WeaponAttackType attType)
{
    if (attType > BASE_ATTACK)
        return;

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    /* difference in AP between current attack power and base value from DB */
    float att_pwr_change = GetTotalAttackPowerValue(attType) - GetCreatureInfo()->attackpower;
    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + (att_pwr_change * GetAPMultiplier(attType, false) / 14.0f);
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);
    float dmg_multiplier = GetCreatureInfo()->dmg_multiplier;

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * dmg_multiplier * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * dmg_multiplier * base_pct + total_value) * total_pct;

    SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
    SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
}

/*#######################################
########                         ########
########    PETS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Pet::UpdateStats(Stats stat)
{
    if (stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);
    CreatureInfo const *cinfo = GetCreatureInfo(); 

    Unit *owner = GetOwner();

    // chained, use original owner instead
    if (owner && owner->GetTypeId() == TYPEID_UNIT && ((Creature*)owner)->GetEntry() == GetEntry())
        if (Unit *creator = GetCreator())
            owner = creator;

    if (owner && owner->GetTypeId() == TYPEID_PLAYER)
    {
        float scale_coeff = 0.0f;
        switch(stat)
        {
            case STAT_STRENGTH:
            {
                if (owner->getClass() == CLASS_DEATH_KNIGHT)
                {
                    if (getPetType() == SUMMON_PET)
                    {
                        scale_coeff = 0.7f;
                        // Ravenous Dead
                        if (SpellEntry const* spell = ((Player*)owner)->GetKnownTalentRankById(1934))
                            scale_coeff *= 1.0f + spell->CalculateSimpleValue(EFFECT_INDEX_1) / 100.0f;
                        // Glyph of Ghoul
                        if (Aura *glyph = owner->GetDummyAura(58686))
                            scale_coeff += glyph->GetModifier()->m_amount / 100.0f;
                    }
                }
                break;
            }
            case STAT_STAMINA:
            {
                scale_coeff = 0.3f;
                switch (owner->getClass())
                {
                    case CLASS_HUNTER:
                    {
                        scale_coeff = 0.45f;
                        //Wild Hunt
                        uint32 bonus_id = 0;
                        if (HasSpell(62762))
                            bonus_id = 62762;
                        else if (HasSpell(62758))
                            bonus_id = 62758;
                        if (const SpellEntry *bonusProto = sSpellStore.LookupEntry(bonus_id)) 
                            scale_coeff *= 1 + bonusProto->CalculateSimpleValue(EFFECT_INDEX_0) / 100.0f;
                        break;
                    }
                    case CLASS_WARLOCK:
                    {
                        scale_coeff = 0.75f;
                        break;
                    }
                    case CLASS_DEATH_KNIGHT:
                    {
                        if (getPetType() == SUMMON_PET)
                        {
                            // Ravenous Dead
                            if (owner->GetTypeId() == TYPEID_PLAYER)
                                if (SpellEntry const* spell = ((Player*)owner)->GetKnownTalentRankById(1934))
                                    scale_coeff *= 1.0f + spell->CalculateSimpleValue(EFFECT_INDEX_1) / 100.0f;
                            // Glyph of Ghoul
                            if (Aura *glyph = owner->GetDummyAura(58686))
                                scale_coeff += glyph->GetModifier()->m_amount / 100.0f;
                        }
                        break; 
                    }
                    case CLASS_DRUID:
                    {
                        
                        //For treants, use 70% of stamina / 3 treants, guessed
                        scale_coeff = 0.7f / 3.0f; 
                        break;
                    }
                }
                break;
            }
            case STAT_INTELLECT:
            {
                //warlock's and mage's pets gain 30% of owner's intellect
                if (getPetType() == SUMMON_PET)
                {
                    if (owner && (owner->getClass() == CLASS_WARLOCK || owner->getClass() == CLASS_MAGE) )
                        scale_coeff = 0.3f;
                }
                break;
            }
        }
        value += float(owner->GetStat(stat)) * scale_coeff;
    }
    SetStat(stat, int32(value));

    switch(stat)
    {
        case STAT_STRENGTH:         UpdateAttackPowerAndDamage();        break;
        case STAT_AGILITY:          UpdateArmor();                       break;
        case STAT_STAMINA:          UpdateMaxHealth();                   break;
        case STAT_INTELLECT:        UpdateMaxPower(POWER_MANA);          break;
        case STAT_SPIRIT:
        default:
            break;
    }

    return true;
}

bool Pet::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        UpdateStats(Stats(i));

    for(int i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Pet::UpdateResistances(uint32 school)
{
    if (school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));

        Unit *owner = GetOwner();
        // chained, use original owner instead
        if (owner && owner->GetTypeId() == TYPEID_UNIT && ((Creature*)owner)->GetEntry() == GetEntry())
            if (Unit *creator = GetCreator())
                owner = creator;

        // hunter and warlock pets gain 40% of owner's resistance
        if (owner && (getPetType() == HUNTER_PET || (getPetType() == SUMMON_PET && owner->getClass() == CLASS_WARLOCK)))
            value += float(owner->GetResistance(SpellSchools(school))) * 0.4f;

        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Pet::UpdateArmor()
{
    float value = 0.0f;
    float bonus_armor = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    Unit *owner = GetOwner();
    // chained, use original owner instead
    if (owner && owner->GetTypeId() == TYPEID_UNIT && ((Creature*)owner)->GetEntry() == GetEntry())
        if (Unit *creator = GetCreator())
            owner = creator;

    // hunter and warlock and shaman pets gain 35% of owner's armor value
    if (owner && (getPetType() == HUNTER_PET || (getPetType() == SUMMON_PET 
        && (owner->getClass() == CLASS_WARLOCK || owner->getClass() == CLASS_SHAMAN))))
        bonus_armor = 0.35f * float(owner->GetArmor());

    value  = GetModifierValue(unitMod, BASE_VALUE);
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetStat(STAT_AGILITY);
    value += GetModifierValue(unitMod, TOTAL_VALUE) + bonus_armor;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));
}

void Pet::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;
    UnitMods unitModStam = UNIT_MOD_STAT_STAMINA;
    float stamina = GetStat(STAT_STAMINA) - GetCreateStat(STAT_STAMINA);
    stamina  *= GetModifierValue(unitModStam, TOTAL_PCT);

    float value   = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value  *= GetModifierValue(unitMod, BASE_PCT);
    value  += GetModifierValue(unitMod, TOTAL_VALUE) + stamina * 10.0f;
    value  *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Pet::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float addValue = (power == POWER_MANA) ? GetStat(STAT_INTELLECT) - GetCreateStat(STAT_INTELLECT) : 0.0f;

    float value  = GetModifierValue(unitMod, BASE_VALUE) + GetCreatePowers(power);
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) +  addValue * 15.0f;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Pet::UpdateAttackPowerAndDamage(bool ranged)
{
    if (ranged)
        return;

    float val = 0.0f;
    float bonusAP = 0.0f;
    UnitMods unitMod = UNIT_MOD_ATTACK_POWER;

    val = (GetStat(STAT_STRENGTH) - 20.0f) * 2;

    Unit* owner = GetOwner();

    // chained, use original owner instead
    if (owner && owner->GetTypeId() == TYPEID_UNIT && ((Creature*)owner)->GetEntry() == GetEntry())
        if (Unit *creator = GetCreator())
            owner = creator;

    if (owner && owner->GetTypeId()==TYPEID_PLAYER)
    {
        if (getPetType() == HUNTER_PET)                      //hunter pets benefit from owner's attack power
        {
            float ap_coeff = 0.22f;
            float sp_coeff = 0.1287f;
            //Wild Hunt
            uint32 bonus_id = 0;
            if (HasSpell(62762))
                bonus_id = 62762;
            else if (HasSpell(62758))
                bonus_id = 62758;
            if (const SpellEntry *bonusProto = sSpellStore.LookupEntry(bonus_id))
            {
                ap_coeff *= 1 + bonusProto->CalculateSimpleValue(EFFECT_INDEX_1) / 100.0f;
                sp_coeff *= 1 + bonusProto->CalculateSimpleValue(EFFECT_INDEX_1) / 100.0f;
            }

            bonusAP = owner->GetTotalAttackPowerValue(RANGED_ATTACK) * ap_coeff;
            SetBonusDamage( int32(owner->GetTotalAttackPowerValue(RANGED_ATTACK) * sp_coeff));
        }      
        else if (getPetType() == SUMMON_PET)
        {
            switch(owner->getClass())
            {
                case CLASS_WARLOCK:
                {
                    //demons benefit from warlocks shadow or fire damage
                    int32 fire  = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FIRE)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FIRE);
                    int32 shadow = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_SHADOW)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_SHADOW);
                    int32 maximum  = (fire > shadow) ? fire : shadow;
                    if (maximum < 0)
                        maximum = 0;
                    SetBonusDamage(int32(maximum * 0.15f));
                    bonusAP = maximum * 0.57f;
                    break;
                }
                case CLASS_MAGE:
                {
                    //water elementals benefit from mage's frost damage
                    int32 frost = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FROST)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FROST);
                    if (frost < 0)
                        frost = 0;
                    SetBonusDamage(int32(frost * 0.4f));
                    break;
                }
                case CLASS_SHAMAN:
                {
                    float coeff = 0.3f;
                    //Glyph of Feral Spirit
                    if (Aura *glyph = owner->GetDummyAura(63271))
                        coeff += glyph->GetModifier()->m_amount / 100.0f;
                    bonusAP += coeff * owner->GetTotalAttackPowerValue(BASE_ATTACK);
                    break;
                }
                case CLASS_DRUID:
                {
                    //Guessed
                    float coeff = 0.55f;
                    int32 sp = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_NATURE)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_NATURE);
                    bonusAp += coeff * sp;
                    break;    
                }
            }
        }
        else if (getPetType() == GUARDIAN_PET)
            if (owner->getClass() == CLASS_DEATH_KNIGHT)
                bonusAP += owner->GetTotalAttackPowerValue(BASE_ATTACK) * 0.4f;
    }

    SetModifierValue(UNIT_MOD_ATTACK_POWER, BASE_VALUE, val + bonusAP);

    //in BASE_VALUE of UNIT_MOD_ATTACK_POWER for creatures we store data of meleeattackpower field in DB
    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER, (int32)base_attPower);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetInt32Value(UNIT_FIELD_ATTACK_POWER_MODS, (int32)attPowerMod);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field
    SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, attPowerMultiplier);

    //automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);
}

void Pet::UpdateDamagePhysical(WeaponAttackType attType)
{
    if (attType > BASE_ATTACK)
        return;

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    float att_speed = float(GetAttackTime(BASE_ATTACK))/1000.0f;

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType)/ 14.0f * att_speed;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    //  Pet's base damage changes depending on happiness
    if (getPetType() == HUNTER_PET && attType == BASE_ATTACK)
    {
        switch(GetHappinessState())
        {
            case HAPPY:
                // 125% of normal damage
                total_pct *= 1.25f;
                break;
            case CONTENT:
                // 100% of normal damage, nothing to modify
                break;
            case UNHAPPY:
                // 75% of normal damage
                total_pct *= 0.75f;
                break;
        }
    }
    else if (getPetType() == SUMMON_PET)
    {    
        Unit* owner = GetOwner();
        if (owner && owner->getClass() == CLASS_PRIEST)
            total_value += 0.3f * owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_SHADOW);
    }

    float mindamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
    SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
}
