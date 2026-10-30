#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "NoFearMonsterAIController.generated.h"

class UNoFearMeleeComponent;

/**
 * Монстр: бродит вокруг дома, заметив врага — бежит к нему и бьёт с рывком (UNoFearMeleeComponent).
 * Обычные и агрессивные монстры отличаются только командой (Monster / AggroMonster) и цветом тела.
 * Дерево: BT_NoFear_Monster.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearMonsterAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearMeleeComponent* GetMeleeComponent() const;

	/** Если у тела нет UNoFearMeleeComponent, добавить его с настройками по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	bool bAddMeleeComponentIfMissing = true;

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
