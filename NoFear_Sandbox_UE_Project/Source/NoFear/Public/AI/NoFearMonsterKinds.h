#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearMonsterAIController.h"
#include "NoFearMonsterKinds.generated.h"

/**
 * Прыгун: мелкий и быстрый. Гонится спринтом, кружит дальше (600–850 см), нападает прыжком издалека
 * (UNoFearMonsterAttack_Leap) и после приземления отскакивает назад. Почуяв, что на него навели оружие, отпрыгивает в сторону.
 * Тело: CBP_Bot_Jumper.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearJumperAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearJumperAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Отпрыгивать в сторону, когда на прыгуна смотрит ствол врага. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge")
	bool bDodgeWhenAimedAt = true;

	/** Ствол смотрит на прыгуна точнее этого угла — уклоняется, градусы (вблизи угол шире — по ширине тела). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0", ClampMax = "45"))
	float DodgeAimAngle = 6.f;

	/** Дальше этого от стрелка не уклоняется, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeMaxDistance = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeSpeed = 750.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeUpSpeed = 250.f;

	/** Уклоняется не чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeCooldown = 2.5f;

	/** Сколько раз уклонился с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Dodge")
	int32 GetDodgeCount() const { return DodgeCount; }

	/** Уклониться прочь с линии прицела From (вбок от направления AimDirection). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Dodge")
	bool Dodge(FVector AimDirection);

protected:
	virtual void UpdateBot() override;
	virtual void UpdateMovementStance() override;

private:
	/** Враг с оружием в руках, который сейчас целится в прыгуна. Возвращает направление его прицела. */
	bool FindAimingThreat(FVector& OutAimDirection) const;

	int32 DodgeCount = 0;
	double NextDodgeTime = 0.0;
};

/**
 * Плевун: держится на 800–1500 см и плюёт по дуге кислотой (UNoFearMonsterAttack_Spit): снаряд ранит при попадании
 * и оставляет лужу, которая жжёт стоящих в ней. Не стоит в очереди за жетоном. Цель ближе RetreatStartDistance —
 * отступает (ключ KeepAway, ветка дерева «Отступить»); прижали — отбивается слабым ударом.
 * Тело: CBP_Bot_Spitter.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearSpitterAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearSpitterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Цель ближе этого — отступать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float RetreatStartDistance = 700.f;

	/** Отступает, пока цель не дальше этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float RetreatStopDistance = 950.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	bool IsKeepingAway() const { return bKeepingAway; }

	/** Сколько раз начинал отступать. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	int32 GetRetreatCount() const { return RetreatCount; }

protected:
	virtual void UpdateBot() override;
	virtual void WriteBlackboard() override;
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee) override;

private:
	bool bKeepingAway = false;
	int32 RetreatCount = 0;
};

/**
 * Танк: большой, медленный (всегда шагом), много здоровья. Широкий удар с отбрасыванием и таран издалека
 * (UNoFearMonsterAttack_Charge): врезавшись в стену, оглушён на секунду.
 * Тело: CBP_Bot_Tank.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearTankAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearTankAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual bool ShouldWalk() const override { return true; }
};
