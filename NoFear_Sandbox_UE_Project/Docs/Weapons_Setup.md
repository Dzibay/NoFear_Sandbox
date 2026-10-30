# Оружие: настройка в редакторе

Код оружия на C++:

- `UNoFearWeaponData` — Data Asset со всеми параметрами ствола.
- `ANoFearWeapon` — актор оружия. Его создаёт компонент, руками на уровень ставить не нужно.
- `UNoFearWeaponComponent` — уже стоит на `ANoFearCharacter`, а значит и на `CBP_SandboxCharacter`. Компонент выдаёт оружие, переключает его, принимает ввод, ставит позу GASP (`OverlayState`) и каждый кадр передаёт отдачу рук в анимблюпринт.

Почему больше не залипает: огонь привязан к **Started / Completed / Canceled**, а не к Triggered. Очередь стреляет по одному таймеру, и отпускание кнопки всегда его гасит. Плюс спуск отпускается при смене контроллера и при смене оружия.

## Что вернулось из старой версии

Старая система (коммит `d4bcd02`) состояла из `BP_Weapon*`, `CBP_SandboxCharacter` и `ABP_SandboxCharacter`. Вот что из неё сохранилось и как работает теперь:

| Что | Где было | Как теперь |
|---|---|---|
| Позы держания (винтовка, пистолет в одной руке) | Оверлеи GASP `OverlaySystem/Overlays/*`, выбор через `OverlayState` | То же самое. Значение ставит компонент из поля **Overlay State** в Data Asset'е. |
| Перезарядка | `AM_Character*_Reload_Montage` в слоте `UpperBody` | То же самое: поле **Character Reload Montage**. |
| Затвор и магазин самого ствола | `AM_Rifle_Fire_Montage` и др. на меше оружия | Поля **Weapon Fire / Reload Animation**: ставятся `A_*_Fire` / `A_*_Reload`. |
| Рывок рук при выстреле | `ABP_SandboxCharacter`: узлы Modify Bone на upperarm_r / lowerarm_r / hand_r и spine_01–03, сила `Recoil_Amount` | Те же узлы. Повороты костей берутся из Data Asset'а (**Recoil → Arms**), силу считает C++ по той же кривой: пик через 0.05 с, покой к 0.5 с. |
| Оружие на спине и поясе | `SocketPassive` у каждого `BP_Weapon_*` | Поле **Holster Socket**. Убранное оружие теперь висит на теле, а не пропадает. |

`ABP_SandboxCharacter` уже возвращён из `d4bcd02` вместе со слотом `UpperBody` и узлами отдачи. Ему нужна маленькая починка, см. шаг 5.

---

## 0. Пересобрать редактор

Если C++ менялся при открытом редакторе, **закрой** редактор и собери Editor-таргет. Потом открой проект снова через `C:\NoFear_Sandbox\...\NoFear_Sandbox_UE_Project.uproject`.

## 1. Ввод (папка `Content/Input`)

Создай Input Action (ПКМ → Input → Input Action):

| Ассет | Value Type |
|---|---|
| `IA_Fire` | Digital (bool) |
| `IA_Reload` | Digital (bool) |
| `IA_SwitchWeapon` | Axis1D (float) |
| `IA_Holster` | Digital (bool) |

`IA_Aim` уже есть и привязан к ПКМ в `IMC_Sandbox`, его используем как есть.

Создай Input Mapping Context `IMC_Weapon` с такими привязками:

- `IA_Fire` → **Left Mouse Button**, плюс Gamepad Right Trigger.
- `IA_Reload` → **R**.
- `IA_SwitchWeapon` → **Mouse Wheel Axis**.
- `IA_Holster` → **H** (X занят приседанием в рэгдолл: `IA_Ragdoll` в `IMC_Sandbox`).

**Triggers не добавляй**, оставь пустыми. Для огня это важно: никаких Hold и Pulse.

## 2. Data Asset'ы оружия (папка `Content/Weapons/Data`)

ПКМ → Miscellaneous → Data Asset → **NoFearWeaponData**. Создай `DA_Rifle`, `DA_Pistol` и `DA_Shotgun`.

В UE 5.8 пункта Data Asset может не быть в списке Miscellaneous: попробуй набрать «Data Asset» в строке поиска этого меню. Другой вариант: создать ассеты скриптом `Content/Weapons/Data` (уже сделано, значения из таблиц ниже в них внесены); тогда остаётся проверить поля и подправить на вкус.

### Вид, сокеты, анимации

Это значения из старой версии.

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Overlay State | `Rifle` | `Pistol1H` | `Rifle` |
| Mesh | `SM_Rifle` | `SM_Pistol` | `SM_Shotgun` |
| Attach Socket | `overlay_rifle` | `overlay_pistol` | `overlay_rifle` |
| Holster Socket | `spine_weapon_first_socket` | `pelvis_weapon_first_socket` | `spine_weapon_second_socket` |
| Character Fire Montage | — | — | — |
| Character Reload Montage | `AM_CharacterRifle_Reload_Montage` | `AM_CharacterPistol_Reload_Montage` | `AM_CharacterShotGun_Reload_Montage` |
| Character Equip Montage | `AM_CharacterRifle_Equip_Montage` | `AM_CharacterPistol_Equip_Montage` | `AM_CharacterRifle_Equip_Montage` |
| Character Holster Montage | — (играется доставание задом наперёд) | — | — |
| Weapon Fire Animation | `A_Rifle_Fire` | `A_Pistol_Fire` | `A_Shotgun_Fire` |
| Weapon Reload Animation | `A_Rifle_Reload` | `A_Pistol_Reload` | `A_Shotgun_Reload` |

- **Mesh**: это скелетный меш `SM_*` из `Content/Weapons/<тип>/Meshes` (не `SK_*`: это скелет, а `SM_*` в этом проекте — именно SkeletalMesh). Анимации `A_*_Fire` и `A_*_Reload` сделаны под его скелет `SK_*`, сокет `Muzzle` лежит на нём же.
- **Character Fire Montage** в старой версии не использовался: выстрел показывала процедурная отдача рук (ниже). Если захочется сильнее, можно поставить `AM_CharacterRifle_Fire_Montage` или `AM_CharacterPistol_Fire_Montage`, но на скорострельном автомате монтаж будет перезапускаться каждый выстрел.
- Пистолет в старой версии был **Pistol1H** (одной рукой). Если хочется двумя руками — `Pistol2H`.

### Отдача рук (раздел Recoil → Arms)

Повороты в градусах: Pitch, Yaw, Roll. Значения — как они реально работали в старой игре.

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Upper Arm Recoil | 1, -1, -2 | 4, 0, 0 | 2, -3, -5 |
| Lower Arm Recoil | -1, -1, 1 | 4, 3, 0 | -1, -1, 2 |
| Hand Recoil | 1, 2, 1 | 4, 3, 0 | 2, 4, 1 |
| Arm Recoil Scale | 1 | 1 | 1 |

- В старом `ABP_SandboxCharacter` предплечье и кисть были перепутаны местами (`LowerArm` брал значение из `Hand_R_Rotation` оружия и наоборот). В таблице это уже учтено, так что выглядеть будет так же, как раньше.
- Рука дёрнется слишком сильно или слабо — меняй **Arm Recoil Scale**. Хочешь другую форму рывка — создай Curve Float (время в секундах → 0..1) и поставь в **Arm Recoil Curve**.
- В прицеливании рывок умножается на **Aim Recoil Multiplier**.

### Стрельба и урон

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Fire Mode | Auto | Single | Single |
| Rounds Per Minute | 650 | 400 | 70 |
| Pellets Per Shot | 1 | 1 | 9 |
| Pellet Spread Degrees | — | — | 4 |
| Magazine Size | 30 | 12 | 6 |
| Starting / Max Reserve | 120 / 240 | 48 / 96 | 24 / 48 |
| Damage | 25 | 30 | 12 (на дробину) |
| Range | 15000 | 8000 | 4000 |
| Falloff Start / End | 3000 / 8000 | 1500 / 4000 | 800 / 2500 |
| Min Damage Multiplier | 0.5 | 0.5 | 0.2 |
| Hip / Aim Spread | 2.5 / 0.4 | 1.5 / 0.5 | 3 / 2 |
| Recoil Pitch | 0.6–0.9 | 1.2–1.6 | 3–4 |

### Общие поля

Одинаковые для всех трёх:

- **Fire Sound** — `SC_GunShot_Cue`.
- **Dry Fire Sound** — `SC_EmptyGunShot_Cue`.
- **Fire Camera Shake** — `BP_CameraShake_Recoil` (`Content/Blueprints/Cameras`).
- **Muzzle Flash** — `P_Muzzle` (Effects/MuzzleFlash) или `NS_WeaponFire` из Lyra.
- **Tracer** — `NS_WeaponFire_Tracer`, для дробовика `NS_WeaponFire_Tracer_Shotgun`. Точки попадания код передаёт сам.
- **Default Impact**:
  - Effect: `P_Concrete` (Effects/Particles/Hit).
  - Decal: `M_BulletHole` или `MI_Decal_Concrete`.
- **Surface Impacts** (по желанию): разные эффекты по типу поверхности, например Wood → `P_Wood`. Типы поверхностей задаются в Project Settings → Physics.

Если ствол сидит в руке криво, подстрой **Attach Offset**: Location и Rotation относительно сокета. Для оружия на спине — **Holster Offset**.

## 3. Персонаж (`CBP_SandboxCharacter`)

1. Убедись, что **Parent Class = NoFearCharacter** (File → Reparent Blueprint), и сохрани (Ctrl+S).
2. В Components выбери **WeaponComponent** и заполни:
   - **Starting Weapons**: `DA_Rifle`, `DA_Pistol`, `DA_Shotgun`.
   - **Input Mapping**: `IMC_Weapon`.
   - **Fire Action**: `IA_Fire`.
   - **Aim Action**: `IA_Aim`.
   - **Reload Action**: `IA_Reload`.
   - **Switch Weapon Action**: `IA_SwitchWeapon`.
   - **Holster Action**: `IA_Holster`.
3. Поза меняется сама: компонент пишет в переменную `OverlayState` и вызывает `OnRep_OverlayState`. Если появится предупреждение «нет позы …», проверь, что имя в Data Asset'е совпадает с пунктом `Enum_OverlayState`.
4. Раздел **Weapons → Animation** трогать не нужно: имена переменных (`Recoil_Amount`, `UpperArm_R_Rotation`, `LowerArm_R_Rotations`, `Hand_R_Rotations`) уже совпадают с `ABP_SandboxCharacter`.

## 4. Убрать «бутафорское» оружие оверлея

Оверлей GASP сам вешает в руку модельку (M4A1 / M9), и получилось бы два ствола. Открой:

- `OverlaySystem/Overlays/Rifle/DA_Overlay_Rifle`
- `OverlaySystem/Overlays/Pistol1H/DA_Overlay_Pistol1H`
- `OverlaySystem/Overlays/Pistol2H/DA_Overlay_Pistol2H`

В каждом очисти поле **Mesh** (или Static/Skeletal Mesh): кнопка ⟲ или выбрать None.

## 5. Починить `ABP_SandboxCharacter`

Анимблюпринт вернулся из старой версии. Слот `UpperBody` и узлы отдачи уже на месте. Сломались только узлы, которые читали старые `CBP_SandboxCharacter.Recoil_Amount` и `BP_Weapon`: этих классов больше нет, теперь значения пишет C++.

### 5.1. EventGraph: удалить старое чтение отдачи

1. Открой `ABP_SandboxCharacter` и нажми **Compile**. В Compiler Results будут ошибки про `Recoil_Amount`, `SelectedItemInHand` и `BP_Weapon`.
2. В **EventGraph** найди **Event Blueprint Update Animation** → Branch → **Sequence**.
3. Удали всё, что висит на выходах **Then 0** и **Then 1**:
   - Cast To CBP_SandboxCharacter;
   - два **Is Valid** (макрос с ромбиком);
   - Set **Recoil_Amount** и серый узел Recoil_Amount (из персонажа);
   - **Selected Item In Hand**;
   - Cast To **BP_Weapon**;
   - три Set: **UpperArm_R_Rotation**, **LowerArm_R_Rotations**, **Hand_R_Rotations**, и их геттеры.
4. **Then 2** (Update_CVarDrivenVariables → Update_Logic) не трогай.

Сами переменные `Recoil_Amount`, `UpperArm_R_Rotation`, `LowerArm_R_Rotations` и `Hand_R_Rotations` в панели **My Blueprint** **не удаляй**: в них пишет C++.

### 5.2. AnimGraph: здесь ничего менять не нужно

Узлы отдачи уже подключены правильно. Найди в **AnimGraph** рамку «Don't Apply Hand IK if we are playing a Montage or InAir»: в ней стоит **Blend Poses by bool**.

- **False Pose** — цепочка **Modify Bone** (upperarm_r → lowerarm_r → hand_r → spine_01 → spine_02 → spine_03). Это отдача.
- **True Pose** — `Use cached pose 'PreHandIK'`, то есть поза без отдачи.
- **Active Value** — условие «Is Slot Active (DefaultSlot) OR MovementModeLastFrame == InAir». Оно истинно только во время траверсалов (слот `DefaultSlot`) и в воздухе. При обычной стрельбе и перезарядке (слот `UpperBody`) оно ложно, поэтому работает цепочка отдачи.

**Не трогай Active Value и не ставь галочку.** Если поставить true, отдача отключится совсем.

Без выстрела `Recoil_Amount` = 0, и узлы Modify Bone ничего не меняют.

### 5.3. Сохранить

**Compile** — ошибок быть не должно. **Save**.

## 6. Проверка

| Клавиша | Что делает |
|---|---|
| ЛКМ | Огонь. Автомат стреляет, пока держишь, и сразу перестаёт, когда отпускаешь. Правая рука и корпус коротко дёргаются на каждом выстреле. |
| ПКМ | Прицел (пока держишь): камера GASP подъезжает к плечу и сужает поле зрения до 60, разброс, отдача и рывок рук меньше, перекрестие сходится. |
| R | Перезарядка с анимацией. При пустом магазине и зажатом огне — сама. |
| Колесо | Смена оружия. Убранные стволы висят на спине и на поясе. |
| H | Убрать / достать. |

Боты стреляют через тот же код: `WeaponComponent → StartFire / StopFire`. Точность бота берётся из `NoFearCombatComponent`. Рывок рук и оружие на спине у ботов тоже есть.

## 7. Перекрестие

Рисует `ANoFearHUD` (C++), он выставлен в `ANoFearGameMode`, поэтому в редакторе делать ничего не нужно.

- Стоит в центре экрана: именно туда летит пуля.
- Зазор между чёрточками равен настоящему разбросу оружия: расходится при беге, прыжке и стрельбе, сходится при прицеливании.
- Краснеет, когда в центре враг (команда считается через `UNoFearTeamLibrary::AreHostile`).
- Бледнеет при перезарядке и доставании, пропадает, когда оружие убрано.

Подстроить вид можно в Class Defaults у `ANoFearHUD` (создай Blueprint-наследник и поставь его в World Settings → HUD Class) или поменять значения по умолчанию в `NoFearHUD.h`: цвета, длина и толщина чёрточек, минимальный зазор.

## 8. Камера при прицеливании

Камеру ведёт GASP (`CameraAsset_SandboxCharacter`): в `CBP_SandboxCharacter` она вызывается через `IA_Aim → WantsToAim → риг Aim`. В проекте остались старые `Camera(NotUsedByDefault)` и `SpringArm`, они были активны и перебивали камеру GASP, поэтому прицел ничего не менял. Теперь `ANoFearCharacter::BeginPlay` гасит старую камеру, если на персонаже есть `GameplayCamera` (флаг `bDisableLegacySpringArmCamera`, по умолчанию включён). Делать в редакторе ничего не нужно.

## 9. Звуки и анимации перезарядки, доставания и убирания

Всё настраивается в `DA_*` и уже заполнено (звуки из Lyra, `Content/External/LyraContent/Audio/Sounds`). В редакторе делать ничего не нужно; ниже то, что можно подкрутить.

### Звуки

Раздел **Effects** в Data Asset. Каждое поле — список записей `Sound / Time / Volume Multiplier`: звук стартует через `Time` секунд после начала действия (0 — сразу).

| Поле | DA_Rifle | DA_Pistol | DA_Shotgun |
|---|---|---|---|
| Reload Sounds | ClipOut 0.45 с, ClipIn 0.95 с, `Rifle2_Mech_01` 1.55 с | ClipOut 0.4 с, ClipIn 0.95 с, Slide 1.45 с | ClipOut 0.6 с, ClipIn 1.25 с, `Shotgun_Mech_02` 2.0 с |
| Equip Sounds | `Lyra_WeaponSwap_01` 0.05 с | `Lyra_WeaponSwap_03` 0.4 с (в момент хвата) | `Lyra_WeaponSwap_01` 0.05 с |
| Holster Sounds | `Lyra_WeaponSwap_02` 0 с | `Lyra_WeaponSwap_02` 0.1 с | `Lyra_WeaponSwap_03` 0 с |

- Звуки магазина и затвора в исходниках тихие (громкость ассета 0.15–0.2), поэтому у них **Volume Multiplier** 2.5–3. Foley доставания/убирания громкий, ему поставлено 0.4–0.5.
- Тайминги подогнаны под монтажи: `ClipOut` — когда рука вынимает магазин, `ClipIn` — когда вставляет. Сменил монтаж перезарядки — сдвинь `Time`.
- Звук привязан к стволу и затухает по **Handling Attenuation** (`ATT_Default`). Перезарядка и смена оружия прерываются вместе со своими звуками: недоигранные звуки не звучат.

### Убирание оружия

Убирание теперь не мгновенное: ствол остаётся в руке, пока играет анимация, и только после неё уходит в кобуру (состояние `Holstering`, в нём нельзя стрелять, перезаряжаться и целиться).

- **Character Holster Montage** — монтаж убирания. Если поля пустое, берётся **Character Equip Montage**, проигранный задом наперёд.
- **Holster Time** — за сколько секунд проиграть убирание (монтаж растягивается/сжимается). 0 — по длине монтажа. Сейчас: автомат и дробовик 0.8 с, пистолет 0.7 с.
- **Equip Grab Time** — через сколько секунд после начала доставания ствол перескакивает из кобуры в руку. 0 — сразу. Пистолет 0.5 с (рука берёт его из кобуры на 0.5 с монтажа), у автомата и дробовика ствол на спине и сразу в руке.
- **Holster Stow Time** — через сколько секунд убирания ствол перескакивает из руки в кобуру. 0 — в самом конце. Пистолет 0.45 с: ровно тогда, когда при обратном проигрывании доставания рука отпускает ствол.
- Если убрать оружие, пока оно ещё достаётся, убирание начнётся с того места, где остановилось доставание, и займёт пропорционально меньше времени.

### Смена оружия

`UNoFearWeaponComponent` сначала убирает текущее оружие, потом достаёт новое. Во время убирания можно запросить другое оружие или убрать совсем: сработает **последний** запрос. Колесо мыши листает от «целевого» оружия, а не от того, что в руках, поэтому быстрая прокрутка не залипает. Поза персонажа (Overlay State) меняется в момент доставания нового оружия.

Пока оружие в руках не сменилось, `GetTargetWeaponIndex()` возвращает то, что будет в руках после смены.

### Монтаж доставания автомата

`AM_CharacterRifle_Equip_Montage` собран из `MM_Rifle_Equip` (слот `UpperBody`, блендинг 0.25 с). Если нужно другое доставание автомата и дробовика, подставь свой монтаж в **Character Equip Montage**: он должен играть в слоте `UpperBody`.
