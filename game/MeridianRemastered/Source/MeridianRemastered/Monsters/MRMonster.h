#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/MRSpriteAppearance.h"
#include "MRMonster.generated.h"

class UMRSpriteBodyComponent;
struct FMRMonsterDef;

/**
 * A monster or NPC, drawn the original game's way (docs/sprites.md "Monsters"): the same sprite
 * body as players, with the class's look and its own Kod animations (stand, walk, attack), and
 * its corpse when it dies. Spawned by UMRMonsterSubsystem from the original spawn tables.
 *
 * Server-authoritative. The AI is a stand-in until combat exists:
 * - NPCs (AI_NOMOVE) stand where Kod puts them.
 * - Monsters idle and wander near their spawn point.
 * - Aggressive ones (AI_FIGHT_AGGRESSIVE: mummies) chase a player they can see and attack,
 *   playing the attack animation and the original sound; so does a monster that has been hit.
 * - Attacks deal no damage yet. Three player attacks kill a monster (a placeholder): it becomes
 *   its corpse for mr.Monster.CorpseSeconds.
 */
UCLASS()
class MERIDIANREMASTERED_API AMRMonster : public ACharacter
{
	GENERATED_BODY()

public:
	AMRMonster(const FObjectInitializer& ObjectInitializer);

	/** Server, right after spawning: which class (data/monsters.json) and its home zone. */
	void InitMonster(FName InClass, int32 InZone);

	FName GetMonsterClass() const { return MonsterClass; }
	FName GetLook() const { return Look; }
	int32 GetZone() const { return Zone; }
	bool IsDead() const { return bDead; }
	bool IsNpc() const;
	UMRSpriteBodyComponent* GetSpriteBody() const { return SpriteBody; }
	const FMRSpriteActionState& GetActionState() const { return Action; }

	/** Server: a player's attack landed (placeholder until combat). */
	void TakePlaceholderHit(AActor* From);

	/** Server: play an action (attack, ...) on every client; "aware" and "die" only sound. */
	void PlayMonsterAction(FName Action);

	/** Tests (the monster line-up): no AI, just the given action looping. */
	void SetAIEnabled(bool bEnabled) { bAI = bEnabled; }
	/** Server: turn into the corpse now. */
	void Die();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	FName Look;

	UPROPERTY(Replicated)
	FName MonsterClass;

	UPROPERTY(ReplicatedUsing = OnRep_Action)
	FMRSpriteActionState Action;

	UPROPERTY(Replicated)
	bool bDead = false;

	UPROPERTY(Transient)
	TObjectPtr<UMRSpriteBodyComponent> SpriteBody;

	UFUNCTION()
	void OnRep_Look();
	UFUNCTION()
	void OnRep_Action();

private:
	const FMRMonsterDef* Def() const;
	void ApplyLook();
	void ServerThink(float DeltaSeconds);
	APawn* FindTarget() const;
	void PlaySound(FName Which) const;

	enum class EState : uint8 { Idle, Wander, Chase };
	EState State = EState::Idle;
	float StateTime = 0.f;
	float ThinkTimer = 0.f;
	float AttackCooldown = 0.f;
	float StuckTime = 0.f;
	FVector Home = FVector::ZeroVector;
	FVector WanderTo = FVector::ZeroVector;
	TWeakObjectPtr<APawn> Target;
	int32 Zone = 0;
	int32 Hits = 0;
	bool bAI = true;
	bool bProvoked = false;
};
