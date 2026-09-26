#ifndef __NAVBOT_ZPSBOT_TASKS_SCENARIO_HUMAN_OBJECTIVES_H_
#define __NAVBOT_ZPSBOT_TASKS_SCENARIO_HUMAN_OBJECTIVES_H_

class CZPSBotObjectiveFindItemTask : public AITask<CZPSBot>
{
public:
	AITask<CZPSBot>* InitialNextTask(CZPSBot* bot) override;

	TaskResult<CZPSBot> OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask) override;
	TaskResult<CZPSBot> OnTaskUpdate(CZPSBot* bot) override;

	QueryAnswerType ShouldPickup(CBaseBot* me, CBaseEntity* item) override;

	const char* GetName() const override { return "ObjectiveFindItem"; }

private:
	CountdownTimer m_nextScanTimer;
	
	bool HasSpaceInInventory(CZPSBot* bot) const;
};

class CZPSBotObjectiveUseItemTask : public AITask<CZPSBot>
{
public:
	CZPSBotObjectiveUseItemTask()
	{
		m_usingItem = false;
	}

	static bool IsPossible(CZPSBot* bot);

	TaskResult<CZPSBot> OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask) override;
	TaskResult<CZPSBot> OnTaskUpdate(CZPSBot* bot) override;

	QueryAnswerType ShouldPickup(CBaseBot* me, CBaseEntity* item) override;
	// Don't attack enemies if we're using the item
	QueryAnswerType ShouldAttack(CBaseBot* me, const CKnownEntity* them) override { return m_usingItem ? ANSWER_NO : ANSWER_YES; }
	QueryAnswerType ShouldHurry(CBaseBot* me) override { return m_usingItem ? ANSWER_YES : ANSWER_UNDEFINED; }
	QueryAnswerType ShouldSwitchToWeapon(CBaseBot* me, const CBotWeapon* weapon) override { return m_usingItem ? ANSWER_NO : ANSWER_UNDEFINED; }

	const char* GetName() const override { return "ObjectiveUseItem"; }

private:
	CMeshNavigator m_nav;
	bool m_usingItem;
	CountdownTimer m_switchDelay;

	bool IsItemEquipped(CZPSBot* bot) const;
	void EquipRequiredItem(CZPSBot* bot);
};

class CZPSBotObjectiveFollowItemCarrierTask : public AITask<CZPSBot>
{
public:
	static bool IsPossible(CZPSBot* bot, CBaseEntity** carrier);

	CZPSBotObjectiveFollowItemCarrierTask(CBaseEntity* carrier) :
		m_carrier(carrier)
	{
	}

	AITask<CZPSBot>* InitialNextTask(CZPSBot* bot) override;

	TaskResult<CZPSBot> OnTaskUpdate(CZPSBot* bot) override;

	QueryAnswerType ShouldAttack(CBaseBot* me, const CKnownEntity* them) override { return ANSWER_YES; }
	QueryAnswerType ShouldSwitchToWeapon(CBaseBot* me, const CBotWeapon* weapon) override { return ANSWER_YES; }

	const char* GetName() const override { return "ObjectiveFollowItemCarrier"; }

private:
	CHandle<CBaseEntity> m_carrier;
	CountdownTimer m_checkInventory;
};

class CZPSBotObjectiveDropItemTask : public AITask<CZPSBot>
{
public:
	static bool IsPossible(CZPSBot* bot);

	TaskResult<CZPSBot> OnTaskUpdate(CZPSBot* bot) override;

	const char* GetName() const override { return "ObjectiveDropItem"; }

private:
	CMeshNavigator m_nav;
	CountdownTimer m_switchCooldown;

	bool OwnsRequiredItem(CZPSBot* bot) const;
	bool IsItemEquipped(CZPSBot* bot) const;
	void EquipRequiredItem(CZPSBot* bot);
};

// Press the objective button. Only the survivor bot nearest to the button presses it, the others hold position.
class CZPSBotObjectiveUseButtonTask : public AITask<CZPSBot>
{
public:
	CZPSBotObjectiveUseButtonTask(CBaseEntity* button) :
		m_button(button)
	{
		m_rangeToButton = std::numeric_limits<float>::max();
		m_waiting = false;
	}

	// Radius around the button where other survivor bots are counted as competing for it.
	static constexpr float COMPETE_RADIUS = 256.0f;

	TaskResult<CZPSBot> OnTaskStart(CZPSBot* bot, AITask<CZPSBot>* pastTask) override;
	TaskResult<CZPSBot> OnTaskUpdate(CZPSBot* bot) override;
	void OnTaskEnd(CZPSBot* bot, AITask<CZPSBot>* nextTask) override;
	bool OnTaskPause(CZPSBot* bot, AITask<CZPSBot>* nextTask) override;
	TaskResult<CZPSBot> OnTaskResume(CZPSBot* bot, AITask<CZPSBot>* pastTask) override;

	TaskEventResponseResult<CZPSBot> OnStuck(CZPSBot* bot) override;
	TaskEventResponseResult<CZPSBot> OnMoveToFailure(CZPSBot* bot, CPath* path, IEventListener::MovementFailureType reason) override;

	QueryAnswerType ShouldAttack(CBaseBot* me, const CKnownEntity* them) override;
	QueryAnswerType ShouldHurry(CBaseBot* me) override;

	const char* GetName() const override { return "ObjectiveUseButton"; }

private:
	CHandle<CBaseEntity> m_button;
	CMeshNavigator m_nav;
	CPathFailCounter m_counter;
	CountdownTimer m_timeout;
	float m_rangeToButton;
	bool m_waiting;

	// Client indexes of bots currently running this task.
	static inline std::vector<int> s_activeBots;

	static void AddActiveBot(int client);
	static void RemoveActiveBot(int client);
	static bool IsActiveBot(int client);
	bool IsAnotherBotCloser(CZPSBot* bot, const Vector& buttonPos) const;
};

#endif // !__NAVBOT_ZPSBOT_TASKS_SCENARIO_HUMAN_OBJECTIVES_H_