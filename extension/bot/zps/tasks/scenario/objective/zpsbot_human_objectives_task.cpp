#include NAVBOT_PCH_FILE
#include <mods/zps/zps_lib.h>
#include <mods/zps/zps_mod.h>
#include <bot/zps/zpsbot.h>
#include <bot/tasks_shared/bot_shared_roam.h>
#include <bot/tasks_shared/bot_shared_drop_weapon.h>
#include <bot/tasks_shared/bot_shared_collect_items.h>
#include <bot/tasks_shared/bot_shared_escort_entity.h>
#include "zpsbot_human_objectives_task.h"

AITask<CZPSBot>* CZPSBotObjectiveFindItemTask::InitialNextTask(CZPSBot* bot)
{
	// roam is used to simulate searching
	return new CBotSharedRoamTask<CZPSBot, CZPSBotPathCost>(bot, 8192.0f);
}

TaskResult<CZPSBot> CZPSBotObjectiveFindItemTask::OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask)
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	const std::string& name = mgr.GetItemSearchID();

	if (bot->GetInventoryInterface()->FindItemDeliver(name) != nullptr)
	{
		return PauseFor(new CBotSharedRoamTask<CZPSBot, CZPSBotPathCost>(bot, 8192.0f), "Already have required item, roaming!");
	}

	return Continue();
}

TaskResult<CZPSBot> CZPSBotObjectiveFindItemTask::OnTaskUpdate(CZPSBot* bot)
{
	if (GetNextTask() == nullptr)
	{
		StartNewNextTask(bot, new CBotSharedRoamTask<CZPSBot, CZPSBotPathCost>(bot, 8192.0f), "Restarting roam task!");
	}

	if (m_nextScanTimer.IsElapsed())
	{
		m_nextScanTimer.StartRandom(1.0f, 2.0f);
		const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
		const std::string& name = mgr.GetItemSearchID();

		if (bot->GetInventoryInterface()->FindItemDeliver(name))
		{
			return Done("I've got the item!");
		}

		const float detectionRadius = mgr.GetDetectionRadius();
		Vector eyePos = bot->GetEyeOrigin();
		CBaseEntity* found = nullptr;

		auto func = [&name, &detectionRadius, &found, &eyePos](int index, edict_t* edict, CBaseEntity* entity) {
			if (entity)
			{
				CBaseEntity* owner = nullptr;
				entprops->GetEntPropEnt(entity, Prop_Send, "m_hOwner", nullptr, &owner);

				if (owner)
				{
					return true;
				}

				bool cantequip = false;
				entprops->GetEntPropBool(entity, Prop_Send, "m_bCantEquip", cantequip);

				if (cantequip)
				{
					return true;
				}

				const Vector& pos = UtilHelpers::getEntityOrigin(entity);

				if ((pos - eyePos).IsLengthGreaterThan(detectionRadius))
				{
					return true;
				}

				std::string itemid = entprops->GetEntPropString(entity, Prop_Data, "m_strItemID");
				
				if (ke::StrCaseCmp(itemid.c_str(), name.c_str()) == 0)
				{
					found = entity;
					return false;
				}

				// Fallback to targetname for legacy/old maps
				const char* targetname = entityprops::GetEntityTargetname(entity);

				if (targetname && targetname[0] != '\0')
				{
					if (ke::StrCaseCmp(targetname, name.c_str()) == 0)
					{
						found = entity;
						return false;
					}
				}
			}

			return true;
		};

		UtilHelpers::ForEachEntityOfClassname("item_deliver", func);

		if (found)
		{
			m_nextScanTimer.Invalidate();
			
			if (!HasSpaceInInventory(bot))
			{
				const CBotWeapon* todrop = bot->GetInventoryInterface()->FindWeaponToDrop();

				if (todrop)
				{
					return PauseFor(new CBotSharedDropWeaponTask<CZPSBot>(todrop, 2.0f), "Dropping weapon to make space in my inventory!");
				}
			}

			auto task = new CBotSharedCollectItemsTask<CZPSBot, CZPSBotPathCost>(bot, found, NBotSharedCollectItemTask::COLLECT_PRESS_USE);
			auto func = std::bind(NBotSharedCollectItemTask::CommonWeaponValidator<CZPSBot>, std::placeholders::_1, std::placeholders::_2);
			task->SetValidationFunction(func);
			return SwitchTo(task, "Collecting item!");
		}
	}

	return Continue();
}

QueryAnswerType CZPSBotObjectiveFindItemTask::ShouldPickup(CBaseBot* me, CBaseEntity* item)
{
	if (item)
	{
		// don't pick weapons
		if (UtilHelpers::FClassnameIs(item, "weapon_*"))
		{
			return ANSWER_NO;
		}
	}

	return ANSWER_UNDEFINED;
}

bool CZPSBotObjectiveFindItemTask::HasSpaceInInventory(CZPSBot* bot) const
{
	const WeaponInfo* info = extmanager->GetMod()->GetWeaponInfoManager()->GetClassnameInfo("item_deliver");

	if (!info) { return true; }

	int size = static_cast<const ZPSWeaponInfo*>(info)->GetInventorySize();

	if (bot->GetInventoryInterface()->GetInventorySize() + size >= CZPSBotInventory::MAX_INVENTORY_SIZE)
	{
		return false;
	}

	return true;
}

bool CZPSBotObjectiveUseItemTask::IsPossible(CZPSBot* bot)
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	const std::string& name = mgr.GetItemSearchID();
	CBaseEntity* specificitem = mgr.GetGenericTargetEntity();

	// target not set
	if (mgr.GetUseItemTarget() == nullptr)
	{
		return false;
	}

	if (specificitem)
	{
		// bot got the specific item needed
		if (bot->GetInventoryInterface()->HasWeapon(specificitem))
		{
			return true;
		}
	}

	// we have the item in our inventory
	if (bot->GetInventoryInterface()->FindItemDeliver(name) != nullptr)
	{
		return true;
	}

	return false;
}

TaskResult<CZPSBot> CZPSBotObjectiveUseItemTask::OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask)
{
	m_usingItem = false;
	return Continue();
}

TaskResult<CZPSBot> CZPSBotObjectiveUseItemTask::OnTaskUpdate(CZPSBot* bot)
{
	// waiting for the weapon switch cooldown
	if (!m_switchDelay.IsElapsed())
	{
		return Continue();
	}

	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	CBaseEntity* entity = mgr.GetUseItemTarget();

	if (!entity)
	{
		return Done("Use target is NULL!");
	}

	Vector eyePos = bot->GetEyeOrigin();
	Vector center = UtilHelpers::getWorldSpaceCenter(entity);

	// item_deliver use radius appears to be smaller than the default use radius
	if ((eyePos - center).IsLengthLessThan((CBaseExtPlayer::PLAYER_USE_RADIUS * 0.7f)))
	{
		m_usingItem = true;

		if (!IsItemEquipped(bot))
		{
			EquipRequiredItem(bot);
			return Continue();
		}

		bot->GetControlInterface()->AimAt(center, IPlayerController::LOOK_PRIORITY, 0.5f, "Looking at use entity!");

		if (bot->GetControlInterface()->IsAimOnTarget())
		{
			bot->GetControlInterface()->PressAttackButton(0.2f);
			return Done("Item used!");
		}
	}

	if (m_nav.NeedsRepath())
	{
		CZPSBotPathCost cost(bot, RouteType::FASTEST_ROUTE);
		Vector pos = trace::getground(center);
		m_nav.ComputePathToPosition(bot, pos, cost);
		m_nav.StartRepathTimer();
	}

	m_nav.Update(bot);
	return Continue();
}

QueryAnswerType CZPSBotObjectiveUseItemTask::ShouldPickup(CBaseBot* me, CBaseEntity* item)
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	CBaseEntity* entity = mgr.GetUseItemTarget();

	if (!entity)
	{
		return ANSWER_UNDEFINED;
	}

	Vector eyePos = me->GetEyeOrigin();
	Vector p1 = UtilHelpers::getWorldSpaceCenter(entity);
	Vector p2 = UtilHelpers::getWorldSpaceCenter(item);
	float d1 = (eyePos - p1).LengthSqr();
	float d2 = (eyePos - p2).LengthSqr();

	// do not pick up items if the use target is closer than the item
	if (d1 <= d2)
	{
		return ANSWER_NO;
	}

	return m_usingItem ? ANSWER_UNDEFINED : ANSWER_NO;
}

bool CZPSBotObjectiveUseItemTask::IsItemEquipped(CZPSBot* bot) const
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	CBaseEntity* specificitem = mgr.GetGenericTargetEntity();
	const CBotWeapon* activeWeapon = bot->GetInventoryInterface()->GetActiveBotWeapon();

	if (!activeWeapon)
	{
		return false;
	}

	if (activeWeapon->GetEntity() == specificitem)
	{
		return true;
	}

	const CBotWeapon* requiredItem = bot->GetInventoryInterface()->FindItemDeliver(mgr.GetItemSearchID());

	if (activeWeapon == requiredItem)
	{
		return true;
	}

	return false;
}

void CZPSBotObjectiveUseItemTask::EquipRequiredItem(CZPSBot* bot)
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	CBaseEntity* specificitem = mgr.GetGenericTargetEntity();

	const CBotWeapon* weapon = bot->GetInventoryInterface()->GetWeaponOfEntity(specificitem);

	if (weapon)
	{
		m_switchDelay.Start(3.0f);
		bot->GetInventoryInterface()->EquipWeapon(weapon);
		return;
	}

	weapon = bot->GetInventoryInterface()->FindItemDeliver(mgr.GetItemSearchID());

	if (weapon)
	{
		m_switchDelay.Start(3.0f);
		bot->GetInventoryInterface()->EquipWeapon(weapon);
	}
}

bool CZPSBotObjectiveFollowItemCarrierTask::IsPossible(CZPSBot* bot, CBaseEntity** carrier)
{
	CBaseEntity* pPlayer = nullptr;
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();

	auto func = [&pPlayer, &mgr](int client, edict_t* entity, SourceMod::IGamePlayer* player) {
		CBaseEntity* pEntity = UtilHelpers::EdictToBaseEntity(entity);

		if (pEntity)
		{
			if (modhelpers->GetEntityTeamNumber(pEntity) != static_cast<int>(zps::ZPSTeam::ZPS_TEAM_SURVIVORS))
			{
				return;
			}

			if (modhelpers->IsDead(pEntity))
			{
				return;
			}

			if (zpslib::PlayerHasNamedItem(pEntity, mgr.GetItemSearchID().c_str()))
			{
				pPlayer = pEntity;
				return;
			}
		}
	};

	UtilHelpers::ForEachPlayer(func);

	if (pPlayer)
	{
		if (pPlayer == bot->GetEntity())
		{
			return false;
		}

		*carrier = pPlayer;
		return true;
	}

	return false;
}

AITask<CZPSBot>* CZPSBotObjectiveFollowItemCarrierTask::InitialNextTask(CZPSBot* bot)
{
	CBaseEntity* carrier = m_carrier.Get();

	if (carrier)
	{
		return new CBotSharedEscortEntityTask<CZPSBot, CZPSBotPathCost>(bot, carrier, 1e10f, 450.0f);
	}

	return nullptr;
}

TaskResult<CZPSBot> CZPSBotObjectiveFollowItemCarrierTask::OnTaskUpdate(CZPSBot* bot)
{
	CBaseEntity* carrier = m_carrier.Get();

	if (!carrier)
	{
		return Done("Item carrier disconnected!");
	}

	if (GetNextTask() == nullptr)
	{
		return Done("No longer following the item carrier!");
	}

	if (m_checkInventory.IsElapsed())
	{
		m_checkInventory.Start(5.0f);
	}

	return Continue();
}

bool CZPSBotObjectiveDropItemTask::IsPossible(CZPSBot* bot)
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();

	CBaseEntity* target = mgr.GetUseItemTarget();

	if (!target)
	{
		return false;
	}

	CZPSBotInventory* inventory = bot->GetInventoryInterface();
	CBaseEntity* item = mgr.GetGenericTargetEntity();

	if (item != nullptr)
	{
		if (inventory->HasWeapon(item))
		{
			return true;
		}
	}

	return inventory->FindItemDeliver(mgr.GetItemSearchID()) != nullptr;
}

TaskResult<CZPSBot> CZPSBotObjectiveDropItemTask::OnTaskUpdate(CZPSBot* bot)
{
	if (!m_switchCooldown.IsElapsed())
	{
		return Continue();
	}

	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();

	if (!OwnsRequiredItem(bot))
	{
		return Done("Bot doesn't own the required item!");
	}

	CBaseEntity* target = mgr.GetUseItemTarget();

	if (!target)
	{
		return Done("Drop target is NULL!");
	}

	Vector vTarget = UtilHelpers::getWorldSpaceCenter(target);
	Vector eyePos = bot->GetEyeOrigin();

	if ((eyePos - vTarget).IsLengthLessThan(mgr.GetDetectionRadius()))
	{
		bot->GetCombatInterface()->DisableCombat(0.5f);

		if (!IsItemEquipped(bot))
		{
			EquipRequiredItem(bot);
			return Continue();
		}

		bot->GetControlInterface()->AimAt(vTarget, IPlayerController::LOOK_CRITICAL, 1.0f, "Looking at drop target entity!");

		if (bot->GetControlInterface()->IsAimOnTarget())
		{
			bot->GetInventoryInterface()->DropHeldWeapon();
		}
	}

	if (m_nav.NeedsRepath())
	{
		CZPSBotPathCost cost(bot);
		m_nav.ComputePathToPosition(bot, vTarget, cost);
		m_nav.StartRepathTimer();
	}

	m_nav.Update(bot);
	return Continue();
}

bool CZPSBotObjectiveDropItemTask::OwnsRequiredItem(CZPSBot* bot) const
{
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	CZPSBotInventory* inventory = bot->GetInventoryInterface();
	CBaseEntity* item = mgr.GetGenericTargetEntity();

	if (item != nullptr)
	{
		if (inventory->HasWeapon(item))
		{
			return true;
		}
	}

	return inventory->FindItemDeliver(mgr.GetItemSearchID()) != nullptr;
}

bool CZPSBotObjectiveDropItemTask::IsItemEquipped(CZPSBot* bot) const
{
	CZPSBotInventory* inventory = bot->GetInventoryInterface();
	auto weapon = inventory->GetActiveZPSWeapon();

	if (!weapon)
	{
		return false;
	}

	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();
	
	CBaseEntity* item = mgr.GetGenericTargetEntity();

	if (weapon->GetEntity() == item)
	{
		return true;
	}

	return weapon->IsItemGenericOfID(mgr.GetItemSearchID());
}

void CZPSBotObjectiveDropItemTask::EquipRequiredItem(CZPSBot* bot)
{
	CZPSBotInventory* inventory = bot->GetInventoryInterface();
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();

	CBaseEntity* item = mgr.GetGenericTargetEntity();
	auto weapon = inventory->GetWeaponOfEntity(item);

	if (weapon)
	{
		inventory->EquipWeapon(weapon);
		m_switchCooldown.Start(2.0f);
		return;
	}

	auto itemdeliver = inventory->FindItemDeliver(mgr.GetItemSearchID());

	if (itemdeliver)
	{
		inventory->EquipWeapon(itemdeliver);
		m_switchCooldown.Start(2.0f);
	}
}
void CZPSBotObjectiveUseButtonTask::AddActiveBot(int client)
{
	if (!IsActiveBot(client))
	{
		s_activeBots.push_back(client);
	}
}

void CZPSBotObjectiveUseButtonTask::RemoveActiveBot(int client)
{
	s_activeBots.erase(std::remove(s_activeBots.begin(), s_activeBots.end(), client), s_activeBots.end());
}

bool CZPSBotObjectiveUseButtonTask::IsActiveBot(int client)
{
	return std::find(s_activeBots.begin(), s_activeBots.end(), client) != s_activeBots.end();
}

TaskResult<CZPSBot> CZPSBotObjectiveUseButtonTask::OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask)
{
	AddActiveBot(bot->GetIndex());
	return Continue();
}

void CZPSBotObjectiveUseButtonTask::OnTaskEnd(CZPSBot* bot, AITask<CZPSBot>* nextTask)
{
	RemoveActiveBot(bot->GetIndex());
}

bool CZPSBotObjectiveUseButtonTask::OnTaskPause(CZPSBot* bot, AITask<CZPSBot>* nextTask)
{
	RemoveActiveBot(bot->GetIndex());
	return true;
}

TaskResult<CZPSBot> CZPSBotObjectiveUseButtonTask::OnTaskResume(CZPSBot* bot, AITask<CZPSBot>* pastTask)
{
	AddActiveBot(bot->GetIndex());
	return Continue();
}

bool CZPSBotObjectiveUseButtonTask::IsAnotherBotCloser(CZPSBot* bot, const Vector& buttonPos) const
{
	const float myRange = (buttonPos - bot->GetEyeOrigin()).Length();
	const int myIndex = bot->GetIndex();
	bool closer = false;

	auto func = [&bot, &buttonPos, &myRange, &myIndex, &closer](CBaseBot* other) {
		if (closer || other == bot)
		{
			return;
		}

		// only bots currently going for the button compete for it
		if (!IsActiveBot(other->GetIndex()))
		{
			return;
		}

		if (!modhelpers->IsAlive(other->GetEntity()))
		{
			return;
		}

		if (static_cast<CZPSBot*>(other)->GetMyZPSTeam() != zps::ZPSTeam::ZPS_TEAM_SURVIVORS)
		{
			return;
		}

		const float otherRange = (buttonPos - other->GetEyeOrigin()).Length();

		if (otherRange > COMPETE_RADIUS)
		{
			return;
		}

		// ties go to the lower client index
		if (otherRange < myRange || (otherRange == myRange && other->GetIndex() < myIndex))
		{
			closer = true;
		}
	};

	extmanager->ForEachBot(func);
	return closer;
}

TaskResult<CZPSBot> CZPSBotObjectiveUseButtonTask::OnTaskUpdate(CZPSBot* bot)
{
	if (m_timeout.HasStarted() && m_timeout.IsElapsed())
	{
		return Done("Task timed out!");
	}

	if (bot->GetMovementInterface()->IsControllingMovements())
	{
		return Continue();
	}

	CBaseEntity* button = m_button.Get();

	if (!button)
	{
		return Done("Button is NULL!");
	}

	// Ends the task if the objective changed while this task was paused (ie: the bot was collecting items).
	const CZPSObjectiveManager& mgr = CZombiePanicSourceMod::GetZPSMod()->GetObjectiveManager();

	if (mgr.GetCurrentObjective() != CZPSObjectiveManager::ObjectiveTypes::OBJECTIVE_USE_BUTTON || mgr.GetUseButton() != button)
	{
		return Done("Task is no longer valid!");
	}

	Vector eyePos = bot->GetEyeOrigin();
	Vector buttonPos = UtilHelpers::getWorldSpaceCenter(button);
	m_rangeToButton = (buttonPos - eyePos).Length();

	// another survivor bot is nearer the button, hold position and let it press
	if (m_rangeToButton <= COMPETE_RADIUS && IsAnotherBotCloser(bot, buttonPos))
	{
		m_waiting = true;
		m_timeout.Invalidate();
		bot->GetControlInterface()->AimAt(buttonPos, IPlayerController::LOOK_INTERESTING, 0.5f, "Waiting for teammate to use the button.");
		return Continue();
	}

	m_waiting = false;

	if (m_rangeToButton <= CBaseExtPlayer::PLAYER_USE_RADIUS)
	{
		Vector usePos;
		UtilHelpers::math::CalcClosestPointOfEntity(button, eyePos, usePos);

		IPlayerController* input = bot->GetControlInterface();
		input->AimAt(buttonPos, IPlayerController::LOOK_PRIORITY, 0.5f, "Looking at USE entity!");
		CBaseEntity* obstruction = nullptr;

		if (!m_timeout.HasStarted())
		{
			m_timeout.Start(5.0f);
		}

		if (input->IsAimOnTarget() && !modhelpers->IsUseObstructed(bot->GetEntity(), button, &obstruction, &usePos))
		{
			input->PressUseButton();
			return Done("Use button pressed!");
		}

		if (obstruction)
		{
			if (bot->IsDebugging(BOTDEBUG_TASKS))
			{
				bot->DebugPrintToConsole(255, 255, 0, "%s OBJECTIVE USE BUTTON: +USE IS OBSTRUCTED BY \"%s\"! \n",
					bot->GetDebugIdentifier(), UtilHelpers::textformat::FormatEntity(obstruction));
			}

			if (bot->IsAbleToBreak(obstruction))
			{
				m_timeout.Invalidate();
				bot->GetMovementInterface()->BreakObstacle(obstruction);
				return Continue();
			}
		}
	}

	if (m_nav.NeedsRepath())
	{
		CZPSBotPathCost cost(bot);

		if (!m_nav.ComputePathToPosition(bot, buttonPos, cost))
		{
			if (m_counter.Increase())
			{
				return Done("Too many pathing failures!");
			}
		}
	}

	m_nav.Update(bot);
	return Continue();
}

TaskEventResponseResult<CZPSBot> CZPSBotObjectiveUseButtonTask::OnStuck(CZPSBot* bot)
{
	m_nav.Invalidate();

	if (m_counter.Increase())
	{
		return TryDone(PRIORITY_CRITICAL, "Too many pathing failures!");
	}

	return TryToMaintain(PRIORITY_MEDIUM);
}

TaskEventResponseResult<CZPSBot> CZPSBotObjectiveUseButtonTask::OnMoveToFailure(CZPSBot* bot, CPath* path, IEventListener::MovementFailureType reason)
{
	if (m_counter.Increase())
	{
		return TryDone(PRIORITY_CRITICAL, "Too many pathing failures!");
	}

	return TryToMaintain(PRIORITY_MEDIUM);
}

QueryAnswerType CZPSBotObjectiveUseButtonTask::ShouldAttack(CBaseBot* me, const CKnownEntity* them)
{
	if (!m_waiting && m_rangeToButton <= COMPETE_RADIUS)
	{
		return ANSWER_NO;
	}

	return ANSWER_UNDEFINED;
}

QueryAnswerType CZPSBotObjectiveUseButtonTask::ShouldHurry(CBaseBot* me)
{
	if (!m_waiting && m_rangeToButton <= COMPETE_RADIUS)
	{
		return ANSWER_YES;
	}

	return ANSWER_UNDEFINED;
}