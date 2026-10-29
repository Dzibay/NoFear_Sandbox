# ИИ ботов: настройка в редакторе

C++ модуль `NoFear` (`Source/NoFear`) добавляет:

| Класс | Где искать в редакторе | Что делает |
|---|---|---|
| `UNoFearCombatComponent` | Add Component → **NoFear Combat** | Видимость цели, время реакции, точность, подавление. Пишет состояние в блэкборд |
| `UNoFearCombatSubsystem` | ноды **Report Shot**, **Compute AI Shot End**, **Find Combat Component** | Подавление от пролетающих пуль, выстрел бота с разбросом |
| `UEnvQueryContext_NoFearThreat` | контекст **NoFear Threat** в EQS | От кого прятаться: цель, последний стрелявший или последняя известная позиция |
| `UEnvQueryTest_NoFearCover` | тест **NoFear: Cover** в EQS | Оценка укрытия: полное, низкое или его нет |

## 0. Сборка

Компилятор MSVC не понимает кириллицу в пути проекта (`C:\Users\долбаеб\...`): UBT пишет пути в response-файлы в UTF-8, а `cl.exe` читает их в cp1251. Поэтому проект открывается и собирается через ссылку с латинским путём:

```
C:\NoFear_Sandbox  →  C:\Users\долбаеб\Desktop\NoFear_Sandbox   (junction)
```

- Открывать редактор только через `C:\NoFear_Sandbox\NoFear_Sandbox_UE_Project\NoFear_Sandbox_UE_Project.uproject` — иначе Live Coding и пересборка из редактора упадут.
- Нужны компоненты Build Tools 2022: MSVC v143 (14.44), Windows 11 SDK 10.0.22621, **.NET Framework 4.8 SDK**.
- Ссылку можно удалить командой `rmdir C:\NoFear_Sandbox` — удаляется только ссылка, файлы проекта остаются.

## 1. NavMesh на уровне

1. Открыть `Content/Levels/DefaultLevel`.
2. Place Actors → Volumes → **Nav Mesh Bounds Volume**. Растянуть на весь уровень с запасом по высоте.
3. Нажать **P** — проходимая область подсветится зелёным. Проверить, что навмеш есть на всех площадках, где ходят боты.
4. Если блоки уровня двигаются во время игры: Project Settings → Navigation Mesh → Runtime Generation = **Dynamic**.

## 2. Персонажи

**CBP_BaseBotCharacter**
1. Add Component → **NoFear Combat**. `Team Id` = 1 (NPC).
2. Компонент сам включает `Use Acceleration For Paths` у Character Movement — без этого motion matching не видит ускорения при движении по пути.

**BP_BaseMonster** (компонент не нужен)
1. Character Movement → Nav Movement → включить **Use Acceleration For Paths**.

## 3. Блэкборд BB_BaseBot

Добавить ключи (имена точно такие — компонент ищет их по имени):

| Ключ | Тип |
|---|---|
| `HasLineOfSight` | Bool |
| `CanFire` | Bool |
| `IsSuppressed` | Bool |
| `Suppression` | Float |
| `LastKnownLocation` | Vector |
| `CoverLocation` | Vector |

## 4. EQS-запрос укрытия

Content Browser → Artificial Intelligence → Environment Query → `EQ_FindCover`.

- Генератор **Points: Pathing Grid**: Generate Around = Querier, Grid Half Size = 1500, Space Between = 150.
- Тест **Distance**: Distance To = **NoFear Threat**, Test Purpose = Filter Only, Filter Type = Minimum, Float Value Min = 500 (не прятаться вплотную к врагу).
- Тест **Distance**: Distance To = Querier, Test Purpose = Score Only, Scoring Factor = **-1** (ближе — лучше).
- Тест **NoFear: Cover**: настройки по умолчанию (фильтр от 0.1 и оценка).

Проверка: поставить на уровень `EQSTestingPawn`, указать ему запрос — увидите точки и их оценки.

## 5. Перемещение по NavMesh

В **BT_BaseBot** и **BT_BaseMonster** заменить `BTT_MoveToLocation` и `BTT_MoveToTarget` на встроенный **Move To**:
- вместо `BTT_MoveToLocation` — Move To с ключом `InvestigationLocation`, Acceptable Radius = 100;
- вместо `BTT_MoveToTarget` (монстры) — Move To с ключом `AttackTarget`, Acceptable Radius = 200, **Observe Blackboard Value** включён — монстр преследует движущуюся цель.

Старые `SetMovingTarget` и таймер с `AddMovementInput` в персонажах после этого не используются.

## 6. Ветка боя в BT_BaseBot

Ветку Battle (декоратор `State == Battle`) сделать Selector-ом из четырёх веток по приоритету:

```
Battle (Selector)
├── Укрыться        [Blackboard: IsSuppressed Is Set, Observer Aborts = Lower Priority]
│   ├── BTT_Bot_StopFire
│   ├── Run EQS Query: EQ_FindCover → CoverLocation (Single Best Item)
│   ├── Move To: CoverLocation (Acceptable Radius 50, Allow Strafe)
│   └── Wait 2.0 ± 1.0
├── Перезарядка     [BTD_Bot_NeedReloadWeapon]
│   ├── BTT_Bot_StopFire
│   ├── Run EQS Query: EQ_FindCover → CoverLocation
│   ├── Move To: CoverLocation
│   └── BTT_Bot_ReloadWeapon
├── Огонь           [Blackboard: CanFire Is Set, Observer Aborts = Both]
│   ├── (существующие Equip / SetFocus / ChangeAim)
│   ├── BTT_Bot_StartFire
│   ├── Wait 0.6 ± 0.4      ← длина очереди
│   ├── BTT_Bot_StopFire
│   └── Wait 0.4 ± 0.3      ← пауза между очередями
└── Сблизиться
    ├── BTT_Bot_StopFire
    └── Move To: LastKnownLocation (Acceptable Radius 300)
```

`BTT_Bot_StopFire` в начале остальных веток нужен потому, что при прерывании ветки «Огонь» оружие иначе продолжит стрелять. Надёжнее ещё и добавить в `BTT_Bot_StartFire` событие **Receive Abort AI** → StopFireAttack → Finish Abort.

## 7. BP_Weapon → FireTracing

**Выстрел бота.** В ветке ИИ конечную точку трассировки считать нодой **Compute AI Shot End**:
- Shooter = Self (оружие; компонент найдётся у владельца);
- Muzzle Location = позиция сокета `Muzzle`;
- Range = дальность оружия;
- Fallback Aim Point = позиция `GetAttackTarget` (на случай, если у стрелка нет NoFear Combat).

Результат подать в `End` у `LineTraceSingle`. Случайный разброс из BP для ИИ убрать, иначе разброс посчитается дважды.

**Подавление.** Сразу после `LineTraceSingle` (и для игрока, и для ботов) вызвать **Report Shot**:
- Shooter = Self, Trace Start / Trace End — те же, что у трассировки, Hit = Out Hit.
- У дробовика трассировка идёт на каждую дробину — для него в продвинутых пинах поставить Suppression Amount = 0.05.

## 8. Реакции (по желанию)

В CBP_BaseBotCharacter у компонента NoFear Combat есть события:
- **On Under Fire** (Shooter, Shot Origin, Suppression Added) — если бот в Idle, перевести AIC_BaseBot в Investigating с `InvestigationLocation = Shot Origin`: бот пойдёт проверить, откуда стреляли.
- **On Suppressed Changed** — анимация вздрагивания, звук, присед.

## Настройки точности (NoFear Combat → Accuracy)

| Параметр | По умолчанию | Смысл |
|---|---|---|
| Base Spread Degrees | 1.2 | Разброс в идеальных условиях |
| Acquire Spread Multiplier | 3 | Во сколько раз хуже первые выстрелы после обнаружения |
| Aim Settle Time | 1.5 с | За сколько бот прицеливается полностью |
| Target Moving Spread | 1 | +100 % разброса по цели, бегущей 6 м/с |
| Self Moving Spread | 0.8 | +80 % разброса, когда бежит сам бот |
| Effective Range | 1500 | Дальше разброс растёт дополнительно |
| Suppressed Spread Multiplier | 2.5 | Разброс при полном подавлении |
| Tracking Lag | 0.15 с | Прицел запаздывает за движущейся целью |
| Headshot Chance | 0.08 | Доля выстрелов в голову |
| Reaction Time Min / Max | 0.25 / 0.6 с | Задержка перед первым выстрелом |

Пуля рядом сбивает прицеливание. Подавленный бот реагирует медленнее.

## Отладка

Во время игры клавиша **'** (на русской раскладке — **Э**) открывает Gameplay Debugger: дерево, блэкборд, EQS, восприятие выбранного бота.
